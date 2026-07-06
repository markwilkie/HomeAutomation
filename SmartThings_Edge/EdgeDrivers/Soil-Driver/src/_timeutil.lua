local function secondsToFriendly(seconds)
  if seconds < 60 then
    return "Just now"
  end

  local minutes = math.floor(seconds / 60)
  if minutes < 60 then
    return string.format("%dm ago", minutes)
  end

  local hours = math.floor(minutes / 60)
  minutes = minutes % 60
  if hours < 24 then
    return string.format("%dh %dm ago", hours, minutes)
  end

  local days = math.floor(hours / 24)
  hours = hours % 24
  return string.format("%dd %dh ago", days, hours)
end

return {
  secondsToFriendly = secondsToFriendly
}
