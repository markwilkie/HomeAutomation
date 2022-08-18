local log = require "log"

local histData = {}

local function dump(t)
    for k, v in pairs(t) do
      log.debug("key: ",k," values: ",v.time, v.rainrate)
    end
end

-- remove entries older than 12 hours
local function pruneData()
  local currentEpoch=os.time()-(7*60*60)
  local cutTime=currentEpoch-(12*60*60)

  for k, v in pairs(histData) do
    if v.time < cutTime then
      table.remove(histData,k)
    end
  end
end

local function insertData(time,rainrate)
  pruneData()
  table.insert(histData,{time=time, rainrate=rainrate})
  dump(histData)
end

local function findLastHourTotal(hoursAgo)
  local currentEpoch=os.time()-(7*60*60)
  local hourAgo=currentEpoch-(hoursAgo*60*60)

  local totalrain=0
  local totalentries=0

  for k, v in ipairs(histData) do  --ipairs gaurantees it's in index order
    if v.epoch > hourAgo then
      break  --will return the last one right before this one
    end
    totalentries=totalentries+1
    totalrain=totalrain+v.rainrate
  end
  return totalrain*(12/totalentries)   --use ratio to get to a 12 hour approx as each entry is in/hour
end

local function findLast12HoursTotal()
  local totalrain=0
  local totalentries=0
  for k, v in pairs(histData) do
    totalentries=totalentries+1
    totalrain=totalrain+v.rainrate
  end
  return totalrain*(12/totalentries)   --use ratio to get to a 12 hour approx as each entry is in/hour
end

 return {
    insertData = insertData,
    findLastHourTotal = findLastHourTotal,
    findLast12HoursTotal = findLast12HoursTotal
  }