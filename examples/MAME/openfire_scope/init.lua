-- openfire_scope: MAME plugin that sends the picture of a game's scope screen to an OpenFIRE scope.
--
-- Made for Silent Scope 1 and 2 (Konami Hornet), where the arcade board draws the view through the rifle scope
-- on a second screen of its own (":rscreen"). Every emulated frame the plugin cuts a square out of that screen,
-- scales it to 240 x 240 and sends it to the OpenFIRE Scope Daemon over TCP (MAME's Lua can do TCP but not UDP).
-- During a pause of MAME the scope keeps the last image. With other games, or when the daemon is not running,
-- the plugin does nothing.
--
-- The settings are just below; see README.md.
--
-- MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE
local exports = {
	name = 'openfire_scope',
	version = '1.0.0',
	description = 'OpenFIRE scope: the scope screen of the game on the OpenFIRE scope',
	license = 'MIT',
	author = { name = 'Alessandro Satanassi, Team OpenFIRE' } }

local plugin = exports

-- ---------------------------------------------------------------------------------------------------------------
-- Settings
-- ---------------------------------------------------------------------------------------------------------------
local settings = {
	player = 1,                 -- the OpenFIRE player whose scope gets the picture (1 to 4)
	screens = { ':rscreen' },   -- screens to look for, the first one found is used (Silent Scope: ':rscreen')
	-- Square cut out of the screen, in pixels of the emulated screen, centred. The scope screen of Silent Scope is
	-- 768 x 236 pixels shown on a 4:3 monitor: 576 x 236 is the square in the middle. nil = the whole width/height.
	crop_width = 576,
	crop_height = nil,
	offset_x = 0,               -- moves the square to the right (pixels, may be negative)
	offset_y = 0,               -- moves the square down
	-- Send one emulated frame out of this many. 2 = about 30 frames per second, as many as the radio link of the
	-- scope carries in practice, at half the cost (making a frame takes a few milliseconds of Lua).
	send_every = 2,
	discovery_port = 47810,     -- as in the daemon settings
}

-- ---------------------------------------------------------------------------------------------------------------
-- Protocol (see the README of the examples): over TCP the game sends the whole frame, 16-byte header + pixels.
-- ---------------------------------------------------------------------------------------------------------------
local FORMAT_BGRX = 2           -- MAME pixels are 32-bit 0xAARRGGBB in host order: bytes B, G, R, A on a PC
local STRIP_PAUSE = 0xFF
local PAUSE_EVERY_S = 0.2
local DISCOVERY_EVERY_S = 1.0
local ANSWER_WAIT_S = 0.5

local function header(frame, strip, session)
	return string.pack('<c2BBI4BBBI4B', 'OF', 1, FORMAT_BGRX, frame & 0xFFFFFFFF, strip, 15, settings.player, session, 0)
end

local function now()
	return emu.osd_ticks() / emu.osd_ticks_per_second()   -- real time, it also runs while MAME is paused
end

-- emu.file over TCP: open() connects at once (blocking), read() never blocks, write() blocks until sent.
local function connect(port)
	local file = emu.file('', 3)   -- read + write, no create: a client
	local err = file:open('socket.127.0.0.1:' .. port)
	if err then return nil end
	return file
end

local function write_all(file, data)
	local done = 0
	while done < #data do
		local n = file:write(done == 0 and data or data:sub(done + 1))
		if not n or n <= 0 then return false end
		done = done + n
	end
	return true
end

-- ---------------------------------------------------------------------------------------------------------------
-- Scaling: nearest pixel. string.unpack() reads the 240 chosen pixels of a row straight from the screen pixels,
-- skipping the others, and string.pack() writes them out: both run in C, with one format string made once.
-- A square narrower than 240 pixels is enlarged: some pixels are used twice, so each column is read once and a
-- map says which one goes where (slower, only in that case).
-- ---------------------------------------------------------------------------------------------------------------
local ROW_OUT = '<' .. string.rep('I4', 240)

local function row_format(w)
	local parts, previous, map, read = { '<' }, -1, {}, 0
	for k = 0, 239 do
		local column = (k * 2 + 1) * w // 480
		if column ~= previous then
			parts[#parts + 1] = string.rep('x', (column - previous - 1) * 4) .. 'I4'
			previous = column
			read = read + 1
		end
		map[k + 1] = read
	end
	return table.concat(parts), read < 240 and map or nil
end

-- ---------------------------------------------------------------------------------------------------------------
-- The plugin
-- ---------------------------------------------------------------------------------------------------------------
local subscriptions = {}

function plugin.startplugin()
	local screen, crop                 -- the scope screen and the square cut out of it
	local enabled = false              -- false: another game, or no daemon at the start
	local session, frame = 0, 0
	local video, video_port            -- TCP connection for the video
	local has_scope, video_wanted_port = false, nil
	local ask, answer, ask_time, next_discovery = nil, '', 0, 0
	local next_pause = 0
	local count = 0                    -- emulated frames, for send_every

	local function close_video()
		if video then video:close() end
		video, video_port = nil, nil
	end

	local function stop_all()
		close_video()
		if ask then ask:close() end
		ask = nil
		has_scope = false
	end

	local function give_up(why)
		emu.print_info('openfire_scope: ' .. why .. '; start the daemon, then reset the game (F3)')
		enabled = false
		stop_all()
	end

	local function find_screen()
		for _, tag in ipairs(settings.screens) do
			local s = manager.machine.screens[tag]
			if s then return s end
		end
		return nil
	end

	local function make_crop(s)
		local sw, sh = s.width, s.height
		local w = math.min(settings.crop_width or sw, sw)
		local h = math.min(settings.crop_height or sh, sh)
		local x = (sw - w) // 2 + settings.offset_x
		local y = (sh - h) // 2 + settings.offset_y
		x = math.max(0, math.min(x, sw - w))
		y = math.max(0, math.min(y, sh - h))
		local rows = {}
		for i = 0, 239 do rows[i + 1] = y + (i * 2 + 1) * h // 480 end
		local format, map = row_format(w)
		return { sw = sw, sh = sh, x = x, y = y, w = w, h = h, format = format, map = map, rows = rows }
	end

	local function picture()
		local pixels, sw, sh = screen:pixels()
		if sw ~= crop.sw or sh ~= crop.sh then crop = make_crop(screen) end   -- the game changed resolution
		local out, done = {}, {}
		local pack, unpack, format, map = string.pack, string.unpack, crop.format, crop.map
		local wide = {}
		for i = 1, 240 do
			local y = crop.rows[i]
			local row = done[y]
			if not row then
				local first = (y * crop.sw + crop.x) * 4 + 1
				if map then
					local columns = { unpack(format, pixels, first) }
					for k = 1, 240 do wide[k] = columns[map[k]] end
					row = pack(ROW_OUT, table.unpack(wide, 1, 240))
				else
					row = pack(ROW_OUT, unpack(format, pixels, first))
				end
				done[y] = row
			end
			out[i] = row
		end
		return table.concat(out)
	end

	-- Discovery over TCP, once a second: connect, write WHOAMI, the answer arrives in the next frames and the daemon
	-- then closes the connection. A daemon that does not answer counts as no scope.
	local function discovery(t)
		if ask then
			local got = ask:read(4096)
			if got and #got > 0 then
				answer = answer .. got
			elseif #answer > 0 or t - ask_time > ANSWER_WAIT_S then
				ask:close()
				ask = nil
				has_scope, video_wanted_port = false, nil
				for line in answer:gmatch('[^\n]+') do
					local p, port = line:match('^OF_SCOPE:P(%d);PORT=(%d+)')
					if tonumber(p) == settings.player then
						has_scope, video_wanted_port = true, tonumber(port)
					end
				end
			end
			return
		end
		if t < next_discovery then return end
		next_discovery = t + DISCOVERY_EVERY_S
		ask = connect(settings.discovery_port)
		if not ask then
			-- no daemon: stop until the next reset (on Windows a connection to a closed port can take 2 s)
			give_up('no answer from the OpenFIRE Scope Daemon')
			return
		end
		answer, ask_time = '', t
		if not write_all(ask, 'WHOAMI') then
			ask:close()
			ask = nil
		end
	end

	local function send(data)
		if video and video_port ~= video_wanted_port then close_video() end
		if not video then
			video = connect(video_wanted_port)
			video_port = video_wanted_port
			if not video then
				give_up(string.format('no TCP video on port %d', video_wanted_port))
				return
			end
		end
		if not write_all(video, data) then
			close_video()
			has_scope = false
		end
	end

	local function on_frame()
		if not enabled then return end
		local t = now()
		discovery(t)
		if not enabled then return end
		if not has_scope then   -- no scope paired for this player: nothing to make
			close_video()
			return
		end
		if manager.machine.paused then
			if t >= next_pause then
				next_pause = t + PAUSE_EVERY_S
				send(header(frame, STRIP_PAUSE, session))
			end
			return
		end
		next_pause = 0
		count = count + 1
		if count % math.max(1, settings.send_every) ~= 0 then return end
		frame = frame + 1
		send(header(frame, 0, session) .. picture())
	end

	local function on_reset()
		stop_all()
		screen = find_screen()
		enabled = screen ~= nil
		if not enabled then return end
		crop = make_crop(screen)
		session = math.random(1, 0x7FFFFFFF)
		frame, next_discovery = 0, 0
	end

	subscriptions.reset = emu.add_machine_reset_notifier(on_reset)
	subscriptions.frame = emu.add_machine_frame_notifier(on_frame)
	subscriptions.stop = emu.add_machine_stop_notifier(function ()
		stop_all()
		enabled = false
	end)
end

return exports
