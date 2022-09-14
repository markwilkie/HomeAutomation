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
local wifiswitch = capabilities["radioamber53161.wifiOnly"]
local modestate = capabilities["radioamber53161.modeState"] 


-- require custom handlers from driver package
local discovery = require "discovery"


-----------------------------------------------------------------
-- local functions
-----------------------------------------------------------------

function RefreshTest(content)
  log.debug("Refreshing Test Data")
  
  commonglobals.lastHeardFromESP = os.time()
  commonglobals.handshakeRequired = false
  commonglobals.newDataAvailable = true

  --local jsondata = json.decode(content)
  --globals.rssi = jsondata.wifi_strength
  --globals.voltage = tonumber(string.format("%.1f", jsondata.voltage))
  globals.currentTime = os.date("%a %X", 1662559690)
end

local function calcLQI(rssi)
  --Calc LQI
  local MAC_SPEC_ED_MAX = 255
  local ED_RF_POWER_MIN_DBM = -90
  local ED_RF_POWER_MAX_DBM = -5
  local LQI = (MAC_SPEC_ED_MAX * (rssi - ED_RF_POWER_MIN_DBM)) / (ED_RF_POWER_MAX_DBM - ED_RF_POWER_MIN_DBM);
  return LQI
end

-- Get latest test updates
local function emitTestData(driver, device)
  log.info("Emiting Test Data")

  device:emit_event(modestate.WifiOnly("Enabled"))
  device:emit_event(modestate.PowerSaver("no"))
  device:emit_event(modestate.Boost("foo"))
end


-- free_heap":258356,"min_free_heap":254896,"pms_read_time":1662561498,"cpu_reset_code":5,"cpu_reset_reason"

-------------
-- Handlers
----------

-- refresh handler
local function refresh(driver, device)
  log.debug("Calling Refresh")
  if not emitTestData(driver, device) then
    return false
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
  log.info("[" .. device.id .. "] Adding new test device ")
end

-- this is called both when a device is added (but after `added`) and after a hub reboots.
local function device_init(driver, device)
  log.warn("[" .. device.id .. "] ----------------------->  Initializing test device")

  --default values
  device:emit_event(modestate.WifiOnly("Enabled"))
  device:emit_event(modestate.PowerSaver("no"))
  device:emit_event(modestate.Boost("foo"))

  device:emit_event(wifiswitch.Switch.off())

  refresh(driver, device)
end

-- this is called when a device is removed by the cloud and synchronized down to the hub
local function device_removed(driver, device)
  log.info("[" .. device.id .. "] Removing test device")
end


local test_driver = Driver("test", {
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
test_driver:run()