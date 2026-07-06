--- MiniSplit Matter Bridge custom driver.
---
--- SmartThings' built-in MATTER_GENERIC fingerprinting only recognizes a fixed
--- catalog of endpoint-type combinations (e.g. "matter/hvac/thermostat/humidity").
--- This device's shape -- one Thermostat endpoint, TWO TemperatureMeasurement
--- endpoints (indoor + outdoor), one RelativeHumidityMeasurement endpoint, and
--- one OnOff+LevelControl endpoint repurposed for compressor load -- doesn't
--- match any of those, so the generic composer silently drops everything
--- except the first thermostat+humidity match it finds. This driver defines
--- the composition explicitly instead of relying on that fallback.
---
--- Endpoint layout (fixed by the MiniSplit ESP32 firmware, src/matter_device.cpp):
---   1 = Thermostat   (OnOff + Thermostat clusters)      -> component "main"
---   2 = Temperature  (indoor, mirrors Tuya/BME280)      -> component "indoorTemp"
---   3 = Humidity     (BME280, or idle if not present)   -> component "humidity"
---   4 = Temperature  (outdoor, from Tuya "ure" DP)       -> component "outdoorTemp"
---   5 = OnOff+Level  (compressor running / load %)       -> component "compressor"
---   6 = Level        (rolling 8h average load %)         -> component "compressor8h"
---   7 = Level        (rolling 24h average load %)        -> component "compressor24h"
---   8 = Level        (cycles in trailing 60min, RAW count, not %) -> component "compressorCycles"
---   9 = Level        (max cycles/hr over 24h, RAW count, not %)   -> component "compressorCyclesMax24h"
---  10 = Temperature  (minutes in current on/off state, RAW, not x100C) -> component "compressorStateTime"

local capabilities = require "st.capabilities"
local clusters = require "st.matter.clusters"
local MatterDriver = require "st.matter.driver"
local log = require "log"
local st_utils = require "st.utils"

-- Custom capability (radioamber53161.loadPercent) -- switchLevel's "Dimmer"
-- labeling didn't fit a percent-load reading and had no history graph either,
-- so this reports load as its own "percent" attribute with a plain "Load" card.
local cap_load_percent = capabilities["radioamber53161.loadPercent"]

-- Custom capabilities for compressor cycling stats. Both are raw counts, not
-- percentages -- endpoints 8/9 still use LevelControl for transport (same
-- cluster as the load-percent endpoints), so the level handler below has to
-- branch by endpoint to know whether to treat a report as a % or a raw count.
local cap_cycle_count = capabilities["radioamber53161.cycleCount"]

-- Minutes in current on/off state. Endpoint 10 repurposes TemperatureMeasurement
-- for its wide int16 range, so the temperature handler below also has to
-- branch by endpoint to know whether to divide by 100 (real temperature) or not.
local cap_duration = capabilities["radioamber53161.duration"]

local CYCLES_ENDPOINTS = { [8] = true, [9] = true }
local DURATION_ENDPOINT = 10

local ENDPOINT_TO_COMPONENT = {
  [1] = "main",
  [2] = "indoorTemp",
  [3] = "humidity",
  [4] = "outdoorTemp",
  [5] = "compressor",
  [6] = "compressor8h",
  [7] = "compressor24h",
  [8] = "compressorCycles",
  [9] = "compressorCyclesMax24h",
  [10] = "compressorStateTime",
}

local COMPONENT_TO_ENDPOINT = {
  main = 1,
  indoorTemp = 2,
  humidity = 3,
  outdoorTemp = 4,
  compressor = 5,
  compressor8h = 6,
  compressor24h = 7,
  compressorCycles = 8,
  compressorCyclesMax24h = 9,
  compressorStateTime = 10,
}

local function endpoint_to_component(device, endpoint_id)
  return ENDPOINT_TO_COMPONENT[endpoint_id] or "main"
end

local function component_to_endpoint(device, component_id)
  return COMPONENT_TO_ENDPOINT[component_id] or 1
end

-- Tuya "mode" DP only ever produces these five Matter SystemMode values
-- (see MiniSplit/src/main.c map_tuya_mode_to_matter). "Off" is deliberately
-- not in this table -- it's handled via the "switch" capability on "main"
-- instead, matching how the firmware routes SystemMode=kOff through
-- tuya_set_power() rather than the mode DP.
local SYSTEM_MODE_MAP = {
  [clusters.Thermostat.types.ThermostatSystemMode.OFF] = capabilities.thermostatMode.thermostatMode.off,
  [clusters.Thermostat.types.ThermostatSystemMode.AUTO] = capabilities.thermostatMode.thermostatMode.auto,
  [clusters.Thermostat.types.ThermostatSystemMode.COOL] = capabilities.thermostatMode.thermostatMode.cool,
  [clusters.Thermostat.types.ThermostatSystemMode.HEAT] = capabilities.thermostatMode.thermostatMode.heat,
  [clusters.Thermostat.types.ThermostatSystemMode.FAN_ONLY] = capabilities.thermostatMode.thermostatMode.fanonly,
  [clusters.Thermostat.types.ThermostatSystemMode.DRY] = capabilities.thermostatMode.thermostatMode.dryair,
}

