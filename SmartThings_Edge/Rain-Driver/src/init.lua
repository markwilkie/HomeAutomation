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
local raindatastore = require "raindatastore"
local globals = require "globals"

-- Custom Capabiities
local datetime = capabilities["radioamber53161.datetime"]
local rainrate = capabilities["radioamber53161.rainrate"]
local raintotal = capabilities["radioamber53161.raintotal"]

-- require custom handlers from driver package
local discovery = require "discovery"


-----------------------------------------------------------------
-- local functions
-----------------------------------------------------------------

function RefreshRain(content)
  log.info("Refreshing Rain Data")
  
  commonglobals.lastHeardFromESP = os.time()
  commonglobals.handshakeRequired = false
  commonglobals.newDataAvailable = true

  local jsondata = json.decode(content);
  globals.rainrate = tonumber(string.format("%.1f", jsondata.rain_rate))
  globals.moisture = jsondata.moisture
  globals.currentTime = os.date("%a %X", jsondata.current_time)

  --adding to the data store so we can get max
  raindatastore.insertData(jsondata.current_time,globals.rainrate)

  --get historical info
  globals.raintotallasthour = raindatastore.findLastHourTotal(1)
  globals.raintotallast12hours = raindatastore.findLast12HoursTotal()
end

-- Get latest rain updates
local function emitRainData(driver, device)
  log.info(string.format("[%s] Emiting Rain Data", device.device_network_id))

  device:emit_event(rainrate.rainrate(globals.rainrate))
  device:emit_event(capabilities.waterSensor.water(globals.moisture))
  device:emit_event(datetime.datetime(globals.currentTime))
  device:emit_component_event(device.profile.components['lastHour'],raintotal.raintotal(globals.raintotallasthour))
  device:emit_component_event(device.profile.components['last12Hour'],raintotal.raintotal(globals.raintotallast12hours))

  return true
end

-------------
-- Handlers
----------

-- refresh handler
local function refresh(driver, device)
  log.debug(string.format("[%s] Calling refresh", device.device_network_id))

  --check if we've heard from devices lately
  if os.time() > (commonglobals.lastHeardFromESP + 650) then
    commonglobals.handshakeRequired = true
    log.warn("Haven't heard from rain so going into handshake mode.")
  end

  --hand shake if needed
  if commonglobals.handshakeRequired then
    if not myclient.handshakeNow("rain") then
      return false
    end
  end

  --Actually update the fields on the smart app
  if commonglobals.newDataAvailable then
    commonglobals.newDataAvailable = false
    if not emitRainData(driver, device) then
      return false
    end
  end

  return true
end

-- this is called once a device is added by the cloud and synchronized down to the hub
local function device_added(driver, device)
  log.info("[" .. device.id .. "] Adding new rain device")
end

-- this is called both when a device is added (but after `added`) and after a hub reboots.
local function device_init(driver, device)
  log.info("[" .. device.id .. "] Initializing rain device")

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
  log.info("[" .. device.id .. "] Removing rain device")
end

local rain_driver = Driver("rain", {
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
rain_driver:run()