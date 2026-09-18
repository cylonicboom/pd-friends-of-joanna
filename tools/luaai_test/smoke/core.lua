-- Smoke test for the core pd.* functions (src/game/luaai_api.c).
-- The pd.* API it exercises is ported from Kai (be46717).
--
-- Run it as scripts/init.lua (relative to the game's working directory) and
-- load a stage. Once a player is live it calls every core function once from
-- the "draw" handler, logs a PASS or FAIL line for each through pd.log, and
-- ends with "core smoke done N/M". It also checks that event handlers are
-- isolated (one that errors, one that loops forever) and that ctx is
-- read-only, through a pass-through ailist override.

local results = { pass = 0, total = 0 }
local frames = { tick = 0, draw = 0 }
local done = false
local ctxcheck = nil -- set by the override: true, or a failure string

local function report(name, ok, detail)
	results.total = results.total + 1
	if ok then
		results.pass = results.pass + 1
	end
	pd.log(string.format("core smoke %s %s%s", ok and "PASS" or "FAIL", name,
		detail and (" (" .. tostring(detail) .. ")") or ""))
end

local function isnum(...)
	for i = 1, select("#", ...) do
		if type((select(i, ...))) ~= "number" then
			return false
		end
	end
	return select("#", ...) > 0
end

local function check(name, fn)
	local ok, res, detail = pcall(fn)
	if not ok then
		report(name, false, res)
	else
		report(name, res, detail)
	end
end

-- Handler isolation: the first handler raises, the second spins. Both must be
-- stopped and logged, and the counting handler after them must still run.
local erroredonce, spunonce = false, false
pd.on("tick", function()
	if not erroredonce then
		erroredonce = true
		error("deliberate error from the smoke test")
	end
end)
pd.on("tick", function()
	if not spunonce and frames.tick > 5 then
		spunonce = true
		while true do end
	end
end)
pd.on("tick", function() frames.tick = frames.tick + 1 end)

local events = {}
for _, ev in ipairs({ "weaponfire", "chrfire", "punch", "alert", "kill", "damage",
		"headshot", "spawn", "roomenter", "missioncomplete", "firingrange",
		"weaponfound", "weaponpickup", "objective", "cheatunlock", "challengecomplete" }) do
	pd.on(ev, function() events[ev] = (events[ev] or 0) + 1 end)
end

local overridden = nil
local ranall = false

local function passthrough(ctx)
	if ctxcheck == nil then
		local wrote = pcall(function() ctx.exec = function() return 1 end end)
		local mt = getmetatable(ctx)
		if wrote then
			ctxcheck = "ctx.exec could be replaced"
		elseif mt ~= "ctx" then
			ctxcheck = "getmetatable(ctx) is " .. tostring(mt)
		elseif type(ctx.cur) ~= "function" or type(ctx.exec) ~= "function" then
			ctxcheck = "ctx methods missing"
		else
			ctxcheck = true
		end
	end
	-- Run the list exactly as its transpiled chunk would.
	while true do
		local r = ctx:exec(ctx:cur())
		if r ~= 0 then
			return r
		end
	end
end

local function runall()
	check("on", function() return type(pd.on) == "function" and frames.tick > 0, frames.tick .. " ticks" end)
	check("draw_box", function() pd.draw_box(10, 10, 60, 20, 0x00ff0080, 5) return true end)
	check("draw_text", function() pd.draw_text(14, 14, "core smoke", 0xffffffff, 5) return true end)
	check("text_size", function()
		local w, h = pd.text_size("core smoke")
		return isnum(w, h) and w > 0 and h > 0, w .. "x" .. h
	end)
	check("text high bytes", function()
		-- the font is ASCII: other bytes are drawn and measured as '?'
		pd.draw_text(14, 30, "caf\195\169 \226\128\148 \1\127\195", 0xffffffff, 5)
		local w = pd.text_size("caf\195\169\195")
		local w2 = pd.text_size("caf???")
		return w == w2, w .. " vs " .. w2
	end)
	check("each_chr", function()
		local n = 0
		pd.each_chr(function(chrnum, list, off, alert, islua)
			n = n + 1
			if overridden == nil and chrnum >= 0 and islua == 1 then
				overridden = list
			end
		end)
		return n > 0, n .. " rows"
	end)
	check("octree_stats", function() return pd.octree_stats() == nil end)
	check("dlcache_stats", function() return pd.dlcache_stats() == nil end)
	check("perf", function()
		local p = pd.perf()
		return type(p) == "table" and isnum(p.fps, p.frame_ms, p.vtx_used, p.vtx_total)
			and type(p.show_fps) == "boolean" and p.octree == nil,
			string.format("fps %.1f vtx %d/%d", p.fps, p.vtx_used, p.vtx_total)
	end)
	local chrs = pd.all_chrs()
	check("all_chrs", function()
		local n = 0
		pd.all_chrs(function() n = n + 1 end)
		return type(chrs) == "table" and #chrs > 0 and n == #chrs, #chrs .. " chrs"
	end)
	local c = chrs[1]
	check("chr_info", function()
		local t = pd.chr_info(c)
		return type(t) == "table" and t.chrnum == c and isnum(t.x, t.health), "chr " .. tostring(c)
	end)
	check("chr_pos", function() return isnum(pd.chr_pos(c)) and pd.chr_pos(-5) == nil end)
	check("chr_health", function() return isnum(pd.chr_health(c)) end)
	check("player_pos", function()
		return isnum(pd.player_pos()) and pd.player_pos(99) == nil
	end)
	check("player_count", function() local n = pd.player_count() return n >= 1, n end)
	check("stage", function() local s = pd.stage() return isnum(s), string.format("0x%x", s) end)
	check("distance", function() return math.abs(pd.distance(0, 0, 0, 3, 4, 0) - 5) < 1e-6 end)
	check("lvupdate", function() return isnum(pd.lvupdate()), pd.lvupdate() end)
	check("mission_complete", function() return pd.mission_complete() == false end)
	check("load_serial", function() local s = pd.load_serial() return isnum(s) and s >= 1, s end)
	check("persist_set too long", function()
		-- the settings file is read back a line at a time into char[2048], so
		-- a longer entry would come back split into a truncated value and a
		-- junk second key. pd.persist_set refuses it and returns false; the
		-- key must be left untouched, not half-written.
		local huge = string.rep("x", 4096)
		local refused = pd.persist_set("core_smoke_huge", huge) == false
		local edge = pd.persist_set("core_smoke_edge", string.rep("y", 2000)) == true
		return refused and pd.persist_get("core_smoke_huge") == nil and edge
			and #pd.persist_get("core_smoke_edge") == 2000
	end)
	check("persist_set delimiters", function()
		-- the store file is one key=value per line, so a value with a line
		-- break in it used to read back as a forged second entry, and an '='
		-- in a key moved the split. both are refused now; an '=' in a value
		-- is still fine, because the reader splits on the first one only.
		local nl = pd.persist_set("core_smoke_nl", "x\ny=forged") == false
		local cr = pd.persist_set("core_smoke_cr", "x\ry=forged") == false
		local nul = pd.persist_set("core_smoke_nul", "x\0y") == false
		local eqkey = pd.persist_set("core_smoke=eq", "v") == false
		local eqval = pd.persist_set("core_smoke_eqval", "a=b=c") == true
		return nl and cr and nul and eqkey and eqval
			and pd.persist_get("core_smoke_nl") == nil
			and pd.persist_get("core_smoke_cr") == nil
			and pd.persist_get("core_smoke_nul") == nil
			and pd.persist_get("core_smoke=eq") == nil
			and pd.persist_get("core_smoke_eqval") == "a=b=c",
			string.format("nl=%s cr=%s nul=%s eqkey=%s eqval=%s", tostring(nl),
				tostring(cr), tostring(nul), tostring(eqkey), tostring(eqval))
	end)
	check("persist_set", function()
		pd.persist_set("core_smoke", "ok=1")
		pd.persist_set("~core_smoke_session", "yes")
		return true
	end)
	check("persist_get", function()
		return pd.persist_get("core_smoke") == "ok=1"
			and pd.persist_get("~core_smoke_session") == "yes"
			and pd.persist_get("core_smoke_missing") == nil
	end)
	check("ap_mode", function()
		local before = pd.ap_mode()
		local on = pd.ap_mode(true)
		local off = pd.ap_mode(false)
		return before == false and on == true and off == false
	end)
	check("unlock", function() pd.unlock("weapon_pri", 7) pd.unlock(0, 300) return true end)
	check("is_unlocked", function()
		return pd.is_unlocked("weapon_pri", 7) and not pd.is_unlocked("weapon_pri", 8)
			and not pd.is_unlocked(0, 300) and not pd.is_unlocked("nonsense", 7)
	end)
	check("lock", function() pd.lock(2, 7) return not pd.is_unlocked("weapon_pri", 7) end)
	check("ap_reset", function()
		pd.unlock("stage", 3)
		pd.ap_reset()
		return not pd.is_unlocked("stage", 3)
	end)
	check("ap_list_header", function()
		local a = pd.ap_list_header("Smoke 1/2")
		local b = pd.ap_list_header(nil)
		return a == "Smoke 1/2" and b == ""
	end)
	check("input_source", function() local s = pd.input_source() return s == "pad" or s == "kbm", s end)
	local nchr, nmisc = pd.bio_count()
	check("bio_count", function() return isnum(nchr, nmisc), nchr .. " chr, " .. nmisc .. " misc" end)
	check("bio_text", function()
		if pd.bio_text(0, -1) ~= nil or pd.bio_text(1, 9999) ~= nil then
			return false, "out of range slot not nil"
		end
		if nchr > 0 then
			local name, body = pd.bio_text(0, 0)
			if type(name) ~= "string" or type(body) ~= "string" then
				return false, "chr bio 0 missing"
			end
		end
		if nmisc > 0 then
			local name, body = pd.bio_text(1, 0)
			if type(name) ~= "string" or type(body) ~= "string" then
				return false, "misc bio 0 missing"
			end
		end
		return true
	end)
	check("ext_poll", function() return pd.ext_poll() == nil end)
	check("objective_status", function()
		local s = pd.objective_status(0)
		return isnum(s) and s >= -1 and s <= 2 and pd.objective_status(99) == -1, s
	end)
	check("buttons", function() return isnum(pd.buttons()) end)
	check("buttons_pressed", function() return isnum(pd.buttons_pressed()) end)
	check("chr_slots", function()
		local free, total = pd.chr_slots()
		return isnum(free, total) and total > 0 and free <= total, free .. "/" .. total
	end)
	check("player_name", function() local n = pd.player_name() return type(n) == "string", n end)
	check("room_count", function() local n = pd.room_count() return isnum(n) and n > 1, n end)
	check("aim_chr", function() local a = pd.aim_chr() return a == nil or isnum(a), a end)
	check("aim_screen", function()
		local x, y = pd.aim_screen()
		return isnum(x, y), x and string.format("%.0f,%.0f", x, y)
	end)
	check("aim_bounds", function() return isnum(pd.aim_bounds()) end)
	check("ap_connect", function() return pd.ap_connect("localhost", 1) == nil end)
	check("ap_status", function() return pd.ap_status() == nil end)
	check("ap_send", function() return pd.ap_send("{}") == nil end)
	check("ap_poll", function() return pd.ap_poll() == nil end)
	check("ap_disconnect", function() return pd.ap_disconnect() == nil end)
	check("handler isolation", function()
		return erroredonce and spunonce and frames.tick > 10, frames.tick .. " ticks"
	end)
end

pd.on("draw", function()
	frames.draw = frames.draw + 1
	if done then
		return
	end
	if pd.player_count() < 1 or pd.player_pos() == nil or frames.draw < 60 then
		return
	end
	if not ranall then
		ranall = true
		runall()
		if overridden ~= nil then
			pd.register_ailist(overridden, passthrough)
			pd.log(string.format("core smoke override on list 0x%x", overridden))
		end
		return
	end
	if overridden == nil then
		-- a stage with no chrs (a Combat Simulator arena) has no list to override
		report("ctx read-only", false, "no chr list to override on this stage")
		done = true
		return
	end
	if ctxcheck == nil then
		if frames.draw > 600 then
			report("ctx read-only", false, "override never ran")
			done = true
		end
		return
	end
	report("ctx read-only", ctxcheck == true, ctxcheck ~= true and ctxcheck or nil)
	local seen = {}
	for k, v in pairs(events) do
		seen[#seen + 1] = k .. "=" .. v
	end
	table.sort(seen)
	pd.log("core smoke events: " .. (#seen > 0 and table.concat(seen, " ") or "none"))
	pd.log(string.format("core smoke done %d/%d", results.pass, results.total))
	done = true
end)

pd.log("core smoke loaded")
