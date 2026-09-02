# MiniSplit

## Home Assistant / Climate Rules
- BME280 is the source of truth for room temperature. Apply calibration offsets to the sensor reading, never to the setpoint.
- Do not add smoothing/filtering to Tuya-reported temperature; treat Tuya as a command sink only.
- After changing climate automations, verify by counting setpoint reversals over a comparable window and report before/after numbers.
