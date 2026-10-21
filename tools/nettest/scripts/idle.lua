-- idle driver: the host skips the intro cutscene, nobody moves.
local t = 0
local A = 0x8000
pd.on("tick", function()
  t = t + 1
  local buttons = 0
  if NETTEST.host and t > 300 and (t % 300) < 3 then
    buttons = A
  end
  pd.pad(0, { x = 0, y = 0, buttons = buttons })
end)