--- Attribute handlers (device -> SmartThings) -------------------------------

local function temperature_handler(driver, device, ib, response)
  if ib.data.value == nil then
    return
  end
  -- Endpoint 10 repurposes this same cluster for a raw (unscaled) minutes
  -- count, not an x100 Celsius reading.
  if ib.endpoint_id == DURATION_ENDPOINT then
    device:emit_event_for_endpoint(ib.endpoint_id, cap_duration.minutes({ value = ib.data.value, unit = "min" }))
    return
  end
  device:emit_event_for_endpoint(ib.endpoint_id, capabilities.temperatureMeasurement.temperature({ value = ib.data.value / 100.0, unit = "C" }))
end

local function humidity_handler(driver, device, ib, response)
  if ib.data.value == nil then
    return
  end
  device:emit_event_for_endpoint(ib.endpoint_id, capabilities.relativeHumidityMeasurement.humidity(st_utils.round(ib.data.value / 100.0)))
end

local function system_mode_handler(driver, device, ib, response)
  local mode = SYSTEM_MODE_MAP[ib.data.value]
  if mode then
    device:emit_event_for_endpoint(ib.endpoint_id, mode())
  else
    log.warn(string.format("Unmapped Thermostat SystemMode value: %s", tostring(ib.data.value)))
  end
end

local function heating_setpoint_handler(driver, device, ib, response)
  if ib.data.value == nil then
    return
  end
  device:emit_event_for_endpoint(ib.endpoint_id, capabilities.thermostatHeatingSetpoint.heatingSetpoint({ value = ib.data.value / 100.0, unit = "C" }))
end

local function cooling_setpoint_handler(driver, device, ib, response)
  if ib.data.value == nil then
    return
  end
  device:emit_event_for_endpoint(ib.endpoint_id, capabilities.thermostatCoolingSetpoint.coolingSetpoint({ value = ib.data.value / 100.0, unit = "C" }))
end

local function on_off_handler(driver, device, ib, response)
  -- Endpoints 6/7 (avg8h/avg24h) also physically have an OnOff cluster (same
  -- dimmable_light-repurposing pattern as endpoint 5), but their components
  -- only declare loadPercent, not switch -- skip them to avoid a "does not
  -- support capability Switch" warning on every report.
  if ib.endpoint_id ~= 1 and ib.endpoint_id ~= 5 then
    return
  end
  if ib.data.value then
    device:emit_event_for_endpoint(ib.endpoint_id, capabilities.switch.switch.on())
  else
    device:emit_event_for_endpoint(ib.endpoint_id, capabilities.switch.switch.off())
  end
end

local function level_handler(driver, device, ib, response)
  if ib.data.value == nil then
    return
  end
  -- Endpoints 8/9 carry raw cycle counts over this same cluster, not a 0-254
  -- scaled percentage.
  if CYCLES_ENDPOINTS[ib.endpoint_id] then
    device:emit_event_for_endpoint(ib.endpoint_id, cap_cycle_count.count({ value = ib.data.value, unit = "cycles" }))
    return
  end
  -- Shared by endpoints 5 (current load), 6 (8h avg), 7 (24h avg).
  local level = st_utils.round(ib.data.value / 254.0 * 100)
  device:emit_event_for_endpoint(ib.endpoint_id, cap_load_percent.percent({ value = level, unit = "%" }))
end

--- Capability handlers (SmartThings -> device) ------------------------------
--- Only "main" (real power + mode + setpoints) is actually controllable.
--- "compressor" only ever reports (see profile) -- no handlers registered
--- for it, so taps on its switch/level UI are safely no-ops.

local function handle_switch_on(driver, device, cmd)
  device:send(clusters.OnOff.server.commands.On(device, component_to_endpoint(device, cmd.component)))
end

local function handle_switch_off(driver, device, cmd)
  device:send(clusters.OnOff.server.commands.Off(device, component_to_endpoint(device, cmd.component)))
end

local function thermostat_mode_command_factory(system_mode)
  return function(driver, device, cmd)
    device:send(clusters.Thermostat.attributes.SystemMode:write(device, component_to_endpoint(device, cmd.component), system_mode))
  end
end

local function handle_set_heating_setpoint(driver, device, cmd)
  local value = st_utils.round(cmd.args.setpoint * 100.0)
  device:send(clusters.Thermostat.attributes.OccupiedHeatingSetpoint:write(device, component_to_endpoint(device, cmd.component), value))
end

local function handle_set_cooling_setpoint(driver, device, cmd)
  local value = st_utils.round(cmd.args.setpoint * 100.0)
  device:send(clusters.Thermostat.attributes.OccupiedCoolingSetpoint:write(device, component_to_endpoint(device, cmd.component), value))
end

--- Lifecycle ------------------------------------------------------------

local function device_init(driver, device)
  device:set_endpoint_to_component_fn(endpoint_to_component)
  device:set_component_to_endpoint_fn(component_to_endpoint)
  device:subscribe()
