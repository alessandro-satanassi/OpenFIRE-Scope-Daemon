--[[ openfire_scope.lua - send a game's video to the OpenFIRE scopes through the OpenFIRE Scope Daemon.

    One file for Lua 5.1, 5.2, 5.3, 5.4 and LuaJIT. Needs LuaSocket (built into LÖVE; elsewhere
    "luarocks install luasocket"). Windows, Linux and macOS.

        local ofs = require("openfire_scope")
        local scope = ofs.new()                        -- opens the socket, starts the discovery
        -- every game frame:
        scope:update()                                 -- never blocks
        for p = 1, ofs.MAX_PLAYERS do
            if scope:has_scope(p) then
                local pixels = render_scope_view(p)    -- 240x240 as a string, e.g. LÖVE ImageData:getString()
                scope:send_frame(p, pixels, ofs.RGBA8888)
            end
        end
        scope:close()                                  -- at game end

    See README.md next to this file.

    MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE
]]
local socket = require("socket")

local floor, char, sub, concat = math.floor, string.char, string.sub, table.concat

local M = {
    WIDTH = 240, HEIGHT = 240,      -- the scope image is always 240 x 240
    MAX_PLAYERS = 4,                -- players 1..4
    DISCOVERY_PORT = 47810,         -- default; the user can change it in the daemon settings

    -- Pixel layouts accepted by send_frame(). The first three go to the daemon as they are; RGBA8888 is converted
    -- to RGB888 by the library.
    RGB888 = 0,                     -- 3 bytes: R, G, B
    RGB565 = 1,                     -- 2 bytes: 16-bit little-endian, R in the 5 high bits
    BGRX8888 = 2,                   -- 4 bytes: B, G, R, X
    RGBA8888 = 3,                   -- 4 bytes: R, G, B, A (LÖVE ImageData "rgba8")

    -- Results of send_frame().
    OK = 0,
    NO_SCOPE = 1,                   -- no scope for that player right now: nothing sent (not an error)
    ERROR = -1,                     -- bad arguments or closed
}

local STRIPS, STRIP_ROWS = 15, 16
local BPP = { [0] = 3, [1] = 2, [2] = 4, [3] = 4 }
local WHOAMI_EVERY = 1.0            -- discovery request, seconds
local ANSWER_TIMEOUT = 3.0          -- no answer for so long: daemon gone, no scopes
local PRESENCE_EVERY = 0.2          -- pause packet; the daemon forgets a source after 0.5 s
local LOCALHOST = "127.0.0.1"

local function u32(v)               -- little-endian
    return char(v % 256, floor(v / 256) % 256, floor(v / 65536) % 256, floor(v / 16777216) % 256)
end

local Scope = {}
Scope.__index = Scope

