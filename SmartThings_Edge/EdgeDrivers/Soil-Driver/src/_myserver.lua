local cosock = require "cosock"
local socket = cosock.socket
local log = require "log"
local commonglobals = require "_commonglobals"

--local init = require "init"

local CLIENTSOCKTIMEOUT = 2

local function handle_post(driver,device,client,line)
  local err
  local content
  local content_len = 0
  local url = '/'

  --Make sure it's a POST and grab the url
  url= line:match '/%a+'

  --Read through the rest of the header
  while line ~= "" do
    line, err  = client:receive()
    --log.debug ('Received:', line)
    if err ~= nil then
      log.error(string.format("Error on client receive: %s",err))
      return
    end      
    if line:find('-Length:', 1, plaintext) ~= nil then  --get content length
      content_len = line:match '%d+'
      --log.debug ('Len:', content_len)
    end
  end
  
  -- Receive body here 
  content, err = client:receive(tonumber(content_len))
  log.info ('content:', content)
  if err ~= nil then
    log.error(string.format("Error on client receive: %s",err))
    return
  end
      
  OK_MSG = 'HTTP/1.1 200 OK\r\n\r\n'
  client:send(OK_MSG)
  client:close()

  --refresh based on url
  if url == '/admin' then
    RefreshAdmin(content)
  end
  if url == '/weather' then
    RefreshWeather(content)
  end
  if url == '/wind' then
    RefreshWind(content)
  end
  if url == '/rain' then
    RefreshRain(content)
  end
  if url == '/soilgw' then
    RefreshSoilGW(device,content)
  end   
  if url == '/soil' then
    RefreshSoil(device,content)
  end 
end

local function handle_get(driver,device,client,line)

  --Get URL of GET
  local url= line:match '/%a+'

  --epoch request only
  if url == '/epoch' then
    log.info("Sending epoch back as requested")
    local epochMsg = [[HTTP/1.1 200 OK

{"epoch":]]..os.time()-(7*60*60)..[[}]]
    client:send(epochMsg)
    client:close()
    return
  end

  --full settings request
  if url == '/settings' then
    log.debug("Sending settings back as requested")
    local settingsMsg = [[HTTP/1.1 200 OK

{"epoch":]]..os.time()-(7*60*60)

    if(adminPort>0) then
      settingsMsg=settingsMsg..[[,"adminPort":]]..adminPort
    end
    if(weatherPort>0) then
      settingsMsg=settingsMsg..[[,"weatherPort":]]..weatherPort
    end
    if(windPort>0) then
      settingsMsg=settingsMsg..[[,"windPort":]]..windPort
    end
    if(rainPort>0) then
      settingsMsg=settingsMsg..[[,"rainPort":]]..rainPort
    end
    if(soilPort>0) then
      settingsMsg=settingsMsg..[[,"soilPort":]]..soilPort
    end    

    --Put ESP in wifi only mode (usually for OTA updates)
    local flag="false"
    if(commonglobals.wifiOnly) then
      flag="true"
    end
    settingsMsg=settingsMsg..[[,"wifionly_flag":]]..flag..[[}]]

    log.info(string.format("Content: %s",settingsMsg))
    client:send(settingsMsg)
    client:close()
    return
  end

    --register device, and return port
    if url == '/registerDevice' then
      local param,value=line:match("?(%a+)=(%d+)")  --grab id that was passed in
      local port=commonglobals[value]
      if port == nil then
        log.info(string.format("Now registering id %s",value))
        port=AddNewDevice(driver,value)
        if port == nil then
          log.warn(string.format("No port registered for %s",value))
          return
        end
        commonglobals.device_ports[value]=port
      end

      local portMsg = [[HTTP/1.1 200 OK
  
  {"port":]]..port..[[}]]
      
      log.info(string.format("Content: %s",portMsg))
      client:send(portMsg)
      client:close()
      return
    end

  --If we got this far, we don't recognize the request
  log.error(string.format("Unrecognized GET request from client: %s",line))
  client:close()
end

local function handleIncoming(driver,device,client)

  local line, err

  --Set timeout
  client:settimeout(CLIENTSOCKTIMEOUT)

  --get peer name (ip of client)
  local ip, _, _ = client:getpeername()
  if ip == nil then
    log.error("Could not get IP from getpeername()")
    client:close()
    return
  end

  --Receiving from client
  line, err = client:receive()
  if err ~= nil then
    log.error("Error on client receive: " .. err)
    client:close()
    return
  end

  --Make sure it's a POST or GET and grab the url
  log.debug ('Received:', line)
  if line:find('POST', 1, plaintext) ~= nil then
    handle_post(driver,device,client,line)
    return
  end
  if line:find('GET', 1, plaintext) ~= nil then
    handle_get(driver,device,client,line)
    return
  end

  --If we got this far, we don't recognize the request
  log.error("Unrecognized request from client: "..line)
  client:close()
end

local function start_server(driver,device)

    -- Startup Server
    local serversock = socket.tcp()
    serversock:bind('*', 0)
    serversock:listen()

    commonglobals.server_ip, commonglobals.server_port = serversock:getsockname()

    if device == nil then
      log.info(string.format('**************************  Server started at %s:%s', commonglobals.server_ip, commonglobals.server_port))
    else
      log.info(string.format('**************************  Server started for %s at %s:%s', device.label,commonglobals.server_ip, commonglobals.server_port))
      log.info(string.format("Saving port %d for device_network_id: %s",commonglobals.server_port,device.device_network_id))
      device_ports[device.device_network_id]=commonglobals.server_port
    end

    --Spawn thread to accept incoming connections
    cosock.spawn(function()
      while true do
        local client = serversock:accept()
        log.debug(string.format("Accepted connection from", client:getpeername()))
        handleIncoming(driver,device,client)
      end
    end,"server loop")
    
end

return {
    start_server = start_server,
    RefreshWeather = RefreshWeather,
    RefreshWind = RefreshWind,
    RefreshRain = RefreshRain,
    RefreshAdmin = RefreshAdmin,
    RefreshSoil = RefreshSoil,
    RefreshSoilGW = RefreshSoilGW
  }