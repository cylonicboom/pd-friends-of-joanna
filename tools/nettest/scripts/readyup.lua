-- ready-up driver: the host skips cutscenes; every client readies in the hub
-- a few seconds after it arrives (each time). a --dedicated host never readies.
local HUB = 0x26
local A = 0x8000
local frames, readied = 0, false
pd.on("tick", function()
  local st = pd.stage()
  frames = frames + 1
  local buttons = 0
  if st ~= HUB and NETTEST.host and frames > 300 and (frames % 300) < 3 then
    buttons = buttons | A
  end
  pd.pad(0, { x = 0, y = 0, rx = 0, ry = 0, buttons = buttons })
  if not NETTEST.host and st == HUB and not readied and frames > 60 * 3 then
    readied = true
    pd.console("ready")
  end
end)
