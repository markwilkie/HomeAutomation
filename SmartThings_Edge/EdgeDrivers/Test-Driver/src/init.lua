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
local wifiswitch = capabilities["radioamber53161.wifiswitch"]
local firmware = capabilities["radioamber53161.firmware"]
local heapfragmentation = capabilities["radioamber53161.heapFragmentation"]
local cpureset = capabilities["radioamber53161.cpuReset"]
local lastairupdate = capabilities["radioamber53161.lastAirUpdate"]
local airquality = capabilities["radioamber53161.airQuality"]

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

 
  device:emit_event(airquality.AQI(102))
  device:emit_event(airquality.Description("Moderate".." -"..os.date("%a %X", 1662559690)))

  device:emit_event(cpureset.Code(5))
  device:emit_event(cpureset.Reason("cpu restarted"))

  device:emit_event(firmware.Version("2.4.4"))
  device:emit_event(heapfragmentation.Fragmentation(9.2))
  local rssi=-67
  device:emit_event(capabilities.signalStrength.rssi(rssi))
  device:emit_event(capabilities.signalStrength.lqi(calcLQI(rssi)))

  device:emit_event(datetime.datetime(os.date("%a %X", 1662559690)))



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
local function switch_on(driver, device, command)
  log.info("Setting test switch ON")
  commonglobals.wifiOnly = true
  device:emit_event(capabilities.switch.on())
end

-- callback to handle an `off` capability command
local function switch_off(driver, device, command)
  log.info("Setting Wifi Only Mode OFF")
  commonglobals.wifiOnly = false
  device:emit_event(capabilities.switch.switch.off())
end

-- callback to handle an `on` capability command
local function wifiswitch_on(driver, device, command)
  log.info("Setting wifi test switch ON")
  commonglobals.wifiOnly = true
  device:emit_event(wifiswitch.switch.on())
end

-- callback to handle an `off` capability command
local function wifiswitch_off(driver, device, command)
  log.info("Setting wifi test switch OFF")
  commonglobals.wifiOnly = false
  device:emit_event(wifiswitch.switch.off())
end

-- this is called once a device is added by the cloud and synchronized down to the hub
local function device_added(driver, device)
  log.info("[" .. device.id .. "] Adding new test device")
end

-- this is called both when a device is added (but after `added`) and after a hub reboots.
local function device_init(driver, device)
  log.warn("[" .. device.id .. "] ----------------------->  Initializing test device")

  --default values
  device:emit_event(airquality.AQI(102))
  device:emit_event(airquality.Description("Moderate".." -"..os.date("%a %X", 1662559690)))

  device:emit_event(wifiswitch.switch.off())
  device:emit_event(firmware.Version("2.4.3"))

  device:emit_event(heapfragmentation.Fragmentation(8.2))
  device:emit_event(capabilities.signalStrength.rssi(0))
  device:emit_event(capabilities.signalStrength.lqi(0))

  device:emit_event(cpureset.Code(-1))
  device:emit_event(cpureset.Reason("cpu restarted"))  

  

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
    [capabilities.switch.ID] = {
      [capabilities.switch.commands.on.NAME] = switch_on,
      [capabilities.switch.commands.off.NAME] = switch_off,
    },
    [wifiswitch.ID] = {
      [wifiswitch.commands.on.NAME] = wifiswitch_on,
      [wifiswitch.commands.off.NAME] = wifiswitch_off,
    }
  }
})

-- run the driver
test_driver:run()