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
local globals = require "globals"

-- Custom Capabiities
local lastupdated = capabilities["radioamber53161.lastUpdated"]
local voltage = capabilities["radioamber53161.voltage"]
local firmware = capabilities["radioamber53161.firmware"]
local cpureset = capabilities["radioamber53161.cpuReset"]
local heapfragmentation = capabilities["radioamber53161.heapFragmentation"]
local soil = capabilities["radioamber53161.soil"]

-- require custom handlers from driver package
local discovery = require "discovery"


-----------------------------------------------------------------
-- local functions
-----------------------------------------------------------------

function RefreshSoil(content)
  log.debug("Refreshing Soil Data")
  
  commonglobals.lastHeardFromESP = os.time()
  commonglobals.handshakeRequired = false
  commonglobals.newDataAvailable = true

  local jsondata = json.decode(content)
  globals.soil_moisture = jsondata.soil_moisture
  globals.rssi = jsondata.wifi_strength
  globals.firmware_version = jsondata.firmware_version
  globals.heap_fragmentation = jsondata.heap_frag
  globals.currentTime = os.date("%a %X", jsondata.current_time)

end

-- Get latest soil updates
local function emitSoilData(driver, device)
  log.info("Emiting Soil Data")

  device:emit_event(soil.Moisture(globals.soil_moisture))
  device:emit_event(capabilities.signalStrength.rssi(globals.rssi))
  device:emit_event(heapfragmentation.Fragmentation(globals.heap_fragmentation))
  device:emit_event(firmware.Version(globals.firmware_version))
  device:emit_event(lastupdated.Time(globals.currentTime))

  return true
end

-------------
-- Handlers
----------

-- refresh handler
local function refresh(driver, device)
  log.debug("Calling Refresh")

  --check if we've heard from devices lately
  if os.time() > (commonglobals.lastHeardFromESP + 650) then
    commonglobals.handshakeRequired = true
    log.warn("Haven't heard from soil so going into handshake mode.")
  end

  --hand shake if needed
  if commonglobals.handshakeRequired then
    if not myclient.handshakeNow("soil") then
      return false
    end
  end

  --Actually update the fields on the smart app
  if commonglobals.newDataAvailable then
    commonglobals.newDataAvailable = false
    if not emitSoilData(driver, device) then
      return false
    end
  end

  return true
end

-- this is called once a device is added by the cloud and synchronized down to the hub
local function device_added(driver, device)
  log.info("[" .. device.id .. "] Adding new soil device")
end

-- this is called both when a device is added (but after `added`) and after a hub reboots.
local function device_init(driver, device)
  log.info("[" .. device.id .. "] Initializing soil device")

  -- set a default or queried state for each capability attribute
  device:emit_event(soil.Moisture(0))
  device:emit_event(heapfragmentation.Fragmentation(0))
  device:emit_event(firmware.Version("-"))  
  device:emit_event(capabilities.signalStrength.rssi(0))
  device:emit_event(capabilities.signalStrength.rssi(0))
  

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
  log.info("[" .. device.id .. "] Removing soil device")
end

local soil_driver = Driver("soil", {
  discovery = discovery.handle_discovery,
  lifecycle_handlers = {
    added = device_added,
    init = device_init,
    removed = device_removed
  },
  capability_handlers =
  {
    [capabilities.refresh.ID] = {
      [capabilities.refresh.commands.refresh.NAME] = refresh
    }
  }
})

-- run the driver
soil_driver:run()