/**
 * @file flash_log.c
 * @brief Flash-backed ring log: local storage only, no network traffic ever.
 *
 * Design notes:
 * - Armed state is NOT persisted across a reboot, and the ring is fully
 *   erased and restarted fresh on every flash_log_enable() call. This is a
 *   deliberate simplification: any data already written before a reboot
 *   (including the last line before a crash, since writes are unbatched)
 *   stays on flash and stays readable regardless of armed state -- so the
 *   one thing that's actually lost by not persisting armed-state is
 *   continued capture across a *second* subsequent reboot without a human
 *   re-enabling in between. Given this is an on-demand debugging tool, not
 *   an always-on monitor, that's an acceptable tradeoff for the simplicity
 *   of not needing to reconstruct the write position by scanning flash on
 *   every boot.
 * - Writes are immediate (no RAM staging/batching) specifically so the most
 *   recent line before a crash is already safely on flash by the time the
 *   crash happens.
 * - This never touches the network. A flash write costs real per-line CPU
 *   time, but that cost is local to whichever task calls ESP_LOG -- it
 *   cannot compete with Matter/Thread for OpenThread message buffers the
 *   way the (now-disabled) Papertrail UDP logger did, regardless of volume.
 */

#include "flash_log.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define LOG_PARTITION_LABEL "log_ring"
#define SECTOR_SIZE 4096
#define FLASH_LOG_MAX_LINE 400

static const esp_partition_t *s_partition = NULL;
static size_t s_write_offset = 0;
static bool s_wrapped = false;
static volatile bool s_armed = false;
static int s_last_erased_sector_in_wrap = -1;

static vprintf_like_t s_prev_vprintf = NULL;
static SemaphoreHandle_t s_lock = NULL;
static volatile bool s_in_write = false;

static char s_line[FLASH_LOG_MAX_LINE];

// Caller must hold s_lock. Only erases when wrapped -- the first pass
// through the ring relies on the full-partition erase done in
// flash_log_enable(), so sectors are already blank until we come back
// around and need to reuse them.
static void ensure_sector_ready(size_t offset)
{
    if (!s_wrapped) {
        return;
    }
    int sector = (int)(offset / SECTOR_SIZE);
    if (sector == s_last_erased_sector_in_wrap) {
        return;
    }
    esp_partition_erase_range(s_partition, (size_t)sector * SECTOR_SIZE, SECTOR_SIZE);
    s_last_erased_sector_in_wrap = sector;
}

// Caller must hold s_lock.
static void flash_log_write_line(const char *line, size_t len)
{
    if (!s_partition || len == 0) {
        return;
    }
    if (len > s_partition->size) {
        len = s_partition->size;
    }

    if (s_write_offset + len > s_partition->size) {
        s_write_offset = 0;
        s_wrapped = true;
        s_last_erased_sector_in_wrap = -1;
    }

    // A line can straddle a sector boundary -- make sure both the sector it
    // starts in and (if different) the sector it ends in are ready before
    // the actual write.
    ensure_sector_ready(s_write_offset);
    if (len > 0) {
        ensure_sector_ready(s_write_offset + len - 1);
    }

    esp_partition_write(s_partition, s_write_offset, line, len);
    s_write_offset += len;
}

static int flash_log_vprintf(const char *fmt, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);
    int ret = s_prev_vprintf ? s_prev_vprintf(fmt, args_copy) : vprintf(fmt, args_copy);
    va_end(args_copy);

    if (!s_armed || s_in_write || !s_lock) {
        return ret;
    }

    // Bounded wait, never indefinite -- called from every task in the app.
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ret;
    }

    va_copy(args_copy, args);
    int n = vsnprintf(s_line, sizeof(s_line) - 1, fmt, args_copy);
    va_end(args_copy);
    if (n <= 0) {
        xSemaphoreGive(s_lock);
        return ret;
    }
    if (n >= (int)sizeof(s_line) - 1) {
        n = sizeof(s_line) - 2;
    }

    // Strip ANSI color escape sequences in place, same as the Papertrail
    // logger did -- this is read back as plain text over HTTP.
    int ci = 0;
    for (int i = 0; i < n; i++) {
        if (s_line[i] == '\033') {
            while (i < n && s_line[i] != 'm') {
                i++;
            }
            continue;
        }
        s_line[ci++] = s_line[i];
    }
    if (ci == 0 || (ci == 1 && s_line[0] == '\n')) {
        xSemaphoreGive(s_lock);
        return ret;
    }
    if (s_line[ci - 1] != '\n') {
        s_line[ci++] = '\n';
    }

    s_in_write = true;
    flash_log_write_line(s_line, (size_t)ci);
    s_in_write = false;

    xSemaphoreGive(s_lock);
    return ret;
}

esp_err_t flash_log_init(void)
{
    s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
                                            LOG_PARTITION_LABEL);
    if (!s_partition) {
        ESP_LOGE("FLASH_LOG", "Partition '%s' not found -- check partitions.csv", LOG_PARTITION_LABEL);
        return ESP_ERR_NOT_FOUND;
    }

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    s_prev_vprintf = esp_log_set_vprintf(flash_log_vprintf);
    return ESP_OK;
}

esp_err_t flash_log_enable(void)
{
    if (!s_partition || !s_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = esp_partition_erase_range(s_partition, 0, s_partition->size);
    s_write_offset = 0;
    s_wrapped = false;
    s_last_erased_sector_in_wrap = -1;
    if (err == ESP_OK) {
        s_armed = true;
    }
    xSemaphoreGive(s_lock);
    return err;
}

void flash_log_disable(void)
{
    s_armed = false;
}

bool flash_log_is_enabled(void)
{
    return s_armed;
}

size_t flash_log_size(void)
{
    if (!s_partition) {
        return 0;
    }
    return s_wrapped ? s_partition->size : s_write_offset;
}

size_t flash_log_read_chunk(size_t offset, char *buf, size_t buf_len)
{
    if (!s_partition || !buf || buf_len == 0) {
        return 0;
    }
    size_t total = flash_log_size();
    if (offset >= total) {
        return 0;
    }
    size_t to_read = buf_len;
    if (offset + to_read > total) {
        to_read = total - offset;
    }

    if (!s_wrapped) {
        if (esp_partition_read(s_partition, offset, buf, to_read) != ESP_OK) {
            return 0;
        }
        return to_read;
    }

    size_t phys_start = (s_write_offset + offset) % s_partition->size;
    size_t first_chunk = s_partition->size - phys_start;
    if (first_chunk >= to_read) {
        if (esp_partition_read(s_partition, phys_start, buf, to_read) != ESP_OK) {
            return 0;
        }
    } else {
        if (esp_partition_read(s_partition, phys_start, buf, first_chunk) != ESP_OK) {
            return 0;
        }
        if (esp_partition_read(s_partition, 0, buf + first_chunk, to_read - first_chunk) != ESP_OK) {
            return 0;
        }
    }
    return to_read;
}
