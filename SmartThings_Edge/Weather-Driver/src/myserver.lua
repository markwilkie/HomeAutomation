local cosock = require "cosock" 
local socket = require "cosock.socket"
local log = require "log"
local json = require('dkjson')

local globals = require "globals"

local CLIENTSOCKTIMEOUT = 2
local serversock
local server_ip
local server_port
local channelID


local function init_serversocket()

    local serversock = assert(socket.tcp(), "create TCP socket")
    assert(serversock:bind('*', 0))
    serversock:settimeout(0)
    serversock:listen()

    return serversock

end

local function updateWeather(content)
  local jsondata = json.decode(content);

  globals.testTemp = jsondata.test1

  log.debug("test1: ", jsondata.test1)
  log.debug("test2: ", jsondata.test2)
end

local function watch_socket(_, sock)

  local client, accept_err = sock:accept()

  log.debug("Accepted connection from", client:getpeername())

  if accept_err ~= nil then
    log.info("Connection accept error: " .. accept_err)
    sock:close()
    return
  end

  client:settimeout(CLIENTSOCKTIMEOUT)

  local line, err
  local content
  local content_len = 0
  local url = '/'

  local ip, _, _ = client:getpeername()
  if ip ~= nil then

    do -- Receive all headers until blank line is found
    line, err = client:receive()
    log.debug ('Received:', line)
    if line:find('POST', 1, plaintext) == nil then
      log.warn("No POST....ignoring")
      client:close()
    else
      url= line:match '/%a+'
      print('url:', url)
    end        
    if not err then
      while line ~= "" do
        line, err  = client:receive()
        log.debug ('Received:', line)
        if err ~= nil then
          log.warn("Error on client receive: " .. err)
          return
        end      
        if line:find('-Length:', 1, plaintext) ~= nil then  --get content length
          content_len = line:match '%d+'
          log.debug ('Len:', content_len)
        end
      end
      
      -- Receive body here 
      content, err = client:receive(tonumber(content_len))
      log.debug ('content:', content)
      if err ~= nil then
        log.warn("Error on client receive: " .. err)
        return
      end
    end
  end
  else
    log.warn("Could not get IP from getpeername()")
  end
      
  OK_MSG = 'HTTP/1.1 200 OK\r\n\r\n'
  client:send(OK_MSG)
  client:close()

  --if weather
  if url == '/weather' then
    updateWeather(content)
  end
end

local function start_server(driver)

    -- Startup Server
    serversock = init_serversocket()
    server_ip, server_port = serversock:getsockname()
    log.info(string.format('**************************  Server started at %s:%s', server_ip, server_port))
  
    channelID = driver:register_channel_handler(serversock, watch_socket, 'server')
  
end

return {
    start_server = start_server
  }