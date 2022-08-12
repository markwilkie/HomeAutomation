-- require st provided libraries
local capabilities = require "st.capabilities"
local Driver = require "st.driver"
local log = require "log"
local cosock = require "cosock"
local http = cosock.asyncify "socket.http"
local ltn12 = require('ltn12')
local json = require('dkjson')

local myserver = require "myserver"
local globals = require "globals"

-- Custom Capabiities
local atmospressure = capabilities["radioamber53161.atmospressure"]
local datetime = capabilities["radioamber53161.datetime"]

-- require custom handlers from driver package
local discovery = require "discovery"

-----------------------------------------------------------------
-- local functions
-----------------------------------------------------------------

-- dump tables
local function dump(o)
  if type(o) == 'table' then
     local s = '{ '
     for k,v in pairs(o) do
        if type(k) ~= 'number' then k = '"'..k..'"' end
        s = s .. '['..k..'] = ' .. dump(v) .. ','
     end
     return s .. '} '
  else
     return tostring(o)
  end
end

-- Send LAN HTTP Request
local function send_lan_command(url, method, path, body)
  local dest_url = url..'/'..path
  local res_body = {}
  local code

  log.debug(string.format("Calling URL (v1.3): [%s]", dest_url))

  -- HTTP Request
  if(method=="POST") then
    _, code = http.request({
      method=method,
      url=dest_url,
      sink=ltn12.sink.table(res_body),
      headers={
        ['Content-Type'] = 'application/x-www-urlencoded',
        ["Content-Length"] = body:len()
      },
      source = ltn12.source.string(body),
    })
  end

  if(method=="GET") then
    _, code = http.request({
      method=method,
      url=dest_url,
      sink=ltn12.sink.table(res_body),
      headers={
        ['Content-Type'] = 'application/x-www-urlencoded'
      }
    })
  end
  log.debug(string.format("Returned HTTP Code: [%s]", code))
  log.debug(dump(res_body))

  -- Handle response
  if code == 200 or code == 201 then
    return true, res_body
  end
  return false, nil
end

-- handshake post
local function handshakeNow()
  local currentEpoch=os.time()-(7*60*60)
  log.debug("Handshaking Now:  "..[[ {"epoch":]]..currentEpoch..[[,"hubAddress":"]]..server_ip..[[","hubPort":]]..server_port..[[ } ]]);

  local success, data = send_lan_command(
    'http://192.168.15.108:80',
    'POST',
    'handshake',
    [[ {"epoch":]]..currentEpoch..[[,"hubAddress":"]]..server_ip..[[","hubPort":]]..server_port..[[ } ]])

    if(success) then
      log.debug("Successfuly handshook:  ( epoch: "..currentEpoch.." IP: "..server_ip.." Port: "..server_port);
    else
      log.debug("Handshaking NOT successful");
      return false
    end

    return true
end

function RefreshWeather(content)
  log.debug("Refreshing Weather Data")
  local jsondata = json.decode(content);

  globals.currentTime = os.date("%a %X", jsondata.current_time)
  globals.temperature = tonumber(string.format("%.1f", jsondata.temperature))
  globals.heatindex = tonumber(string.format("%.1f", jsondata.heat_index))
  globals.humidity = tonumber(string.format("%.1f", jsondata.humidity))
  globals.pressure = tonumber(string.format("%.1f", jsondata.pressure))
  globals.dewPoint = tonumber(string.format("%.1f", jsondata.dew_point))
  globals.uvIndex = tonumber(string.format("%.1f", jsondata.uv))
  globals.ldr = jsondata.ldr
  globals.moisture = jsondata.moisture

  globals.temperatureChangeLastHour = tonumber(string.format("%.1f", jsondata.temperature_change_last_hour))
  globals.pressureChangeLastHour = tonumber(string.format("%.1f", jsondata.pressure_change_last_hour))

  globals.maxTemperature = tonumber(string.format("%.1f", jsondata.temperature_max_last12))
  globals.temperature_max_time_last12 = os.date("%a %X", jsondata.temperature_max_time_last12)
  globals.minTemperature = tonumber(string.format("%.1f", jsondata.temperature_min_last12))
  globals.temperature_min_time_last12 = os.date("%a %X", jsondata.temperature_main_time_last12)
end

-- Get latest wind updates
local function emitWeatherData(driver, device)
  log.debug(string.format("[%s] Emiting Weather Data", device.device_network_id))

  device:emit_event(capabilities.temperatureMeasurement.temperature(globals.temperature))
  device:emit_event(capabilities.relativeHumidityMeasurement.humidity(globals.humidity))
  device:emit_event(atmospressure.pressure(globals.pressure))
  device:emit_event(capabilities.ultravioletIndex.ultravioletIndex(uglobals.vIndex))
  device:emit_event(capabilities.illuminanceMeasurement.illuminance(globals.ldr))
  device:emit_event(capabilities.waterSensor.water(globals.moisture))
  device:emit_event(capabilities.dewPoint.dewpoint(globals.dewPoint))
  device:emit_event(datetime.globals.currentTime)

  device:emit_component_event(device.profile.components['heatIndex'],capabilities.temperatureMeasurement.temperature(globals.heatindex))
  device:emit_component_event(device.profile.components['lastHour'],capabilities.temperatureMeasurement.temperature(globals.temperatureChangeLastHour))
  --device:emit_component_event(device.profile.components['lastHour'],atmospressure.pressure(globals.pressureChangeLastHour))

  device:emit_component_event(device.profile.components['last12Max'],capabilities.temperatureMeasurement.temperature(globals.maxTemperature))
  device:emit_component_event(device.profile.components['last12Max'],datetime.datetime(globals.temperature_max_time_last12))

  device:emit_component_event(device.profile.components['last12Min'],capabilities.temperatureMeasurement.temperature(minTemperature))
  device:emit_component_event(device.profile.components['last12Min'],datetime.datetime(globals.temperature_min_time_last12))

    return true
end

-------------
-- Handlers
----------

-- refresh handler
local function refresh(driver, device)
  log.debug(string.format("[%s] Calling refresh", device.device_network_id))

  if not handshakeNow() then
    return false
  end

  if not emitWeatherData(driver, device) then
    return false
  end

  return true

end

-- this is called once a device is added by the cloud and synchronized down to the hub
local function device_added(driver, device)
  log.info("[" .. device.id .. "] Adding new weather device")
end

-- this is called both when a device is added (but after `added`) and after a hub reboots.
local function device_init(driver, device)
  log.info("[" .. device.id .. "] Initializing weather device")

  -- Startup Server
  myserver.start_server(driver)  

  -- Refresh schedule
  device.thread:call_on_schedule(
    600,
    function ()
      return refresh(driver, device)
    end,
    'Scheduled Refresh')

  refresh(driver, device)
end

-- this is called when a device is removed by the cloud and synchronized down to the hub
local function device_removed(driver, device)
  log.info("[" .. device.id .. "] Removing weather device")
end

-- create the driver object
local wind_driver = Driver("weather", {
  discovery = discovery.handle_discovery,
  lifecycle_handlers = {
    added = device_added,
    init = device_init,
    removed = device_removed
  },
  supported_capabilities = {},
  capability_handlers = {
    -- Refresh command handler
    [capabilities.refresh.ID] = {
    [capabilities.refresh.commands.refresh.NAME] = refresh
    }
  }
})

-- run the driver
wind_driver:run()