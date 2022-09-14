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
local modestate = capabilities["radioamber53161.modeState"]
local firmware = capabilities["radioamber53161.firmware"]
local cpureset = capabilities["radioamber53161.cpuReset"]
local heapfragmentation = capabilities["radioamber53161.heapFragmentation"]
local wifiswitch = capabilities["radioamber53161.wifiOnly"]

-- require custom handlers from driver package
local discovery = require "discovery"


-----------------------------------------------------------------
-- local functions
-----------------------------------------------------------------

function RefreshAdmin(content)
  log.debug("Refreshing Admin Data")
  
  commonglobals.lastHeardFromESP = os.time()
  commonglobals.handshakeRequired = false
  commonglobals.newDataAvailable = true

  local jsondata = json.decode(content)
  globals.rssi = jsondata.wifi_strength
  globals.vcc_voltage = jsondata.vcc_voltage
  globals.cap_voltage = jsondata.cap_voltage
  globals.firmware_version = jsondata.firmware_version
  globals.cpu_reset_code = jsondata.cpu_reset_code  
  globals.cpu_reset_reason = jsondata.cpu_reset_reason
  globals.heap_fragmentation = jsondata.heap_frag
  globals.currentTime = os.date("%a %X", jsondata.current_time)

  globals.wifiOnlyState = "Off"
  if jsondata.wifi_only then
    globals.wifiOnlyState = "On"
  end
  globals.boostModeState = "Off"
  if jsondata.boost_mode then
    globals.boostModeState = "On"
  end
  globals.powerSaverModeState = "Off"
  if jsondata.power_saver_mode then
    globals.powerSaverModeState = "On"
  end  

end

-- Get latest admin updates
local function emitAdminData(driver, device)
  log.info("Emiting Admin Data")

  device:emit_event(capabilities.signalStrength.rssi(globals.rssi))
  device:emit_event(modestate.WifiOnly(globals.wifiOnlyState))
  device:emit_event(modestate.PowerSaver(globals.powerSaverModeState))
  device:emit_event(modestate.Boost(globals.boostModeState))  
  device:emit_event(voltage.VCC(globals.vcc_voltage))
  device:emit_event(voltage.Capacitors(globals.cap_voltage))
  device:emit_event(cpureset.Code(globals.cpu_reset_code))
  device:emit_event(cpureset.Reason(globals.cpu_reset_reason))
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
local function wifiswitch_on(driver, device, command)
  log.info("Setting wifi test switch ON")
  commonglobals.wifiOnly = true
  device:emit_event(wifiswitch.Switch.on())
end

-- callback to handle an `off` capability command
local function wifiswitch_off(driver, device, command)
  log.info("Setting wifi test switch OFF")
  commonglobals.wifiOnly = false
  device:emit_event(wifiswitch.Switch.off())
end

-- this is called once a device is added by the cloud and synchronized down to the hub
local function device_added(driver, device)
  log.info("[" .. device.id .. "] Adding new admin device")
end

-- this is called both when a device is added (but after `added`) and after a hub reboots.
local function device_init(driver, device)
  log.info("[" .. device.id .. "] Initializing admin device")

  -- set a default or queried state for each capability attribute
  device:emit_event(modestate.WifiOnly("-"))
  device:emit_event(modestate.PowerSaver("-"))
  device:emit_event(modestate.Boost("-"))  
  device:emit_event(wifiswitch.Switch.off())
  device:emit_event(voltage.VCC(0))
  device:emit_event(voltage.Capacitors(0))
  device:emit_event(cpureset.Code(0))
  device:emit_event(cpureset.Reason("na"))
  device:emit_event(heapfragmentation.Fragmentation(0))
  device:emit_event(firmware.Version("-"))  
  device:emit_event(capabilities.signalStrength.rssi(0))
  device:emit_event(capabilities.signalStrength.lqi(0))
  

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
    [capabilities.refresh.ID] = {
      [capabilities.refresh.commands.refresh.NAME] = refresh
    },
    [wifiswitch.ID] = {
      [wifiswitch.commands.on.NAME] = wifiswitch_on,
      [wifiswitch.commands.off.NAME] = wifiswitch_off,
    }
  }
})

-- run the driver
admin_driver:run()