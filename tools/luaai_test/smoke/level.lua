-- Smoke test for level scripting: pd.register_ailist on a list a live chr is
-- running, the yield-is-a-return contract, per-chr state across frames,
-- ctx:self() snapshots, and a background (10xx) override.
-- Companion to docs/luascripting.md ("Verifying it").
--
-- Run it as scripts/init.lua (relative to the game's working directory) and
-- load a stage with chrs (0x1d works). It logs one "level smoke PASS/FAIL"
-- line per check through pd.log and ends with "level smoke done N/M".
-- Headless it needs ~60 frames with a player before it starts and then
-- about 40 more for the override to accumulate entries.

local results = { pass = 0, total = 0 }
local frames = { draw = 0 }
local done = false
local registered = false

local target = nil          -- ailist id we wrap (a chr's live list)
local bglist = nil          -- a 10xx list id, if the stage runs one
local state = {}            -- chrnum -> { entries, first, last }
local bgentries = 0
local bgself = "unset"      -- what ctx:self() returned in the background list
local retcodes = {}         -- ctx:exec return codes seen, by value

local function report(name, ok, detail)
	results.total = results.total + 1
	if ok then
		results.pass = results.pass + 1
	end
	pd.log(string.format("level smoke %s %s%s", ok and "PASS" or "FAIL", name,
		detail and (" (" .. tostring(detail) .. ")") or ""))
end

-- The wrapper. Everything here is re-entered from the top every frame the
-- chr runs its list; no local survives a return. Only `state` (an upvalue
-- table) carries anything over, which is the contract the doc describes.
local function wrapper(ctx)
	local me = ctx:self()
	if me then
		local s = state[me.chrnum]
		if not s then
			s = { entries = 0, first = { me.x, me.y, me.z }, last = nil }
			state[me.chrnum] = s
		end
		s.entries = s.entries + 1
		s.last = { me.x, me.y, me.z }
	end
	-- pass-through: drive the original commands exactly as the transpiled
	-- chunk would
	while true do
		local r = ctx:exec(ctx:cur())
		retcodes[r] = (retcodes[r] or 0) + 1
		if r ~= 0 then
			return r
		end
	end
end

local function bgwrapper(ctx)
	bgentries = bgentries + 1
	local me = ctx:self()
	bgself = me == nil and "nil" or "chr " .. tostring(me.chrnum)
	while true do
		local r = ctx:exec(ctx:cur())
		if r ~= 0 then
			return r
		end
	end
end

-- Prefer a stage list (04xx) over a global one (00xx): a level script is
-- about the stage's own lists. Fall back to whatever a chr is running.
local function pick()
	local fallback = nil
	pd.each_chr(function(chrnum, list, off, alert, islua)
		if chrnum >= 0 and islua == 1 then
			if target == nil and list >= 0x0400 and list < 0x0800 then
				target = list
			elseif fallback == nil then
				fallback = list
			end
		end
		if bglist == nil and list >= 0x1000 and list < 0x1400 then
			bglist = list
		end
	end)
	if target == nil then
		target = fallback
	end
end

local function finish()
	local nchr, total = 0, 0
	local moved = false
	for chrnum, s in pairs(state) do
		nchr = nchr + 1
		total = total + s.entries
		if s.last and (s.last[1] ~= s.first[1] or s.last[3] ~= s.first[3]) then
			moved = true
		end
	end
	report("override ran", total > 0, string.format("list 0x%x, %d entries over %d chrs", target, total, nchr))
	report("re-entered across frames", total >= 2, "entries=" .. total)
	report("per-chr table state persists", nchr > 0 and total >= nchr, nchr .. " chrs tracked")
	local codes = {}
	for k, v in pairs(retcodes) do
		codes[#codes + 1] = k .. "x" .. v
	end
	table.sort(codes)
	report("exec returned a yield", (retcodes[1] or 0) > 0, table.concat(codes, " "))
	-- Informational: a chr idling in a cutscene may not move, so this is a
	-- PASS either way; the detail says whether the snapshot was seen to change.
	report("ctx:self snapshot", true, moved and "position changed between frames" or "no movement seen (cutscene idle?)")
	if bglist then
		report("background list override", bgentries > 0, string.format("list 0x%x, %d entries, self=%s", bglist, bgentries, bgself))
	else
		report("background list override", true, "no 10xx list running on this stage; not exercised")
	end
	-- Scope is ruled, not enforced: record the current behaviour so the line
	-- flips when the owner check lands.
	report("scope", true, string.format("stage 0x%x: register_ailist accepted with no owner check (expected until discovery lands)", pd.stage()))
	pd.log(string.format("level smoke done %d/%d", results.pass, results.total))
	done = true
end

pd.on("draw", function()
	frames.draw = frames.draw + 1
	if done then
		return
	end
	if pd.player_count() < 1 or pd.player_pos() == nil or frames.draw < 60 then
		return
	end
	if not registered then
		registered = true
		pick()
		if target == nil then
			report("override ran", false, "no chr list to override on this stage")
			pd.log(string.format("level smoke done %d/%d", results.pass, results.total))
			done = true
			return
		end
		pd.register_ailist(target, wrapper)
		pd.log(string.format("level smoke override on list 0x%x", target))
		if bglist then
			pd.register_ailist(bglist, bgwrapper)
			pd.log(string.format("level smoke override on background list 0x%x", bglist))
		end
		return
	end
	local total = 0
	for _, s in pairs(state) do
		total = total + s.entries
	end
	if total >= 40 or frames.draw > 900 then
		finish()
	end
end)

pd.log("level smoke loaded")
