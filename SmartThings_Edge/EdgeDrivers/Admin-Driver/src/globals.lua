--admin data

currentTime = nil
wifiOnlyState = nil
boostModeState = nil
powerSaverModeState = nil
cap_voltage = nil
vcc_voltage = nil
rssi = nil
firmware_version = nil
cpu_reset_code = nil
cpu_reset_reason = nil
heap_fragmentation = nil

return {
    currentTime = currentTime,
    wifiOnlyState = wifiOnlyState,
    boostModeLState = boostModeState,
    powerSaverModeState = powerSaverModeState,    
    cap_voltage = cap_voltage,
    vcc_voltage = vcc_voltage,    
    rssi = rssi,
    firmware_version = firmware_version,
    cpu_reset_code = cpu_reset_code,
    cpu_reset_reason = cpu_reset_reason,
    heap_fragmentation = heap_fragmentation
}

