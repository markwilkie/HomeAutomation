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
-- require custom handlers from driver package
local discovery = require "discovery"

-------------
-- Handlers
----------

------------------------
-- Send LAN HTTP Request
local function send_lan_command(url, method, path, body)
  local dest_url = url..'/'..path
  --local dest_url = 'https://api.weather.gov/alerts/active?area=WA'
  local res_body = {}
  --local query = neturl.buildQuery(body or {})

  log.debug(string.format("Calling URL (v1.3): [%s]", dest_url))

  -- HTTP Request
  local _, code = http.request({
    method=method,
    url=dest_url,
    sink=ltn12.sink.table(res_body),
    --headers={
    --['Content-Type'] = 'application/x-www-urlencoded'
    --}
  })

  log.debug(string.format("Returned HTTP Code: [%s]", code))

  -- Handle response
  if code == 200 then
    return true, res_body
  end
  return false, nil
end

-- refresh handler
local function refresh(driver, device)
  log.debug(string.format("[%s] Calling refresh", device.device_network_id))

  local success, data = send_lan_command(
    'http://192.168.15.108:80',
    'GET',
    'refresh')

    if(success) then
      local jsondata = json.decode(table.concat(data)..'}');
      log.debug("Wind speed: "..jsondata.wind_speed);
      log.debug("Wind direction: "..jsondata.wind_direction);
      log.debug("Wind gust: "..jsondata.wind_gust);      
      log.debug("Refresh successful - setting device as online");

      device:emit_event(windspeed.speed(jsondata.wind_speed))
      device:emit_event(winddirection.direction(jsondata.wind_direction))
      device:emit_event(windgust.gust(jsondata.wind_gust))
      device:online()
    else
      log.debug("Refresh NOT successful - setting device as offline");
      device:offline()
    end
end



-----------------------------------------------------------------
-- local functions
-----------------------------------------------------------------
-- this is called once a device is added by the cloud and synchronized down to the hub
local function device_added(driver, device)
  log.info("[" .. device.id .. "] Adding new wind device")
end

-- this is called both when a device is added (but after `added`) and after a hub reboots.
local function device_init(driver, device)
  log.info("[" .. device.id .. "] Initializing wind device")

  refresh(driver,device)

  -- mark device as online so it can be controlled from the app
  device:online()
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
    capabilities.windgust
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