--- Opens the UDP socket and sends the first discovery request.
-- options (all optional): discovery_port (default 47810), always_send (default false: true sends to the default
-- ports 47811-47814 even without a scope, for testing with the daemon's Preview tab or Tools/fake_daemon.py).
-- Returns the scope, or nil and an error message.
function M.new(options)
    options = options or {}
    local udp = (socket.udp4 or socket.udp)()
    if not udp then return nil, "cannot create a UDP socket" end
    local ok, err = udp:setsockname(LOCALHOST, 0)   -- any free port: the answers come back here
    if not ok then udp:close() return nil, err end
    udp:settimeout(0)                                -- never block
    -- 15 datagrams in a row; on macOS it is also needed for datagrams over 9 KB (see README.md)
    local called, sndbuf = pcall(udp.setoption, udp, "send-buffer-size", 4 * 1024 * 1024)
    sndbuf = called and sndbuf ~= nil
    local self = setmetatable({
        always_send = options.always_send and true or false,
        daemon_seen = false,                         -- true while the daemon answers the discovery
        ports = { 0, 0, 0, 0 },                      -- video port of each player (1..4), 0 = no scope
        send_buffer_set = sndbuf,                    -- false if this LuaSocket cannot set the send buffer
        _udp = udp,
        _discovery_port = options.discovery_port or M.DISCOVERY_PORT,
        _frame = { 0, 0, 0, 0 },
        _format = { 0, 0, 0, 0 },
        _paused = { false, false, false, false },
        _next_presence = { 0, 0, 0, 0 },
        _next_whoami = 0,
        _last_answer = 0,
        _session = 0,
    }, Scope)
    self:new_session()
    self:update()                                    -- first discovery request at once
    return self
end

--- A new random session: call it when a brand-new match starts (optional; new() does it).
function Scope:new_session()
    local s
    repeat
        s = (math.random(0, 0x7FFFFFFF) * 2 + floor(socket.gettime() * 1000)) % 4294967296
    until s ~= 0 and s ~= self._session
    self._session = s
end

local function header(self, fmt, frame, strip, player)
    return "OF" .. char(1, fmt) .. u32(frame) .. char(strip, STRIPS, player) .. u32(self._session) .. "\0"
end

local function port_of(self, player)
    return self.ports[player] ~= 0 and self.ports[player] or M.DISCOVERY_PORT + player
end

-- "OF_SCOPE:P1;PORT=47811;FW=...\nOF_SCOPE:UNPAIRED\n..." -> ports
local function parse_answer(self, text)
    local ports = { 0, 0, 0, 0 }
    for line in (text .. "\n"):gmatch("([^\n]*)\n") do
        local player, rest = line:match("^OF_SCOPE:P([1-4])(.*)$")
        if player and (rest == "" or rest:sub(1, 1) == ";") then
            local port = tonumber(rest:match(";PORT=(%d+)") or "")
            if port and port > 0 and port < 65536 then ports[tonumber(player)] = port end
        end
    end
    self.ports = ports
end

--- Call once per game frame. Never blocks: reads the daemon's answers, asks again once a second, and sends the
-- pause packet every 200 ms for the paused players.
function Scope:update()
    local udp = self._udp
    if not udp then return end
    local now = socket.gettime()

    -- 1. answers of the daemon
    while true do
        local data, ip, port = udp:receivefrom()
        if not data then break end                   -- "timeout" = nothing more to read
        if port == self._discovery_port then         -- only the daemon's answers
            parse_answer(self, data)
            self._last_answer = now
            self.daemon_seen = true
        end
    end
    if self.daemon_seen and now - self._last_answer > ANSWER_TIMEOUT then   -- daemon closed
        self.ports = { 0, 0, 0, 0 }
        self.daemon_seen = false
    end

    -- 2. discovery request once a second: scopes can be paired, switched off or moved at any time
    if now >= self._next_whoami then
        udp:sendto("WHOAMI", LOCALHOST, self._discovery_port)
        self._next_whoami = now + WHOAMI_EVERY
    end

    -- 3. pause packets: the 16-byte header alone, strip 0xFF
    for p = 1, M.MAX_PLAYERS do
        if self._paused[p] and self:has_scope(p) and now >= self._next_presence[p] then
            udp:sendto(header(self, self._format[p], self._frame[p], 0xFF, p), LOCALHOST, port_of(self, p))
            self._next_presence[p] = now + PRESENCE_EVERY
        end
    end
end

--- true if player 1..4 has a paired scope right now (or always_send is on).
function Scope:has_scope(player)
    return self.ports[player] ~= nil and (self.ports[player] ~= 0 or self.always_send)
end

--- Sends one 240x240 image to the scope of a player. It also ends the pause of that player.
-- image:  a string with the pixels (LÖVE: ImageData:getString()).
-- pixels: M.RGB888, M.RGB565, M.BGRX8888 or M.RGBA8888 (default RGBA8888).
-- stride: bytes from one row to the next, 0 or nil = tightly packed.
-- flip_y: true when the first row in the string is the bottom of the image.
-- Returns M.OK, M.NO_SCOPE (nothing sent) or M.ERROR.
function Scope:send_frame(player, image, pixels, stride, flip_y)
    pixels = pixels or M.RGBA8888
    if not self._udp or type(image) ~= "string" or not BPP[pixels] or self.ports[player] == nil then return M.ERROR end
    local row = M.WIDTH * BPP[pixels]
    if not stride or stride == 0 then stride = row end
    if stride < row or #image < stride * (M.HEIGHT - 1) + row then return M.ERROR end
    if not self:has_scope(player) then return M.NO_SCOPE end

    local wire = pixels == M.RGBA8888 and M.RGB888 or pixels   -- format written in the header
    local port = port_of(self, player)
    self._paused[player] = false
    self._format[player] = wire
    self._frame[player] = (self._frame[player] + 1) % 4294967296   -- one number per frame and per player

    local rows = {}
    for strip = 0, STRIPS - 1 do
        for r = 1, STRIP_ROWS do
            local y = strip * STRIP_ROWS + r - 1
            local first = (flip_y and (M.HEIGHT - 1 - y) or y) * stride + 1
            rows[r] = sub(image, first, first + row - 1)
        end
        local payload = concat(rows)
        if pixels == M.RGBA8888 then payload = payload:gsub("(...).", "%1") end   -- drop the alpha byte
        -- errors are ignored on purpose: a lost strip only makes that frame incomplete
        self._udp:sendto(header(self, wire, self._frame[player], strip, player) .. payload, LOCALHOST, port)
    end
    return M.OK
end

--- Game paused (menu, pause screen) for one player 1..4, or for all with player 0: the scope keeps the last image
-- instead of showing NO SIGNAL. The next send_frame() of that player ends it.
function Scope:set_paused(player, paused)
    if paused == nil then paused = true end
    for p = 1, M.MAX_PLAYERS do
        if player == 0 or player == p then
            if paused and not self._paused[p] then self._next_presence[p] = 0 end   -- first packet at the next update()
            self._paused[p] = paused and true or false
        end
    end
end

--- Closes the socket. The scopes show NO SIGNAL (or the daemon's screen capture) about 0.5 s later.
function Scope:close()
    if self._udp then self._udp:close() end
    self._udp = nil
    self.ports = { 0, 0, 0, 0 }
    self.daemon_seen = false
end

return M
