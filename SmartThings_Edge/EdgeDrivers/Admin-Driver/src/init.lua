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
local datetime = capabilities["radioamber53161.datetime"]

-- require custom handlers from driver package
local discovery = require "discovery"


-----------------------------------------------------------------
-- local functions
-----------------------------------------------------------------

function RefreshAdmin(content)
  log.info("Refreshing Admin Data")
  
  commonglobals.lastHeardFromESP = os.time()
  commonglobals.handshakeRequired = false
  commonglobals.newDataAvailable = true

  local jsondata = json.decode(content)
  globals.rssi = jsondata.wifi_strength
  globals.voltage = jsondata.voltage
  globals.currentTime = os.date("%a %X", jsondata.current_time)
end

-- Get latest admin updates
local function emitAdminData(driver, device)
  log.info(string.format("[%s] Emiting Admin Data", device.device_network_id))

  device:emit_event(capabilities.signalStrength.rssi(globals.rssi))
  device:emit_event(capabilities.voltageMeasurement.voltage(globals.voltage))
  device:emit_event(datetime.datetime(globals.currentTime))

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
    log.warn("Haven't heard from admin so going into handshake mode.")
  end

  --hand shake if needed
  if commonglobals.handshakeRequired then
    if not myclient.handshakeNow("admin") then
      return false
    end
  end

  --Actually update the fields on the smart app
  if commonglobals.newDataAvailable then
    commonglobals.newDataAvailable = false
    if not emitAdminData(driver, device) then
      return false
    end
  end

  return true
end

-- callback to handle an `on` capability command
local function switch_on(driver, device, command)
  log.debug(string.format("[%s] calling wifi only (on) v5.1", device.device_network_id))
  commonglobals.wifiOnly = true
  device:emit_event(capabilities.switch.switch.on())
end

-- callback to handle an `off` capability command
local function switch_off(driver, device, command)
  log.debug(string.format("[%s] calling wifi only (off) v5.1", device.device_network_id))
  commonglobals.wifiOnly = false
  device:emit_event(capabilities.switch.switch.off())
end

-- this is called once a device is added by the cloud and synchronized down to the hub
local function device_added(driver, device)
  log.info("[" .. device.id .. "] Adding new admin device")

  -- set a default or queried state for each capability attribute
  device:emit_event(capabilities.switch.switch.off())
  device:emit_event(capabilities.voltageMeasurement.voltage(0))
  device:emit_event(capabilities.signalStrength.rssi(0))
  device:emit_event(capabilities.signalStrength.lqi(0))
end

-- this is called both when a device is added (but after `added`) and after a hub reboots.
local function device_init(driver, device)
  log.info("[" .. device.id .. "] Initializing admin device")

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
  log.info("[" .. device.id .. "] Removing admin device")
end

local admin_driver = Driver("admin", {
  discovery = discovery.handle_discovery,
  lifecycle_handlers = {
    added = device_added,
    init = device_init,
    removed = device_removed
  },
  capability_handlers = 
  {
    -- Refresh command handler
    [capabilities.refresh.ID] = {
      [capabilities.refresh.commands.refresh.NAME] = refresh
    },
    [capabilities.switch.ID] = {
      [capabilities.switch.commands.on.NAME] = switch_on,
      [capabilities.switch.commands.off.NAME] = switch_off,
    }
  }
})

-- run the driver
admin_driver:run()