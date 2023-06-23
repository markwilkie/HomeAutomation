-- require st provided libraries
local capabilities = require "st.capabilities"
local Driver = require "st.driver"
local log = require "log"
local json = require('dkjson')

local commonglobals = require "_commonglobals"
local myserver = require "_myserver"
local myclient = require "_myclient"
local globals = require "globals"

-- Custom Capabiities
local lastupdated = capabilities["radioamber53161.lastUpdated"]
local voltage = capabilities["radioamber53161.voltage"]
local firmware = capabilities["radioamber53161.firmware"]
local heapfragmentation = capabilities["radioamber53161.heapFragmentation"]
local soil = capabilities["radioamber53161.soil"]

-- require custom handlers from driver package
local discovery = require "discovery"

-----------------------------------------------------------------
-- local functions
-----------------------------------------------------------------

function RefreshSoil(device,content)
  log.debug("Refreshing Soil Sensor Data")

  --decode
  local jsondata = json.decode(content)

  device:emit_event(soil.Moisture(jsondata.soil_moisture))
  device:emit_event(capabilities.signalStrength.rssi(jsondata.wifi_strength))
  device:emit_event(voltage.VCC(jsondata.vcc_voltage))
  device:emit_event(firmware.Version(jsondata.firmware_version))
  device:emit_event(heapfragmentation.Fragmentation(jsondata.heap_frag))
  device:emit_event(lastupdated.Time(os.date("%a %X", jsondata.current_time)))
end

function RefreshSoilGW(device,content)
  log.debug("Refreshing Soil GW Data")

  --decode
  local jsondata = json.decode(content)

  device:emit_event(lastupdated.Time(os.date("%a %X", jsondata.current_time)))
end

-------------
-- Handlers
----------

function AddNewDevice(driver,id)

  local metadata = {
    type = "LAN",
    -- the DNI must be unique across your hub, using static ID here so that we
    -- only ever have a single instance of this "device"
    device_network_id = id,
    label = "Soil Device #"..id,
    profile = "soil.v1",
    manufacturer = "Wilkie",
    model = "v1",
    vendor_provided_label = nil
  }

  -- tell the cloud to create a new device record, will get synced back down
  -- and `device_added` and `device_init` callbacks will be called
  driver:try_create_device(metadata)

  --dump current device list  (properties are from the metadata passed into the create device)
  local device_list = driver:get_devices()
  for _, deviceInstance in ipairs(device_list) do
    local currentDeviceId=deviceInstance.device_network_id
    log.debug(string.format("Devices we know about: ID: %s Port: %s Label: %s Network ID: %s",deviceInstance.id,commonglobals.device_ports[currentDeviceId],deviceInstance.label,currentDeviceId))   
  end    

  if commonglobals.device_ports[id] == nil then
    log.warn(string.format("No port saved for device id: %s",id))
  end

  --return port
  return commonglobals.device_ports[id]
end

-- refresh handler
local function refresh(driver, device)
  --if we're not the gateway, then just return
  if string.find(device.label, "Soil Device #") then   -- meaning we're hearing from the gateway
    return true
  end

  if os.time() > (commonglobals.lastHeardFromESP + 1000) then
    commonglobals.handshakeRequired = true
    log.warn("Haven't heard from soil GW for a while so going into handshake mode.")
  end

  --hand shake if needed
  if commonglobals.handshakeRequired then
    if not myclient.handshakeNow("soil",device) then
      return false
    end
  end

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
  

  -- Startup Server
  myserver.start_server(driver,device)  

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