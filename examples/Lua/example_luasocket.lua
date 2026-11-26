--[[ example_luasocket.lua - the smallest complete "game" for the OpenFIRE scopes, in plain Lua with LuaSocket.

    It builds a 240x240 RGB scope view (a landscape that pans, a target, a reticle) for every player that has a
    scope and sends it at 60 fps. From 10 s to 13 s it "pauses the game" (the scopes keep the last image); at the
    end it closes (the scopes show NO SIGNAL).

        lua example_luasocket.lua                  60 fps, 20 s
        lua example_luasocket.lua --always         also without scopes: watch the daemon's Preview tab
        luajit example_luasocket.lua --seconds 60

    MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE
]]
package.path = (arg and arg[0] and arg[0]:match("^(.*[/\\])") or "") .. "?.lua;" .. package.path
local ofs = require("openfire_scope")
local socket = require("socket")

local always, seconds = false, 20
for i = 1, #arg do
    if arg[i] == "--always" then always = true end
    if arg[i] == "--seconds" then seconds = tonumber(arg[i + 1]) or seconds end
end

local W, H, PANO = ofs.WIDTH, ofs.HEIGHT, 720
local char, floor, sin = string.char, math.floor, math.sin

-- A 720x240 panorama made once, as one RGB string per row. Pure Lua is too slow to draw every pixel of every
-- frame; a real game renders the view with its engine (see ../README.md, "What to show on the scope").
local pano = {}
for y = 0, H - 1 do
    local px = {}
    for x = 0, PANO - 1 do
        local a = 2 * math.pi * x / PANO
        local hill = 130 + 25 * sin(3 * a) + 10 * sin(11 * a)
        if y < hill then px[x + 1] = char(90 + floor(y / 3), 150 + floor(y / 4), 230)
        else px[x + 1] = char(40 + ((floor(x / 6) + floor(y / 6)) % 2) * 15, 125 - floor((y - 130) / 3), 40) end
    end
    local s = table.concat(px)
    pano[y] = s .. s:sub(1, W * 3)   -- wrap around: any 240-pixel window is one substring
end
local reticle_row = string.rep("\0", 3 * 2)

local function render_scope_view(player, t)
    local pan = floor(t * 40 + player * 180) % PANO
    local tx, ty = floor(120 + 70 * sin(t * 1.3 + player)), floor(125 + 25 * math.cos(t * 0.9))
    local rows = {}
    for y = 0, H - 1 do
        local r = pano[y]:sub(pan * 3 + 1, (pan + W) * 3)
        if y >= ty - 12 and y < ty + 12 then   -- a red target
            r = r:sub(1, (tx - 12) * 3) .. string.rep("\230\40\30", 24) .. r:sub((tx + 12) * 3 + 1)
        end
        if (y >= 50 and y < 112) or (y >= 128 and y < 190) then   -- reticle, vertical line
            r = r:sub(1, 119 * 3) .. reticle_row .. r:sub(121 * 3 + 1)
        elseif y == 119 or y == 120 then                         -- reticle, horizontal line
            r = r:sub(1, 50 * 3) .. string.rep("\0", 62 * 3) .. r:sub(112 * 3 + 1, 128 * 3) .. string.rep("\0", 62 * 3) .. r:sub(190 * 3 + 1)
        end
        rows[y + 1] = r
    end
    return table.concat(rows)
end

local scope = assert(ofs.new({ always_send = always }))
if not scope.send_buffer_set then print("note: this LuaSocket cannot set the send buffer (on macOS use RGB565)") end
local start = socket.gettime()
local next_frame, last_print = start, 0
while true do
    local now = socket.gettime()
    local t = now - start
    if t >= seconds then break end
    local paused = t >= 10 and t < 13   -- the "pause menu"

    scope:update()   -- discovery + pause packets, never blocks
    for p = 1, ofs.MAX_PLAYERS do
        if scope:has_scope(p) then      -- no scope for that player: render nothing
            if paused then scope:set_paused(p, true)   -- the scope keeps the last image
            else scope:send_frame(p, render_scope_view(p, t), ofs.RGB888) end
        end
    end

    if now - last_print >= 1 then
        last_print = now
        local list = {}
        for p = 1, 4 do list[p] = ("P%d=%s"):format(p, scope.ports[p] ~= 0 and "yes" or "-") end
        print(("%5.1f s  daemon %s  scopes: %s%s"):format(t, scope.daemon_seen and "yes" or "no ",
            table.concat(list, " "), paused and "  (paused)" or ""))
        io.stdout:flush()
    end

    next_frame = next_frame + 1 / 60   -- fixed 60 fps; a real game calls this from its own loop
    local delay = next_frame - socket.gettime()
    if delay > 0 then socket.sleep(delay) else next_frame = socket.gettime() end
end
scope:close()   -- the scopes show NO SIGNAL about 0.5 s later
