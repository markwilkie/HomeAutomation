-- require st provided libraries
local capabilities = require "st.capabilities"
local Driver = require "st.driver"
local log = require "log"
local http = require('socket.http')
local ltn12 = require('ltn12')
local json = require('dkjson')

-- Custom Capabiities
local capdefs = require "capabilitydefs"
local windspeed = capabilities.build_cap_from_json_string(capdefs.windspeed)
capabilities["radioamber53161.windspeed"] = windspeed
local windgust = capabilities.build_cap_from_json_string(capdefs.windgust)
capabilities["radioamber53161.windgust"] = windgust
local winddirection = capabilities.build_cap_from_json_string(capdefs.winddirection)
capabilities["radioamber53161.winddirection"] = winddirection
local datetime = capabilities.build_cap_from_json_string(capdefs.datetime)
capabilities["radioamber53161.datetime"] = datetime
-- require custom handlers from driver package
local discovery = require "discovery"


-----------------------------------------------------------------
-- local functions
-----------------------------------------------------------------
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

  -- Handle response
  if code == 200 or code == 201 then
    return true, res_body
  end
  return false, nil
end

-- epoch post
local function setEpoch()
  local currentEpoch=os.time()-(7*60*60)    
  local success, data = send_lan_command(
    'http://192.168.15.108:80',
    'POST',
    'setEpoch',
    [[ {"epoch":]]..currentEpoch..[[} ]])

    if(success) then
      log.debug("Set Epoch to: "..currentEpoch);
    else
      log.debug("Setting Epoch NOT successful");
      return false
    end

    return true
end

-- Get latest wind updates
local function getWindData(driver, device)
  log.debug(string.format("[%s] Calling refresh", device.device_network_id))

  local success, data = send_lan_command(
    'http://192.168.15.108:80',
    'GET',
    'refreshWind')

    if(success) then
      local jsondata = json.decode(table.concat(data)..'}');
      
      local windSpeed = tonumber(string.format("%.1f", jsondata.wind_speed))
      local windGust = tonumber(string.format("%.1f", jsondata.wind_gust))
      local windGustLast12 = tonumber(string.format("%.1f", jsondata.wind_gust_max_last12))

      log.debug("Wind speed: "..windSpeed);
      log.debug("Wind direction: "..jsondata.wind_direction);
      log.debug("Wind gust: "..windGust);      
      log.debug("Last Gust Time: "..os.date("%a %X", jsondata.wind_gust_max_time));
      log.debug("Refresh successful - setting device as online");

      device:emit_event(windspeed.speed(windSpeed))
      device:emit_event(winddirection.direction(jsondata.wind_direction))
      device:emit_event(windgust.gust(windGust))
      device:emit_event(datetime.datetime(os.date("%a %X", jsondata.current_time)))
      device:emit_component_event(device.profile.components['last12'],windgust.gust(windGustLast12))
      device:emit_component_event(device.profile.components['last12'],datetime.datetime(os.date("%a %X", jsondata.wind_gust_max_time)))
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

  if not getWindData(driver, device) then
    return false
  end

  return true

end

-- this is called once a device is added by the cloud and synchronized down to the hub
local function device_added(driver, device)
  log.info("[" .. device.id .. "] Adding new wind device")
end

-- this is called both when a device is added (but after `added`) and after a hub reboots.
local function device_init(driver, device)
  log.info("[" .. device.id .. "] Initializing wind device")

  refresh(driver, device)
end

-- this is called when a device is removed by the cloud and synchronized down to the hub
local function device_removed(driver, device)
  log.info("[" .. device.id .. "] Removing wind device")
end

-- create the driver object
local wind_driver = Driver("wind", {
  discovery = discovery.handle_discovery,
  lifecycle_handlers = {
    added = device_added,
    init = device_init,
    removed = device_removed
  },
  supported_capabilities = {
    capabilities.refresh,
    capabilities.windspeed,
    capabilities.winddirection,
    capabilities.windgust,
    capabilities.temperatureMeasurement,
    capabilities.battery
  },
  capability_handlers = {
    -- Refresh command handler
    [capabilities.refresh.ID] = {
    [capabilities.refresh.commands.refresh.NAME] = refresh
    }
  }
})

-- run the driver
wind_driver:run()