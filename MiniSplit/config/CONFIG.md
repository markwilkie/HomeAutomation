# MiniSplit Matter Bridge Configuration Template

## Tuya API Configuration

**Device ID:** eb11d9ff75ef37d109pihg

**Client ID:** qt4e7qf9mnagnhptxn37

**Client Secret:** [STORE IN SECURE NVS - DO NOT COMMIT]

**API Endpoint:** https://openapi.tuyaus.com/v1.0/

## Network Configuration

No pre-configuration needed. This device joins over Thread — the Thread Operational Dataset is
provisioned by the commissioner (Home Assistant, via OTBR) during Matter/BLE commissioning, not
baked into firmware. (Legacy WiFi-build note: earlier firmware pre-connected to WiFi using
`WIFI_SSID`/`WIFI_PASSWORD` in `include/secrets.h` — no longer used.)

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

### Primary Controls in Home Assistant
- Power (OnOff)
- Temperature Reading (LocalTemperature)
- Temperature Setpoint
- Operating Mode

## Deployment

1. Configure Tuya credentials in NVS secure storage (`include/secrets.h`)
2. Flash to ESP32 — device starts BLE/Matter commissioning immediately, no network pre-connect
3. In Home Assistant: Settings → Devices & Services → Add Integration → Matter → Add device
4. Scan commissioning QR code or enter setup code
5. Home Assistant/OTBR provisions the Thread dataset as part of commissioning
6. Select Matter device from list

## Security Notes

- All Tuya API calls require HMAC-SHA256 signature
- Access token has expiration (~24 hours typical)
- Implement token refresh before expiry
- Use secure NVS partition for credential storage
- Never commit secrets to version control
