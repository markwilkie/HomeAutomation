local cosock = require "cosock"
local http = cosock.asyncify "socket.http"
local ltn12 = require('ltn12')
local log = require "log"

local commonglobals = require "_commonglobals"

-- dump tables
local function dump(o)
    if type(o) == 'table' then
       local s = '{ '
       for k,v in pairs(o) do
          if type(k) ~= 'number' then k = '"'..k..'"' end
          s = s .. '['..k..'] = ' .. dump(v) .. ','
       end
       return s .. '} '
    else
       return tostring(o)
    end
  end

-- Send LAN HTTP Request
local function send_lan_command(url, method, path, body)
    local dest_url = url..'/'..path
    local res_body = {}
    local code
  
    log.debug(string.format("Calling URL (v1.3): [%s]", dest_url))
  
    -- HTTP Request
    if(method=="POST") then
      _, code = http.request({
        method=method,
        url=dest_url,
        sink=ltn12.sink.table(res_body),
        headers={
          ['Content-Type'] = 'application/x-www-urlencoded',
          ["Content-Length"] = body:len()
        },
        source = ltn12.source.string(body),
      })
    end
  
    if(method=="GET") then
      _, code = http.request({
        method=method,
        url=dest_url,
        sink=ltn12.sink.table(res_body),
        headers={
          ['Content-Type'] = 'application/x-www-urlencoded'
        }
      })
    end
    log.debug(string.format("Returned HTTP Code: [%s]", code))
    log.debug(dump(res_body))
  
    -- Handle response
    if code == 200 or code == 201 then
      return true, res_body
    end
    return false, nil
  end
  
  -- handshake post
  local function handshakeNow(deviceName,device)

    local currentEpoch=os.time()-(7*60*60)
    local network_id=0
    local content
    if device ~= nil then
      network_id = device.device_network_id
      content = [[ {"network_id":"]]..network_id..[[","epoch":]]..currentEpoch..[[,"deviceName":"]]..deviceName..[[","hubAddress":"]]..commonglobals.server_ip..[[","hubPort":]]..device_ports[device.device_network_id]..[[ } ]]  
    else
      content = [[ {"network_id":"]]..network_id..[[","epoch":]]..currentEpoch..[[,"deviceName":"]]..deviceName..[[","hubAddress":"]]..commonglobals.server_ip..[[","hubPort":]]..commonglobals.server_port..[[ } ]]
    end

    local nodeIP = 'http://192.168.15.143:80'
    if deviceName == 'soil' then
      nodeIP = 'http://192.168.15.168:80'
    end

    log.debug(string.format("About to Handshake with %s containing %s",nodeIP,content))
  
    local success, data = send_lan_command(
      nodeIP,
      'POST',
      'handshake',
      content)
  
      if(success) then
        log.info(string.format("Successfuly handshook"))
        commonglobals.handshakeRequired=false;
      else
        log.error("Handshaking for "..deviceName.." NOT successful");
        return false
      end
  
      commonglobals.lastHeardFromESP=os.time()
      return true
  end

  return {
    handshakeNow = handshakeNow
  }