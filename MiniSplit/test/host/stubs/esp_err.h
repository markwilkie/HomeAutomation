#ifndef ESP_ERR_H_HOST_STUB
#define ESP_ERR_H_HOST_STUB

// Host-test stand-in for ESP-IDF's esp_err.h -- only what the headers
// included by control_logic.h need.
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1

#endif
