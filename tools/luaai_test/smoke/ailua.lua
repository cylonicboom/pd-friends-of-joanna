-- Smoke test for the generated helper library scripts/ai.lua: loads it
-- through dofile, checks the wrapper table matches what the engine exposes,
-- and drives real commands through the wrappers from inside an ailist
-- override so the operand packing is proven against the engine, not the
-- generator. Companion to docs/aicommands.md.
--
-- Run it as scripts/init.lua with scripts/ai.lua beside it (both relative to
-- the game's working directory) and load a stage with chrs (0x1d works). It
-- logs "ailua smoke PASS/FAIL" lines through pd.log and ends with
-- "ailua smoke done N/M".

local results = { pass = 0, total = 0 }
local frames = 0
local done = false
local phase = "load"
local target = nil
local victim = nil          -- chrnum the override first ran for
local applied = false       -- set_shield / set_alertness issued
local yieldret = nil        -- what ai.yield returned

local function report(name, ok, detail)
	results.total = results.total + 1
	if ok then
		results.pass = results.pass + 1
	end
	pd.log(string.format("ailua smoke %s %s%s", ok and "PASS" or "FAIL", name,
		detail and (" (" .. tostring(detail) .. ")") or ""))
end

local ok, ai = pcall(dofile, "scripts/ai.lua")
report("dofile scripts/ai.lua", ok and type(ai) == "table", not ok and ai or nil)
if not ok then
	pd.log(string.format("ailua smoke done %d/%d", results.pass, results.total))
	return
end

local nfn, nop = 0, 0
for k, v in pairs(ai) do
	if type(v) == "function" then nfn = nfn + 1 end
end
for _ in pairs(ai.OPCODE) do nop = nop + 1 end
report("wrapper count matches OPCODE table", nfn == nop and nfn > 400, nfn .. " functions, " .. nop .. " opcodes")
report("fojo opcodes present", ai.OPCODE.detect_aio == 0x0194 and ai.OPCODE.set_coop_playernum == 0x01e4
	and type(ai.if_playernum_is_coop) == "function", "0x0194, 0x01e1-0x01e4")

-- Inside the override: first entry sets shield 2.5 (u16 25 * 0.1) and
-- alertness 200 through the wrappers, then yields through ai.yield. Later
-- entries read the snapshot back and pass the list through.
local function override(ctx)
	local me = ctx:self()
	-- room == -1 means the chr has no prop yet (spawned later by the list).
	-- Vanilla handlers such as aiSetShield dereference chr->prop without a
	-- check, so a prop-less chr is not a safe target for ctx:run. See
	-- docs/luascripting.md, Containment.
	if me and victim == nil and me.room ~= -1 then
		victim = me.chrnum
		local r1 = ai.set_shield(ctx, 25)
		local r2 = ai.set_alertness(ctx, 200)
		applied = (r1 == 0 and r2 == 0)
		yieldret = ai.yield(ctx)
		return 1
	end
	while true do
		local r = ctx:exec(ctx:cur())
		if r ~= 0 then
			return r
		end
	end
end

pd.on("draw", function()
	frames = frames + 1
	if done then
		return
	end
	if pd.player_count() < 1 or pd.player_pos() == nil or frames < 60 then
		return
	end
	if phase == "load" then
		-- A list some chr WITH a prop is running: only those can take the
		-- commands below (see the note in override). Prefer a 04xx list.
		local fallback
		pd.each_chr(function(chrnum, list, off, alert, islua)
			if chrnum >= 0 and islua == 1 then
				local info = pd.chr_info(chrnum)
				if info and info.room ~= -1 then
					if target == nil and list >= 0x0400 and list < 0x0800 then
						target = list
					elseif fallback == nil then
						fallback = list
					end
				end
			end
		end)
		target = target or fallback
		if target == nil then
			report("override target", false, "no chr with a prop running a list on this stage")
			pd.log(string.format("ailua smoke done %d/%d", results.pass, results.total))
			done = true
			return
		end
		pd.register_ailist(target, override)
		phase = "wait"
		return
	end
	if phase == "wait" then
		if victim == nil then
			if frames > 900 then
				report("override ran", false, "never entered")
				pd.log(string.format("ailua smoke done %d/%d", results.pass, results.total))
				done = true
			end
			return
		end
		-- give the engine a frame to apply before reading back
		phase = "check"
		return
	end
	local info = pd.chr_info(victim)
	report("wrappers returned continue", applied, "set_shield, set_alertness")
	report("ai.yield returns the break flag", yieldret == 1, tostring(yieldret))
	report("u16 packing reached the engine", info and math.abs(info.shield - 2.5) < 0.01,
		info and string.format("chr %d shield %.2f", victim, info.shield) or "no chr_info")
	report("u8 packing reached the engine", info and info.alertness >= 190,
		info and ("alertness " .. info.alertness) or "no chr_info")
	pd.log(string.format("ailua smoke done %d/%d", results.pass, results.total))
	done = true
end)

pd.log("ailua smoke loaded")
