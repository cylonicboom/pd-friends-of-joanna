-- walker driver: wait out the intro, then wander and squeeze off shots.
-- pad 0 is the local profile's pad on every process. the host skips the
-- cutscene (the clients get it mirrored over SVC_CUTSCENE).
--
-- the profile these runs boot with is CONTROLMODE_PC: the LEFT stick looks
-- and the RIGHT stick (rx/ry) walks and strafes. x/y alone just stare at
-- the ceiling.
local t = 0
local A, B, Z = 0x8000, 0x4000, 0x2000
pd.on("tick", function()
  t = t + 1
  local turn, walk, strafe, buttons = 0, 0, 0, 0
  if NETTEST.host and t > 300 and (t % 300) < 3 then
    buttons = buttons | A
  end
  if t > 300 then
    local phase = (t // 120) % 4
    if phase == 0 then walk = 70            -- forward
    elseif phase == 1 then turn = 40        -- look around
    elseif phase == 2 then walk = 70; strafe = 30
    else walk = -40 end                     -- back off
    if (t % 150) < 6 then buttons = buttons | Z end   -- a shot
    if (t % 400) < 3 then buttons = buttons | B end   -- reload / cancel
  end
  pd.pad(0, { x = turn, y = 0, rx = strafe, ry = walk, buttons = buttons })
end)
