local socket = require "cosock.socket"
local log = require "log"
local commonglobals = require "_commonglobals"

local CLIENTSOCKTIMEOUT = 2
local serversock

local function init_serversocket()

    local serversock = assert(socket.tcp(), "create TCP socket")
    assert(serversock:bind('*', 0))
    serversock:settimeout(0)
    serversock:listen()

    return serversock

end

local function handle_post(client,line)
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
      log.error("Error on client receive: " .. err)
      return
    end      
    if line:find('-Length:', 1, plaintext) ~= nil then  --get content length
      content_len = line:match '%d+'
      --log.debug ('Len:', content_len)
    end
  end
  
  -- Receive body here 
  content, err = client:receive(tonumber(content_len))
  log.debug ('content:', content)
  if err ~= nil then
    log.error("Error on client receive: " .. err)
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
end

local function handle_get(client,line)

  --Get URL of GET
  local url= line:match '/%a+'

  --epoch request only
  if url == '/epoch' then
    log.debug("Sending epoch back as requested")
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

    --Put ESP in wifi only mode (usually for OTA updates)
    local flag="false"
    if(commonglobals.wifiOnly) then
      flag="true"
    end
    settingsMsg=settingsMsg..[[,"wifionly_flag":]]..flag..[[}]]

    log.debug("Content: "..settingsMsg)
    client:send(settingsMsg)
    client:close()
    return
  end

  --If we got this far, we don't recognize the request
  log.error("Unrecognized GET request from client: "..line)
  client:close()
end

local function watch_socket(_, sock)

  local line, err

  --Waiting for incoming
  local client, accept_err = sock:accept()
  if accept_err ~= nil then
    log.error("Connection accept error: " .. accept_err .. " from " .. client:getpeername())
    sock:close()
    return
  end
  log.debug("Accepted connection from", client:getpeername())

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
    handle_post(client,line)
    return
  end
  if line:find('GET', 1, plaintext) ~= nil then
    handle_get(client,line)
    return
  end

  --If we got this far, we don't recognize the request
  log.error("Unrecognized request from client: "..line)
  client:close()
end

local function start_server(driver)

    -- Startup Server
    serversock = init_serversocket()
    commonglobals.server_ip, commonglobals.server_port = serversock:getsockname()
    log.info(string.format('**************************  Server started at %s:%s', commonglobals.server_ip, commonglobals.server_port))
  
    driver:register_channel_handler(serversock, watch_socket, 'server')
  
end

return {
    start_server = start_server,
    RefreshWeather = RefreshWeather,
    RefreshWind = RefreshWind,
    RefreshRain = RefreshRain,
    RefreshAdmin = RefreshAdmin
  }