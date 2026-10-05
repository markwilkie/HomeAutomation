# Tuya API Implementation Notes

## Current Status

### Completed (Phase 1.1-1.4)

✅ **HMAC-SHA256 Signature Calculation**
- Implements Tuya signature scheme: `HMAC-SHA256(client_secret, string_to_sign)`
- `string_to_sign = METHOD + "\n" + CONTENT_HASH + "\n" + HEADERS + "\n" + URL`
- Uses mbedtls for cryptographic operations

✅ **HTTP Client Setup**
- Wrapper around `esp_http_client` with automatic header management
- Sets required Tuya headers: `client_id`, `t` (timestamp), `sign_method`, `sign`
- Supports GET and POST methods

✅ **Device Status Polling**
- `tuya_get_device_status()` → GET `/v1.0/iot-03/devices/status`
- Parses JSON response with cJSON
- Maps Tuya device properties to local structure
- Supports all device properties from mini-split

✅ **Device Commands**
- `tuya_set_power(bool)` → POST to send `{"code": "switch", "value": true/false}`
- `tuya_set_temperature(int16_t)` → POST to send `{"code": "temp_set", "value": temp_c}`
- `tuya_set_mode(uint8_t)` → POST to send mode commands (auto/eco/dry/heat)

## Key Implementation Details

### Authentication Flow

```
1. Get current timestamp (ms since epoch)
2. Calculate SHA256(request_body) → content_hash
3. Build string_to_sign:
   METHOD + "\n"
   + content_hash + "\n"
   + "client_id=<id>\nt=<timestamp>\n" + "\n"
   + endpoint_url
4. Calculate HMAC-SHA256(client_secret, string_to_sign) → signature
5. Add headers:
   - client_id: <client_id>
   - t: <timestamp>
   - sign_method: HMAC-SHA256
   - sign: <signature_hex>
   - access_token: <token> (if available)
```

### JSON Structure Examples

**Status Response:**
```json
{
  "result": [
    {
      "id": "device_id",
      "status": [
        {"code": "switch", "value": true},
        {"code": "temp_set", "value": 2200},
        // ... more properties
      ]
    }
  ],
  "success": true,
  "t": 1234567890,
  "tid": "request_id"
}
```

**Command Request:**
```json
{
  "commands": [
    {"code": "switch", "value": true}
  ]
}
```

## Debugging Tips

### Enable Debug Logging
```c
// In main.c
esp_log_level_set("TUYA_CLIENT", ESP_LOG_DEBUG);
```

### Common Issues

1. **Invalid Signature Error**
   - Verify client_secret is correct
   - Check timestamp is recent (within ±5 minutes of API server)
   - Ensure method (GET/POST) matches what's signed

2. **Device Not Found**
   - Verify device_id is correct
   - Check device is online in Tuya app

3. **Request Timeout**
   - Check WiFi connection
   - Verify API endpoint URL
   - Tuya API might be throttling (rate limit)

4. **Token Expired**
   - First requests don't need access_token
   - If using tokens, implement refresh logic
   - See `tuya_refresh_token()` placeholder

## Testing

### Unit Test Ideas
```c
// Test HMAC-SHA256 calculation
void test_signature_calculation(void) {
    char sig[65];
    calculate_tuya_signature("GET", "/path", "", 1000, sig);
    assert_string_equal(sig, "expected_signature");
}

// Test JSON parsing
void test_parse_status_response(void) {
    const char *json = "{...}";
    tuya_device_status_t status = {0};
    parse_status_json(json, &status);
    assert_int_equal(status.temp_current, 2200);
}
```

### Integration Testing
1. Configure real Tuya device credentials
2. Run `tuya_api_test.c` demo
3. Verify device status is read correctly
4. Send power command and verify device responds
5. Monitor Tuya app for updates

## Performance Notes

- Status polling: ~500ms per request (network dependent)
- Command execution: ~300ms per command
- HTTP buffer: 4096 bytes (adjust if needed for large responses)
- Signature calculation: <1ms (CPU dependent)

## Future Enhancements

- [ ] Token refresh with OAuth2 flow
- [ ] Retry logic with exponential backoff
- [ ] Connection pooling for multiple commands
- [ ] Rate limiting to avoid Tuya throttling
- [ ] Caching of device status
- [ ] Event subscription (webhooks) instead of polling

## References

- [Tuya API Sign Algorithm](https://developer.tuya.com/en/docs/iot/api-request-authorization)
- [ESP-IDF HTTP Client](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_client.html)
- [cJSON Library](https://github.com/DaveGamble/cJSON)
- [mbedtls HMAC](https://mbed-tls.readthedocs.io/en/latest/)
