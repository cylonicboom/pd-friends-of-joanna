-- Smoke test for the fx pd.* functions (src/game/luaai_api_fx.c).
-- The pd.* API it exercises is ported from Kai (be46717).
--
-- Run it as scripts/init.lua (relative to the game's working directory) and
-- load a stage. Once a player is live, the "draw" handler walks a list of
-- steps: each step switches one effect on, leaves it on for a few frames so
-- the engine and renderer paths it feeds actually run, switches it off again
-- and logs a PASS or FAIL line through pd.log. The run ends with
-- "fx smoke done N/M".
--
-- Where an effect can be seen from Lua it is checked: pd.fake_crash must stop
-- the sim (pd.lvupdate() reads 0) and release itself, pd.fps_cap must bring
-- pd.perf().fps down and let it back up. The fake-crash freeze is gated off
-- while a cutscene plays, so run it on a stage without an intro (0x26, the
-- CI training level) for that check to pass. pd.load_image and pd.tex_override
-- expect scripts/chaos/images/smoke.png under the base dir and
-- scripts/chaos/images/cwd.png under the working directory (both 16x16).

local results = { pass = 0, total = 0 }
local draws = 0
local done = false

local function report(name, ok, detail)
	results.total = results.total + 1
	if ok then
		results.pass = results.pass + 1
	end
	pd.log(string.format("fx smoke %s %s%s", ok and "PASS" or "FAIL", name,
		detail ~= nil and (" (" .. tostring(detail) .. ")") or ""))
end

local HOLD = 8 -- frames each effect stays on

-- Hostile-float helper. Every pd.* float argument now goes through
-- luaApiNum/luaApiOptNum (src/game/luaai_api_internal.h): a non-finite value
-- is an argument error, not a silent 0, so a script that means it can pcall.
-- refused(fn, ...) is true when the call was refused that way.
local function refused(fn, ...)
	local ok, err = pcall(fn, ...)
	return (not ok) and tostring(err):find("finite") ~= nil
end

local NAN, INF = 0 / 0, 1 / 0

local img = nil
local frozen = 0
local fpscapped = nil

-- A step: on() switches the effect on and returns ok[, detail]; after HOLD
-- frames (or step.hold) off() switches it off and returns ok[, detail]. Both
-- results must be truthy for a PASS. check(), if present, runs every held
-- frame (for effects whose result is only visible later).
local steps = {
	{ "fade", function() return pd.fade(255, 255, 255, 200, 30) == true end,
		function() return pd.fade(0, 0, 0, 0, 1) == true end },
	{ "shake", function() return pd.shake(20) == true end,
		function() return pd.shake(1) == true end },
	{ "screen_tint", function() return pd.screen_tint(200, 150, 60) == true end,
		function() return pd.screen_tint() == true end },
	{ "grayscale", function() return pd.grayscale(true) == true end,
		function() return pd.grayscale(false) == true end },
	{ "flattex", function() return pd.flattex(1) == true and pd.flattex(2) == true end,
		function() return pd.flattex(0) == true end },
	{ "texlabels", function() return pd.texlabels(8) == 0 and type(pd.tex_visible(4)) == "table" end,
		function() return pd.texlabels(0) == 8 end },
	{ "shiny", function() return pd.shiny(2) == true end,
		function() return pd.shiny(0) == true end },
	{ "crt", function() return pd.crt(true) == true end,
		function() return pd.crt(false) == true end },
	{ "screen_fx", function() return pd.screen_fx(16 | 32, true) == true end,
		function() return pd.screen_fx(16 | 32, false) == true end },
	{ "pixelate", function() return pd.pixelate(160, 120, 1001) == true end,
		function() return pd.pixelate() == true end },
	{ "lens", function() return pd.lens(1.4) == true end,
		function() return pd.lens() == true end },
	{ "pirate", function() return pd.pirate(1) == true and pd.pirate(4) == true end,
		function() return pd.pirate() == true end },
	{ "half_mirror", function() return pd.half_mirror(2) == true end,
		function() return pd.half_mirror(0) == true end },
	{ "upside_down", function() return pd.upside_down(true) == true end,
		function() return pd.upside_down(false) == true end },
	{ "double_vision", function() return pd.double_vision(true) == true end,
		function() return pd.double_vision(false) == true end },
	{ "screen_roll", function() return pd.screen_roll(30) == true end,
		function() return pd.screen_roll() == true end,
		check = function(f) pd.screen_roll(30 + f * 10) end },
	{ "vertex_wobble", function() return pd.vertex_wobble(12, 0.03, 0, 4, 1, 200) == true end,
		function() return pd.vertex_wobble() == true end,
		check = function(f) pd.vertex_wobble(12, 0.03, f * 0.2, 4, 1, 200) end },
	{ "hall_of_mirrors", function() return pd.hall_of_mirrors(true) == true end,
		function() return pd.hall_of_mirrors(false) == true end },
	{ "hud_squish", function() return pd.hud_squish(0.425) == true end,
		function() return pd.hud_squish() == true end },
	{ "hud_off", function() return pd.hud_off(true) == true end,
		function() return pd.hud_off(false) == true end },
	{ "hudvd", function() return pd.hudvd(true) == nil end,
		function() return pd.hudvd(false) == nil end },
	{ "terminator", function() return pd.terminator(true) == true end,
		function() return pd.terminator(false) == true end },
	{ "chr_wireframe", function() return pd.chr_wireframe(true) == true end,
		function() return pd.chr_wireframe(false) == true end },
	{ "ipod_ad", function() return pd.ipod_ad(true, 0, 217, 140) == true end,
		function() return pd.ipod_ad(false) == true end },
	{ "paintball", function() return pd.paintball(true) == true end,
		function() return pd.paintball(false) == true end },
	{ "t_pose", function() return pd.t_pose(true) == true end,
		function() return pd.t_pose(false) == true end },
	{ "fov_scale", function() return pd.fov_scale(1.8) == true end,
		function() return pd.fov_scale() == true end },
	{ "aspect_scale", function() return pd.aspect_scale(2) == true end,
		function() return pd.aspect_scale() == true end },
	{ "internal_res", function() return pd.internal_res(120) == true end,
		function() return pd.internal_res() == true end },
	-- Hostile: NaN and inf into every fx float. gfx_screen_roll, the wobble
	-- phase, the lens k and the HUD squish are all read straight by the
	-- renderer, where a NaN poisons a matrix for good (its own clamps are
	-- comparisons, and every comparison against NaN is false).
	{ "fx float bounds", function()
			local all = refused(pd.screen_roll, NAN)
				and refused(pd.screen_roll, INF)
				and refused(pd.vertex_wobble, 12, NAN, 0, 4, 1, 200)
				and refused(pd.vertex_wobble, 12, 0.03, NAN, 4, 1, 200)
				and refused(pd.vertex_wobble, 12, 0.03, 0, 4, 1, INF)
				and refused(pd.lens, NAN)
				and refused(pd.hud_squish, NAN)
				and refused(pd.fade, 255, 255, 255, 200, NAN)
				and refused(pd.fov_scale, NAN)
				and refused(pd.aspect_scale, INF)
				and refused(pd.fake_crash, NAN)
			-- the finite extremes clamp instead, and leave the frame drawable
			local clamped = pd.screen_roll(1e9) == true and pd.lens(1e9) == true
				and pd.hud_squish(1e9) == true
				and pd.vertex_wobble(12, 1e9, 1e9, 4, 1, 1e9) == true
			return all and clamped, string.format("refused=%s clamped=%s", tostring(all), tostring(clamped))
		end,
		function()
			pd.screen_roll() pd.lens() pd.hud_squish() pd.vertex_wobble()
			return true
		end },
	{ "hud_message", function() pd.hud_message("fx smoke: hello") return true end,
		function() pd.hud_message("fx smoke: big banner", 1) return true end },
	-- Hostile: one 200-character unbroken word. textWrap accumulates a word
	-- into a char[32] with no bound, so before the fix this smashed its stack
	-- frame. The bridge soft-breaks the run and textWrap drops any tail that
	-- still does not fit; both must survive a render (HOLD frames) intact.
	{ "hud_message_longword", function()
			pd.hud_message(string.rep("W", 200))
			pd.hud_message(string.rep("M", 200), 1)
			pd.hud_message(string.rep("i", 60) .. " " .. string.rep("W", 120))
			return true
		end,
		function() return true end },
	-- Hostile: the text gags can make a string LONGER than the one the caller
	-- handed down, and hudmsgCreateFromArgs copies it into a char[400] stack
	-- buffer. 120 one-letter words come back from pig latin as ~600 bytes, so
	-- before the fix the copy wrote past the end of that buffer.
	{ "hud_message_transform_overflow", function()
			if pd.piglatin(true) ~= true then return false, "piglatin" end
			pd.hud_message(string.rep("a ", 120))
			pd.hud_message(string.rep("strength ", 40), 1)
			return true
		end,
		function()
			pd.hud_message(string.rep("b ", 150))
			local ok = pd.piglatin(false) == true
			if pd.uwuify(true) == true then
				pd.hud_message(string.rep("run ", 60))
				pd.uwuify(false)
			end
			return ok
		end },
	-- Hostile: 8-bit and control bytes in a long word (the scrub turns these
	-- into '?', which is a word character, so the run must still be broken).
	{ "hud_message_hostile_bytes", function()
			pd.hud_message(string.rep("\xff\x01W", 80))
			pd.hud_message(string.rep("\n", 40) .. string.rep("Z", 100))
			pd.hud_message("")
			return true
		end,
		function() return true end },
	{ "draw_sprite", function() pd.draw_sprite(0x09, 20, 20, 48, 48, 0x000000ff, 1) return true end,
		function() return true end,
		check = function() pd.draw_sprite(0x0a, 80, 20, 32, 32) end },
	{ "list_images", function()
			local t = pd.list_images()
			local seen = {}
			for _, n in ipairs(t) do seen[n] = true end
			return type(t) == "table" and seen.smoke == true and seen.cwd == true,
				table.concat(t, ",")
		end,
		function() return true end },
	{ "load_image", function()
			img = pd.load_image("smoke")
			local cwd = pd.load_image("cwd.png")
			local bad = pd.load_image("../smoke")
			local missing = pd.load_image("no_such_image")
			-- a script names the file, so extImageLoad reads the PNG header
			-- and refuses anything past its dimension/pixel cap before
			-- stbi_load allocates for it (huge.png is 8192x8192)
			local huge = pd.load_image("huge")
			return math.type(img) == "integer" and math.type(cwd) == "integer" and bad == nil
					and missing == nil and huge == nil,
				string.format("smoke=%s cwd=%s bad=%s missing=%s huge=%s", tostring(img), tostring(cwd),
					tostring(bad), tostring(missing), tostring(huge))
		end,
		function() return true end },
	{ "draw_image", function()
			if img == nil then return false, "no image" end
			pd.draw_image(img, 60, 60, 32, 32)
			pd.draw_image(img, 120, 60, 48, 48, 30, 0xff8080ff, 1)
			return true
		end,
		function() return true end,
		check = function(f) if img then pd.draw_image(img, 180, 60, 32, 32, f * 15) end end },
	{ "tex_override", function()
			return pd.tex_override("smoke") == true and pd.tex_override("cwd") == true
				and pd.tex_override("no_such_image") == false and pd.tex_override("smoke") == true
		end,
		function() return pd.tex_override() == true end },
	{ "fps_cap", function() return pd.fps_cap(15) == true end,
		function()
			local capped = fpscapped
			return pd.fps_cap() == true and capped ~= nil and capped < 22, "fps while capped " .. tostring(capped)
		end,
		hold = 200,
		check = function(f) if f == 190 then fpscapped = pd.perf().fps end end },
	{ "fps_uncapped", function() return true end,
		function()
			local fps = pd.perf().fps
			return fpscapped ~= nil and fps > fpscapped, string.format("%.1f after %.1f", fps, fpscapped or -1)
		end,
		hold = 150 },
	{ "fake_crash", function() frozen = 0 return pd.fake_crash(0.5) == true end,
		function()
			return frozen > 0 and pd.lvupdate() > 0, "frozen frames " .. frozen .. ", lvupdate now " .. pd.lvupdate()
				.. (frozen == 0 and "; no freeze - is an intro cutscene playing?" or "")
		end,
		hold = 120,
		check = function() if pd.lvupdate() == 0 then frozen = frozen + 1 end end },
}

local cur, phase, held = 1, "on", 0
local onok, ondetail

pd.on("draw", function()
	draws = draws + 1
	if done then
		return
	end
	if pd.player_count() < 1 or pd.player_pos() == nil or draws < 120 then
		return
	end

	local step = steps[cur]
	if step == nil then
		pd.log(string.format("fx smoke done %d/%d", results.pass, results.total))
		done = true
		return
	end

	local name, on, off = step[1], step[2], step[3]
	if phase == "on" then
		local ok, r, d = pcall(on)
		if not ok then
			onok, ondetail = false, r
		else
			onok, ondetail = r, d
		end
		phase, held = "hold", 0
	elseif phase == "hold" then
		held = held + 1
		if step.check then
			local ok, err = pcall(step.check, held)
			if not ok then
				onok, ondetail = false, err
			end
		end
		if held >= (step.hold or HOLD) then
			local ok, r, d = pcall(off)
			if not ok then
				report(name, false, r)
			elseif not onok then
				report(name, false, "on: " .. tostring(ondetail))
			else
				report(name, r, d or ondetail)
			end
			cur, phase = cur + 1, "on"
		end
	end
end)

pd.log("fx smoke loaded")
