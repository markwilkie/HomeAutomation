handshakeRequired = true
server_ip = ""
server_port = 0
lastHeardFromWeather = 0

--weather data
temperature = nil
humidity = nil
heatindex = nil
pressure = nil
dewPoint = nil
uvIndex = nil
ldr = nil
moisture = nil
temperatureChangeLastHour = nil
pressureChangeLastHour = nil
maxTemperature = nil
minTemperature = nil
currentTime = nil
temperature_max_time_last12 = nil
temperature_min_time_last12 = nil

return {
    handshakeRequired = handshakeRequired,
    lastHeardFromWeather = lastHeardFromWeather,
    server_ip = server_ip,
    server_port = server_port,
    currentTime = currentTime,
    temperature = temperature,
    humidity = humidity,
    pressure = pressure,
    dewPoint = dewPoint,
    uvIndex = uvIndex,
    ldr = ldr,
    moisture = moisture,
    temperatureChangeLastHour = temperatureChangeLastHour,
    pressureChangeLastHour = pressureChangeLastHour,
    maxTemperature = maxTemperature,
    minTemperature = minTemperature,
    temperature_max_time_last12 = temperature_max_time_last12,
    temperature_min_time_last12 = temperature_min_time_last12
  }

