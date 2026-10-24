-- lobby loop driver: the host skips the intro and ends the first mission
-- ~10 s into gameplay with the server console; back in the hub the client
-- readies up (pd.console) and the ready-up starts the mission again. a
-- --dedicated host never readies.
--
-- init.lua reloads with every stage, so the mission count lives in a
-- session-only persist key ('~': memory, never the file).
local HUB = 0x26
local A = 0x8000
local frames, counted, ended, readied = 0, false, false, false
local function missions() return tonumber(pd.persist_get("~nt_missions") or "0") end
pd.on("tick", function()
  local st = pd.stage()
  if not counted then
    counted = true
    if st ~= HUB then pd.persist_set("~nt_missions", tostring(missions() + 1)) end
  end
  frames = frames + 1
  local buttons = 0
  if st ~= HUB and NETTEST.host and frames > 300 and (frames % 300) < 3 then
    buttons = buttons | A -- the host skips the cutscene; clients follow SVC_CUTSCENE
  end
  pd.pad(0, { x = 0, y = 0, rx = 0, ry = 0, buttons = buttons })
  if NETTEST.host and st ~= HUB and missions() == 1 and not ended and frames > 60 * 40 then
    ended = true
    pd.console("endmission")
  end
  if not NETTEST.host and st == HUB and missions() >= 1 and not readied and frames > 60 * 3 then
    readied = true
    pd.console("ready")
  end
end)
