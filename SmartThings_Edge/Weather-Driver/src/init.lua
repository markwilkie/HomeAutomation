-- require st provided libraries
local capabilities = require "st.capabilities"
local Driver = require "st.driver"
local log = require "log"
local cosock = require "cosock"
local http = cosock.asyncify "socket.http"
local ltn12 = require('ltn12')
local json = require('dkjson')

-- Custom Capabiities
local atmospressure = capabilities["radioamber53161.atmospressure"]
local datetime = capabilities["radioamber53161.datetime"]

--local capdefs = require "capabilitydefs"
--local atmospressure = capabilities.build_cap_from_json_string(capdefs.atmospressure)
--capabilities["radioamber53161.atmospressure"] = atmospressure
--local datetime = capabilities.build_cap_from_json_string(capdefs.datetime)
--capabilities["radioamber53161.datetime"] = datetime

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

-- epoch post
local function setEpoch()
  local currentEpoch=os.time()-(7*60*60)
  log.debug([[ {"epoch":]]..currentEpoch..[[} ]]);

  local success, data = send_lan_command(
    'http://192.168.15.108:80',
    'POST',
    'setEpoch',
    [[ {"epoch":]]..currentEpoch..[[} ]])

    if(success) then
      log.debug("Successfuly set Epoch to: "..currentEpoch);
    else
      log.debug("Setting Epoch NOT successful");
      return false
    end

    return true
end

-- Get latest wind updates
local function getWeatherData(driver, device)
  log.debug(string.format("[%s] Calling refresh", device.device_network_id))

  local success, data = send_lan_command(
    'http://192.168.15.108:80',
    'GET',
    'refreshWeather')

    if(success) then
      log.debug("Refresh successful - setting device as online");
      
      local jsondata = json.decode(table.concat(data)..'}');
      
      local temperature = tonumber(string.format("%.1f", jsondata.temperature))
      local heatindex = tonumber(string.format("%.1f", jsondata.heat_index))
      local humidity = tonumber(string.format("%.1f", jsondata.humidity))
      local pressure = tonumber(string.format("%.1f", jsondata.pressure))
      local dewPoint = tonumber(string.format("%.1f", jsondata.dew_point))
      local uvIndex = tonumber(string.format("%.1f", jsondata.uv))

      local temperatureChangeLastHour = tonumber(string.format("%.1f", jsondata.temperature_change_last_hour))
      local pressureChangeLastHour = tonumber(string.format("%.1f", jsondata.pressure_change_last_hour))

      local maxTemperature = tonumber(string.format("%.1f", jsondata.temperature_max_last12))
      local minTemperature = tonumber(string.format("%.1f", jsondata.temperature_min_last12))

      device:emit_event(capabilities.temperatureMeasurement.temperature(temperature))
      device:emit_event(capabilities.relativeHumidityMeasurement.humidity(humidity))
      --device:emit_event(capabilities.atmosphericPressureMeasurement.atmosphericPressure(pressure))
      device:emit_event(atmospressure.pressure(pressure))
      device:emit_event(capabilities.ultravioletIndex.ultravioletIndex(uvIndex))
      device:emit_event(capabilities.illuminanceMeasurement.illuminance(jsondata.ldr))
      device:emit_event(capabilities.waterSensor.water(jsondata.moisture))
      device:emit_event(capabilities.dewPoint.dewpoint(dewPoint))
      device:emit_event(datetime.datetime(os.date("%a %X", jsondata.current_time)))

      device:emit_component_event(device.profile.components['heatIndex'],capabilities.temperatureMeasurement.temperature(heatindex))

      device:emit_component_event(device.profile.components['lastHour'],capabilities.temperatureMeasurement.temperature(temperatureChangeLastHour))
      --device:emit_component_event(device.profile.components['lastHour'],atmospressure.pressure(pressureChangeLastHour))

      device:emit_component_event(device.profile.components['last12Max'],capabilities.temperatureMeasurement.temperature(maxTemperature))
      device:emit_component_event(device.profile.components['last12Max'],datetime.datetime(os.date("%a %X", jsondata.temperature_max_time_last12)))

      device:emit_component_event(device.profile.components['last12Min'],capabilities.temperatureMeasurement.temperature(minTemperature))
      device:emit_component_event(device.profile.components['last12Min'],datetime.datetime(os.date("%a %X", jsondata.temperature_min_time_last12)))
    else
      log.debug("Refresh NOT successful - setting device as offline");
      return false
    end

    return true
end

-------------
-- Handlers
----------

-- refresh handler
local function refresh(driver, device)
  log.debug(string.format("[%s] Calling refresh", device.device_network_id))

  if not setEpoch() then
    return false
  end

  if not getWeatherData(driver, device) then
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