# MiniSplit Matter Bridge Configuration Template

## Tuya API Configuration

**Device ID:** eb11d9ff75ef37d109pihg

**Client ID:** qt4e7qf9mnagnhptxn37

**Client Secret:** [STORE IN SECURE NVS - DO NOT COMMIT]

**API Endpoint:** https://openapi.tuyaus.com/v1.0/

## WiFi Configuration

**SSID:** [CONFIGURE]

**Password:** [CONFIGURE]

## Matter Configuration

**Device Type:** 0x0301 (Thermostat)

**Device Name:** Mini-Split AC

**Vendor ID:** 0xFFF1 (Example - configure for your Matter ecosystem)

**Product ID:** 0x8000 (Example)

## Polling Configuration

**Status Poll Interval:** 30000 ms (30 seconds)

**Command Poll Interval:** 5000 ms (5 seconds)

## Tuya Device Commands

The following commands are available via Tuya API:

```
{
  "code": "switch",      // Power on/off
  "value": true/false
}

{
  "code": "temp_set",    // Target temperature (×100)
  "value": 2100          // 21°C
}

{
  "code": "mode_auto",   // Mode: true to activate
  "value": true
}
```

## Matter Attributes

See ARCHITECTURE.md for full cluster documentation.

### Primary Controls in SmartThings
- Power (OnOff)
- Temperature Reading (LocalTemperature)
- Temperature Setpoint
- Operating Mode

## Deployment

1. Configure Tuya credentials in NVS secure storage
2. Set WiFi SSID/password
3. Flash to ESP32
4. Run "Matter Start Commissioning" via serial console
5. Open SmartThings app → Add Device
6. Scan commissioning QR code or enter setup code
7. Select Matter device from list

## Security Notes

- All Tuya API calls require HMAC-SHA256 signature
- Access token has expiration (~24 hours typical)
- Implement token refresh before expiry
- Use secure NVS partition for credential storage
- Never commit secrets to version control
