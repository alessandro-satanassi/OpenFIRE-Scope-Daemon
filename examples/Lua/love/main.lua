--[[ main.lua - a tiny LÖVE 11 shooting gallery with an OpenFIRE scope for player 1.

    The mouse is the lightgun (an OpenFIRE lightgun moves the mouse too). The world is drawn into a canvas; the scope
    view is the same canvas drawn again into a 240x240 canvas, zoomed around the aim point, with a reticle on top.
    Click to shoot, mouse wheel to zoom, P or Esc for the pause menu (the scope keeps the last image), Q to quit.

    Run it from the Lua folder:   love love
    (it finds ../openfire_scope.lua by itself; in your game, put openfire_scope.lua in the game folder)

    MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE
]]
package.path = love.filesystem.getSourceBaseDirectory() .. "/?.lua;" .. package.path
local ofs = require("openfire_scope")

local PLAYER = 1   -- the scope of player 1 (the mouse); with several lightguns, one aim point and one view per player
local W, H = 960, 540

local scope, world, view
local targets, score, zoom, paused, t = {}, 0, 4, false, 0
local next_send = 0

-- RGBA ImageData -> RGB string. With LuaJIT's FFI (LÖVE 11.3 or later) it is a simple fast loop; otherwise the
-- library converts the RGBA string by itself (a few ms).
local ffi_ok, ffi = pcall(require, "ffi")
local rgb_buffer = ffi_ok and ffi.new("uint8_t[?]", 240 * 240 * 3) or nil
local function scope_pixels(imagedata)
    if rgb_buffer and imagedata.getFFIPointer then
        local src = ffi.cast("const uint8_t *", imagedata:getFFIPointer())
        for i = 0, 240 * 240 - 1 do
            rgb_buffer[i * 3], rgb_buffer[i * 3 + 1], rgb_buffer[i * 3 + 2] = src[i * 4], src[i * 4 + 1], src[i * 4 + 2]
        end
        return ffi.string(rgb_buffer, 240 * 240 * 3), ofs.RGB888
    end
    return imagedata:getString(), ofs.RGBA8888
end

local function new_target(tg)
    tg = tg or {}
    tg.x, tg.y, tg.speed = love.math.random(80, W - 80), love.math.random(150, H - 90), love.math.random() * 1.5 + 0.5
    return tg
end

function love.load(args)
    local always = false   -- "love love --always": send even without a scope (daemon Preview tab)
    for _, a in ipairs(args or {}) do if a == "--always" then always = true end end
    scope = assert(ofs.new({ always_send = always }))
    world = love.graphics.newCanvas(W, H)
    view = love.graphics.newCanvas(240, 240)
    for i = 1, 8 do targets[i] = new_target() end
end

local function target_pos(tg) return tg.x + 30 * math.sin(t * tg.speed), tg.y end

function love.update(dt)
    if not paused then t = t + dt end
    scope:update()   -- discovery + pause packets, never blocks
end

local function draw_world()
    love.graphics.setCanvas(world)
    love.graphics.clear(0.43, 0.67, 0.92)
    love.graphics.setColor(0.24, 0.55, 0.24)
    for x = 0, W - 1 do   -- hills, in columns 1 pixel wide
        local top = 330 + 35 * math.sin(x * 0.01 + 1) + 12 * math.sin(x * 0.045)
        love.graphics.rectangle("fill", x, top, 1, H - top)
    end
    for _, tg in ipairs(targets) do
        local x, y = target_pos(tg)
        for i, r in ipairs({ 18, 14, 9, 4 }) do
            if i % 2 == 1 then love.graphics.setColor(0.94, 0.94, 0.94) else love.graphics.setColor(0.86, 0.16, 0.16) end
            love.graphics.circle("fill", x, y, r)
        end
    end
    love.graphics.setColor(1, 1, 1)
    love.graphics.setCanvas()
end

-- the scope view: the world zoomed around the aim point, and our reticle (the scope draws none)
local function draw_view(ax, ay)
    love.graphics.setCanvas(view)
    love.graphics.clear(0, 0, 0)
    love.graphics.push()
    love.graphics.translate(120, 120)
    love.graphics.scale(zoom)
    love.graphics.translate(-ax, -ay)
    love.graphics.draw(world)
    love.graphics.pop()
    love.graphics.setColor(0, 0, 0)
    love.graphics.setLineWidth(2)
    love.graphics.line(120, 30, 120, 110); love.graphics.line(120, 130, 120, 210)
    love.graphics.line(30, 120, 110, 120); love.graphics.line(130, 120, 210, 120)
    love.graphics.setColor(1, 0.15, 0.15)
    love.graphics.circle("fill", 120, 120, 2)
    love.graphics.setColor(1, 1, 1)
    love.graphics.setCanvas()
end

function love.draw()
    draw_world()
    local ax, ay = love.mouse.getPosition()

    if paused then
        scope:set_paused(PLAYER, true)   -- pause menu: the scope keeps the last image
    elseif scope:has_scope(PLAYER) and love.timer.getTime() >= next_send then
        -- at most 60 images per second: without vsync LÖVE can draw hundreds of frames per second
        next_send = math.max(next_send + 1 / 60, love.timer.getTime() - 1 / 60)
        draw_view(ax, ay)
        local data = view:newImageData()   -- 240x240 RGBA, top row first
        scope:send_frame(PLAYER, scope_pixels(data))
        data:release()
    end

    love.graphics.draw(world)
    if scope:has_scope(PLAYER) then love.graphics.draw(view, W - 170, 10, 0, 160 / 240) end   -- HUD copy
    local state = scope.ports[PLAYER] ~= 0 and ("port " .. scope.ports[PLAYER]) or (scope.always_send and "always_send" or "none")
    love.graphics.setColor(0, 0, 0)
    love.graphics.print(("score %d   zoom %.1fx   scope P%d: %s"):format(score, zoom, PLAYER, state), 10, 10, 0, 1.5)
    love.graphics.setColor(1, 1, 1)
    if paused then love.graphics.print("PAUSED - press P", 380, 250, 0, 2) end
end

function love.mousepressed(x, y, button)
    if button ~= 1 or paused then return end
    for _, tg in ipairs(targets) do
        local tx, ty = target_pos(tg)
        if (tx - x) ^ 2 + (ty - y) ^ 2 < 18 * 18 then score = score + 1; new_target(tg) end   -- hit
    end
end

function love.wheelmoved(_, y) zoom = math.min(10, math.max(2, zoom + y * 0.5)) end

function love.keypressed(key)
    if key == "q" then love.event.quit()
    elseif key == "p" or key == "escape" then paused = not paused end
end

function love.quit()
    scope:close()   -- the scope shows NO SIGNAL about 0.5 s later
end
