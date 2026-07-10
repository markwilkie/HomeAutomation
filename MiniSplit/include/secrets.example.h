#ifndef APP_SECRETS_H
#define APP_SECRETS_H

// Copy this file to include/secrets.h and fill in your real values.
// No WiFi credentials here -- this device joins over Thread, and the
// Thread Operational Dataset is provisioned by the commissioner (Home
// Assistant, via OTBR) during Matter/BLE commissioning, not baked into
// firmware.
#define TUYA_DEVICE_ID "your_tuya_device_id"
#define TUYA_CLIENT_ID "your_tuya_client_id"
#define TUYA_CLIENT_SECRET "your_tuya_client_secret"

#endif // APP_SECRETS_H
