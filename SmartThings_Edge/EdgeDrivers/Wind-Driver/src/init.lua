-- require st provided libraries
local capabilities = require "st.capabilities"
local Driver = require "st.driver"
local log = require "log"
local cosock = require "cosock"
local http = cosock.asyncify "socket.http"
local ltn12 = require('ltn12')
local json = require('dkjson')

local commonglobals = require "_commonglobals"
local myserver = require "_myserver"
local myclient = require "_myclient"
local winddatastore = require "winddatastore"
local globals = require "globals"

-- Custom Capabiities
local datetime = capabilities["radioamber53161.datetime"]
local windspeed = capabilities["radioamber53161.windspeed"]
local windgust = capabilities["radioamber53161.windgust"]
local winddirection = capabilities["radioamber53161.winddirection"]
local winddirectiontext = capabilities["radioamber53161.winddirectiontext"]

-- require custom handlers from driver package
local discovery = require "discovery"


-----------------------------------------------------------------
-- local functions
-----------------------------------------------------------------

function RefreshWind(content)
  log.debug("Refreshing Wind Data...")
  
  commonglobals.lastHeardFromESP = os.time()
  commonglobals.handshakeRequired = false
  commonglobals.newDataAvailable = true

  local jsondata = json.decode(content);
  globals.windSpeed = tonumber(string.format("%.1f", jsondata.wind_speed))
  globals.windGust = tonumber(string.format("%.1f", jsondata.wind_gust))
  globals.winddirection = jsondata.wind_direction_label .. " (" .. jsondata.wind_direction .. "°)"
  globals.currentTime = os.date("%a %X", jsondata.current_time)

  --adding to the data store so we can get max
  winddatastore.insertData(jsondata.current_time,globals.windGust,globals.winddirection)

  --get max gust info
  local gusttime, gustspeed, gustdirection = winddatastore.findMaxGust()
  globals.windGustLast12 = tonumber(string.format("%.1f", gustspeed))
  globals.windGustDirectionLast12 = gustdirection
  globals.windGustTimeLast12 = os.date("%a %X", gusttime)
end

-- Get latest wind updates
local function emitWindData(driver, device)
  log.info("Emiting Wind Data")

  --setting preferences
  globals.windVaneOffset=device.preferences.windVaneOffset  

  device:emit_event(windgust.gust(globals.windGust))
  device:emit_event(windspeed.speed(globals.windSpeed))
  device:emit_event(winddirectiontext.directiontext(globals.winddirection))
  device:emit_event(datetime.datetime(globals.currentTime))
  device:emit_component_event(device.profile.components['last12'],windgust.gust(globals.windGustLast12))
  device:emit_component_event(device.profile.components['last12'],winddirectiontext.directiontext(globals.windGustDirectionLast12))
  device:emit_component_event(device.profile.components['last12'],datetime.datetime(globals.windGustTimeLast12))

  return true
end

-------------
-- Handlers
----------

-- refresh handler
local function refresh(driver, device)
  log.info("Calling refresh...")

  --check if we've heard from devices lately
  if os.time() > (commonglobals.lastHeardFromESP + 650) then
    commonglobals.handshakeRequired = true
    log.warn("Haven't heard from Wind so going into handshake mode.")
  end

  --hand shake if needed
  if commonglobals.handshakeRequired then
    if not myclient.handshakeNow("wind") then
      return false
    end
  end

  --Actually update the fields on the smart app
  if commonglobals.newDataAvailable then
    commonglobals.newDataAvailable = false
    if not emitWindData(driver, device) then
      return false
    end
  end

  return true
end

-- this is called once a device is added by the cloud and synchronized down to the hub
local function device_added(driver, device)
  log.info("[" .. device.id .. "] Adding new wind device and setting preferences")

  globals.windVaneOffset=device.preferences.windVaneOffset
end

-- this is called both when a device is added (but after `added`) and after a hub reboots.
local function device_init(driver, device)
  log.info("[" .. device.id .. "] Initializing wind device")

  -- Startup Server
  myserver.start_server(driver)  

  -- Refresh schedule
  device.thread:call_on_schedule(
    60,
    function ()
      return refresh(driver, device)
    end,
    'Scheduled Refresh')

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
    windspeed,
    winddirection,
    windgust,
    datetime
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