local log = require "log"

local histData = {}

local function dump(t)
    for k, v in pairs(t) do
      log.debug("key: ",k," values: ",v.gusttime, v.gustspeed, v.gustdirection)
    end
end

-- remove entries older than 12 hours
local function pruneData()
  local currentEpoch=os.time()-(7*60*60)
  local cutTime=currentEpoch-(12*60*60)

  for k, v in pairs(histData) do
    if v.gusttime < cutTime then
      table.remove(histData,k)
    end
  end
end

local function insertData(gusttime,gustspeed,gustdirection)
  pruneData()
  table.insert(histData,{gusttime=gusttime, gustspeed=gustspeed, gustdirection=gustdirection})
  dump(histData)
end

local function findMaxGust()
  local gusttime=histData[1].gusttime
  local gustspeed=histData[1].gustspeed
  local gustdirection=histData[1].gustdirection

  for k, v in pairs(histData) do
    if v.gustspeed > gustspeed then
      gustspeed = v.gustspeed
      gusttime = v.gusttime
      gustdirection = v.gustdirection
    end
  end
  return gusttime, gustspeed, gustdirection
end

 return {
    insertData = insertData,
    findMaxGust = findMaxGust
  }