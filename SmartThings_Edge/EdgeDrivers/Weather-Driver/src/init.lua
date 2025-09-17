-- require st provided libraries
local capabilities = require "st.capabilities"
local Driver = require "st.driver"
local log = require "log"
local json = require('dkjson')

-- require custom handlers from driver package
local discovery = require "discovery"

local commonglobals = require "_commonglobals"
local myserver = require "_myserver"
local myclient = require "_myclient"
local weatherdatastore = require "weatherdatastore"
local globals = require "globals"

-- Custom Capabiities
local atmospressure = capabilities["radioamber53161.atmospressure"]
local airquality = capabilities["radioamber53161.airQuality"]
local lastupdated = capabilities["radioamber53161.lastUpdated"]



-----------------------------------------------------------------
-- local functions
-----------------------------------------------------------------

function RefreshWeather(content)
  log.debug("Refreshing Weather Data")
  
  commonglobals.lastHeardFromESP = os.time()
  commonglobals.handshakeRequired = false
  commonglobals.newDataAvailable = true

  local jsondata = json.decode(content);

  --Set read time if we have a new one
  if jsondata.pms_read_time ~= globals.lastpmsreadtime and jsondata.pms_read_time > 0 then
    globals.lastpmsreadtime = jsondata.pms_read_time
  end

  --Ok, now fill the rest of the globals
  globals.currentTime = os.date("%H:%M", jsondata.current_time)
  globals.temperature = tonumber(string.format("%.1f", jsondata.temperature))
  globals.heatindex = tonumber(string.format("%.1f", jsondata.heat_index))
  globals.humidity = tonumber(string.format("%.1f", jsondata.humidity))
  globals.pressure = tonumber(string.format("%.1f", jsondata.pressure))
  globals.dewPoint = tonumber(string.format("%.1f", jsondata.dew_point))
  globals.uvIndex = tonumber(string.format("%.1f", jsondata.uv))
  globals.uv1Index = tonumber(string.format("%.1f", jsondata.uv1))
  globals.uv2Index = tonumber(string.format("%.1f", jsondata.uv2))
  globals.uv3Index = tonumber(string.format("%.1f", jsondata.uv3))
  globals.ldr = jsondata.ldr
  globals.pm25 = jsondata.pm25AQI
  globals.pm25AQI = jsondata.pm25AQI
  globals.pm25Label = jsondata.pm25Label
  globals.pmslastupdate = os.date("%H:%M", jsondata.pms_read_time)

  --adding to the data store so we can do historical calc
  weatherdatastore.insertData(jsondata.current_time,globals.temperature,globals.pressure)

  --do historical calc  (e.g. last hour change)
  globals.temperatureChangeLastHour = tonumber(string.format("%.1f",globals.temperature - weatherdatastore.temperatureHistory(1)))
  globals.pressureChangeLastHour = tonumber(string.format("%.1f",globals.pressure - weatherdatastore.pressureHistory(1)))
end

-- Get latest weather updates
local function emitWeatherData(driver, device)
  log.info("Emiting Weather Data")

  device:emit_event(capabilities.temperatureMeasurement.temperature({value = globals.temperature, unit = 'F'}))
  device:emit_event(capabilities.relativeHumidityMeasurement.humidity(globals.humidity))
  device:emit_event(atmospressure.pressure(globals.pressure))
  device:emit_event(capabilities.ultravioletIndex.ultravioletIndex(globals.uvIndex))
  device:emit_event(capabilities.illuminanceMeasurement.illuminance(globals.ldr))
  device:emit_event(capabilities.dewPoint.dewpoint({value = globals.dewPoint, unit = 'C'}))
  device:emit_event(lastupdated.Time(globals.currentTime))

  device:emit_component_event(device.profile.components['uvmorning'],capabilities.ultravioletIndex.ultravioletIndex(globals.uv3Index))
  device:emit_component_event(device.profile.components['uvmidday'],capabilities.ultravioletIndex.ultravioletIndex(globals.uv1Index))
  device:emit_component_event(device.profile.components['uvevening'],capabilities.ultravioletIndex.ultravioletIndex(globals.uv2Index))

  device:emit_component_event(device.profile.components['airQuality'],airquality.AQI(globals.pm25AQI))
  device:emit_component_event(device.profile.components['airQuality'],airquality.Designation(globals.pm25Label))
  device:emit_component_event(device.profile.components['airQuality'],lastupdated.Time(globals.pmslastupdate))
  device:emit_component_event(device.profile.components['heatIndex'],capabilities.temperatureMeasurement.temperature({value = globals.heatindex, unit = 'C'}))
  device:emit_component_event(device.profile.components['lastHour'],capabilities.temperatureMeasurement.temperature({value = globals.temperatureChangeLastHour, unit = 'F'}))
  device:emit_component_event(device.profile.components['lastHour'],atmospressure.pressure(globals.pressureChangeLastHour))
  return true
end

-------------
-- Handlers
----------

-- refresh handler
local function refresh(driver, device)
  log.info("Calling Refresh")

  --check if we've heard from devices lately
  if os.time() > (commonglobals.lastHeardFromESP + 650) then
    commonglobals.handshakeRequired = true
    log.warn("Haven't heard from Weather so going into handshake mode")
  end

  --hand shake if needed
  if commonglobals.handshakeRequired then
    if not myclient.handshakeNow("weather") then
      return false
    end
  end

  --Actually update the fields on the smart app
  if commonglobals.newDataAvailable then
    commonglobals.newDataAvailable = false
    if not emitWeatherData(driver, device) then
      return false
    end
  end

  return tre
end

-- this is called once a device is added by the cloud and synchronized down to the hub
local function device_added(driver, device)
  log.info("[" .. device.id .. "] Adding new weather device")
end

-- this is called both when a device is added (but after `added`) and after a hub reboots.
local function device_init(driver, device)
  log.info("[" .. device.id .. "] Initializing weather device")

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
  log.info("[" .. device.id .. "] Removing weather device")
end

-- create the driver object
local weather_driver = Driver("weather", {
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
weather_driver:run()