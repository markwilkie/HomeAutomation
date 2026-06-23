# Secure Credential Storage Guide

This guide explains how to securely store Tuya credentials for the MiniSplit Matter Bridge.

## Option 1: NVS Secure Storage (Recommended)

The ESP32 has a built-in encrypted NVS (Non-Volatile Storage) partition. This is the **recommended** approach.

### Setup

1. **Generate NVS Encryption Key**
   ```bash
   python $IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py \
     generate-key --keyfile nvs_key.bin
   ```

2. **Configure Secure NVS in menuconfig**
   ```bash
   idf.py menuconfig
   # Go to: Component config → NVS
   # Enable: Enable NVS Encryption (CONFIG_NVS_ENCRYPTION_ENABLED)
   ```

3. **Load Credentials at Runtime**

   Create `src/secure_storage.c`:
   ```c
   #include "nvs_flash.h"
   #include "esp_log.h"

   esp_err_t load_tuya_credentials(char *client_id, size_t id_len,
                                   char *client_secret, size_t secret_len)
   {
       nvs_handle_t handle;
       esp_err_t err = nvs_open("tuya", NVS_READONLY, &handle);
       if (err != ESP_OK) return err;

       err = nvs_get_str(handle, "client_id", client_id, &id_len);
       if (err != ESP_OK) goto cleanup;

       err = nvs_get_str(handle, "client_secret", client_secret, &secret_len);

   cleanup:
       nvs_close(handle);
       return err;
   }

   esp_err_t save_tuya_credentials(const char *client_id, const char *client_secret)
   {
       nvs_handle_t handle;
       esp_err_t err = nvs_open("tuya", NVS_READWRITE, &handle);
       if (err != ESP_OK) return err;

       err = nvs_set_str(handle, "client_id", client_id);
       if (err != ESP_OK) goto cleanup;

       err = nvs_set_str(handle, "client_secret", client_secret);
       if (err != ESP_OK) goto cleanup;

       err = nvs_commit(handle);

   cleanup:
       nvs_close(handle);
       return err;
   }
   ```

### First-Time Setup

```bash
# After flashing, use console to set credentials:
# (from serial monitor)

nvs_set_str tuya client_id "qt4e7qf9mnagnhptxn37"
nvs_set_str tuya client_secret "YOUR_SECRET_HERE"
```

---

## Option 2: Environment Variables

For development only. **NEVER use for production**.

1. Create `.env.local` (add to .gitignore):
   ```
   TUYA_CLIENT_ID=qt4e7qf9mnagnhptxn37
   TUYA_CLIENT_SECRET=your_secret_here
   ```

2. Load at compile time:
   ```bash
   export TUYA_CLIENT_SECRET=$(grep TUYA_CLIENT_SECRET .env.local | cut -d= -f2)
   idf.py build
   ```

---

## Option 3: Provisioning App

Create a simple provisioning app that:
1. Hosts WiFi AP (e.g., "MiniSplit-Setup")
2. Serves web interface for credential entry
3. Validates credentials with Tuya API
4. Saves to NVS encrypted storage
5. Restarts in normal mode

This is more user-friendly for end users.

---

## Security Best Practices

- ✅ Use NVS encryption (Option 1)
- ✅ Never commit `.env` files to git
- ✅ Rotate Tuya API credentials regularly
- ✅ Use separate credentials for development/production
- ✅ Enable WiFi security (WPA3 if available, at least WPA2)
- ✅ Use HTTPS/TLS for all Tuya API calls

---

## Credential Rotation

Tuya credentials should be rotated periodically:

1. Generate new credentials in Tuya developer console
2. Update NVS storage with new values
3. Verify device still connects
4. Retire old credentials

---

## Troubleshooting

**NVS encryption key errors:**
- Verify `nvs_key.bin` is flashed to correct partition
- Check flash layout: `idf.py partition-table`

**Cannot read credentials:**
- Verify NVS partition was properly initialized
- Check encryption is enabled in menuconfig
- Use `nvs-dump` tool to inspect (requires key)

**Credentials lost after reboot:**
- Verify NVS commit was called
- Check NVS partition size is adequate
- Inspect OTA partition conflicts

---

## References

- [ESP-IDF NVS Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html)
- [NVS Encryption Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_encryption_scheme.html)
- [Tuya API Security](https://developer.tuya.com/en/docs/iot/api-request-authorization?id=Ka2f97z8q0dpl)
