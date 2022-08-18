local log = require "log"

local histData = {}

local function dump(t)
    for k, v in pairs(t) do
      log.debug("key: ",k," values: ",v.epoch, v.temperature, v.pressure)
    end
end

-- remove entries older than 12 hours
local function pruneData()
  local currentEpoch=os.time()-(7*60*60)
  local cutTime=currentEpoch-(12*60*60)

  for k, v in pairs(histData) do
    if v.epoch < cutTime then
      table.remove(histData,k)
    end
  end
end

local function insertData(epoch,temperature,pressure)
  pruneData()
  table.insert(histData,{epoch=epoch, temperature=temperature, pressure=pressure})
  dump(histData)
end

local function findMaxTemperature()
  local epoch=histData[1].epoch
  local maxTemperature=histData[1].temperature
  for k, v in pairs(histData) do
    if v.temperature > maxTemperature then
      maxTemperature = v.temperature
      epoch = v.epoch
    end
  end
  return epoch,maxTemperature
end

local function findMinTemperature()
  local epoch=histData[1].epoch
  local minTemperature=histData[1].temperature
  for k, v in pairs(histData) do
    if v.temperature < minTemperature then
      minTemperature = v.temperature
      epoch = v.epoch
    end
  end
  return epoch,minTemperature
end

--temp last n hours
local function temperatureHistory(hoursAgo)
  local currentEpoch=os.time()-(7*60*60)
  local hourAgo=currentEpoch-(hoursAgo*60*60)

  local temperature=histData[1].temperature
  for k, v in ipairs(histData) do  --ipairs gaurantees it's in index order
    if v.epoch > hourAgo then
      break  --will return the last one right before this one
    end
    temperature = v.temperature
  end
  return temperature
end

--pressure last n hours
local function pressureHistory(hoursAgo)
  local currentEpoch=os.time()-(7*60*60)
  local hourAgo=currentEpoch-(hoursAgo*60*60)

  local pressure=histData[1].pressure   
  for k, v in ipairs(histData) do  --ipairs gaurantees it's in index order
    if v.epoch > hourAgo then
      break  --will return the last one right before this one
    end
    pressure = v.pressure
  end
  return pressure
end

 return {
    insertData = insertData,
    findMaxTemperature = findMaxTemperature,
    findMinTemperature = findMinTemperature,
    temperatureHistory = temperatureHistory,
    pressureHistory = pressureHistory
  }