end

--- Driver definition ------------------------------------------------------

local matter_driver_template = {
  lifecycle_handlers = {
    init = device_init,
  },
  matter_handlers = {
    attr = {
      [clusters.Thermostat.ID] = {
        [clusters.Thermostat.attributes.LocalTemperature.ID] = temperature_handler,
        [clusters.Thermostat.attributes.OutdoorTemperature.ID] = temperature_handler,
        [clusters.Thermostat.attributes.SystemMode.ID] = system_mode_handler,
        [clusters.Thermostat.attributes.OccupiedHeatingSetpoint.ID] = heating_setpoint_handler,
        [clusters.Thermostat.attributes.OccupiedCoolingSetpoint.ID] = cooling_setpoint_handler,
      },
      [clusters.TemperatureMeasurement.ID] = {
        -- Shared by endpoints 2 (indoor) and 4 (outdoor); ib.endpoint_id
        -- routes the event to the correct component either way.
        [clusters.TemperatureMeasurement.attributes.MeasuredValue.ID] = temperature_handler,
      },
      [clusters.RelativeHumidityMeasurement.ID] = {
        [clusters.RelativeHumidityMeasurement.attributes.MeasuredValue.ID] = humidity_handler,
      },
      [clusters.OnOff.ID] = {
        -- Shared by endpoints 1 (main power) and 5 (compressor running).
        [clusters.OnOff.attributes.OnOff.ID] = on_off_handler,
      },
      [clusters.LevelControl.ID] = {
        [clusters.LevelControl.attributes.CurrentLevel.ID] = level_handler,
      },
    },
  },
  capability_handlers = {
    [capabilities.switch.ID] = {
      [capabilities.switch.commands.on.NAME] = handle_switch_on,
      [capabilities.switch.commands.off.NAME] = handle_switch_off,
    },
    [capabilities.thermostatMode.ID] = {
      -- Only base modes are real invokable commands on this capability.
      -- "fanonly"/"dryair" exist as reportable mode VALUES (see SYSTEM_MODE_MAP
      -- above, used for attribute reporting) but not as settable commands --
      -- referencing capabilities.thermostatMode.commands.fanonly crashed the
      -- driver on init (nil field) since no such command exists.
      [capabilities.thermostatMode.commands.auto.NAME] = thermostat_mode_command_factory(clusters.Thermostat.types.ThermostatSystemMode.AUTO),
      [capabilities.thermostatMode.commands.cool.NAME] = thermostat_mode_command_factory(clusters.Thermostat.types.ThermostatSystemMode.COOL),
      [capabilities.thermostatMode.commands.heat.NAME] = thermostat_mode_command_factory(clusters.Thermostat.types.ThermostatSystemMode.HEAT),
    },
    [capabilities.thermostatHeatingSetpoint.ID] = {
      [capabilities.thermostatHeatingSetpoint.commands.setHeatingSetpoint.NAME] = handle_set_heating_setpoint,
    },
    [capabilities.thermostatCoolingSetpoint.ID] = {
      [capabilities.thermostatCoolingSetpoint.commands.setCoolingSetpoint.NAME] = handle_set_cooling_setpoint,
    },
  },
  subscribed_attributes = {
    [capabilities.switch.ID] = {
      clusters.OnOff.attributes.OnOff,
    },
    [capabilities.thermostatMode.ID] = {
      clusters.Thermostat.attributes.SystemMode,
    },
    [capabilities.thermostatHeatingSetpoint.ID] = {
      clusters.Thermostat.attributes.OccupiedHeatingSetpoint,
    },
    [capabilities.thermostatCoolingSetpoint.ID] = {
      clusters.Thermostat.attributes.OccupiedCoolingSetpoint,
    },
    [capabilities.temperatureMeasurement.ID] = {
      clusters.Thermostat.attributes.LocalTemperature,
      clusters.Thermostat.attributes.OutdoorTemperature,
      clusters.TemperatureMeasurement.attributes.MeasuredValue,
    },
    [capabilities.relativeHumidityMeasurement.ID] = {
      clusters.RelativeHumidityMeasurement.attributes.MeasuredValue,
    },
    [cap_load_percent.ID] = {
      clusters.LevelControl.attributes.CurrentLevel,
    },
    [cap_cycle_count.ID] = {
      clusters.LevelControl.attributes.CurrentLevel,
    },
    [cap_duration.ID] = {
      clusters.TemperatureMeasurement.attributes.MeasuredValue,
    },
  },
  supported_capabilities = {
    capabilities.switch,
    capabilities.thermostatMode,
    capabilities.thermostatHeatingSetpoint,
    capabilities.thermostatCoolingSetpoint,
    capabilities.temperatureMeasurement,
    capabilities.relativeHumidityMeasurement,
    cap_load_percent,
    cap_cycle_count,
    cap_duration,
    capabilities.refresh,
  },
}

local matter_driver = MatterDriver("minisplit-matter", matter_driver_template)
matter_driver:run()
