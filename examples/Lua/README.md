# Lua: `openfire_scope.lua`

One file for **Lua 5.1, 5.2, 5.3, 5.4 and LuaJIT**. It needs **LuaSocket**: it is built into LÖVE; elsewhere
`luarocks install luasocket`. Read the main [README](../README.md) first for what to show on the scope.

| File | What it is |
| --- | --- |
| `openfire_scope.lua` | The library. Copy it into your game. |
| `example_luasocket.lua` | The smallest complete "game" in plain Lua: a scope view at 60 fps for every player with a scope, a pause, the end. |
| `love/` | A tiny **LÖVE 11** shooting gallery: the mouse is the lightgun, the scope of player 1 shows a zoom around the aim point. |

## Use it

```lua
local ofs = require("openfire_scope")

local scope = assert(ofs.new())                -- at game start: opens the socket, starts the discovery
                                               -- ofs.new({ discovery_port = 47900 }) for another port
-- every game frame:
scope:update()                                 -- never blocks
for p = 1, ofs.MAX_PLAYERS do
    if scope:has_scope(p) then                 -- no scope for that player: render nothing
        local pixels = render_scope_view(p)    -- your 240x240 image as a string
        scope:send_frame(p, pixels, ofs.RGBA8888)
    end
end

scope:set_paused(0, true)                      -- pause menu, all players; the next send_frame() ends it

scope:close()                                  -- at game end
```

| Function | What it does |
| --- | --- |
| `ofs.new(options)` | Opens a non-blocking UDP socket on 127.0.0.1 and starts the discovery. `options.discovery_port` (47810), `options.always_send` (false). Returns the scope, or `nil` and an error. |
| `scope:update()` | Once per frame: reads the daemon's answers, asks again once a second, sends the pause packets. Never blocks. |
| `scope:has_scope(p)` | `true` if player `p` (1 to 4) has a paired scope now. |
| `scope:send_frame(p, image, pixels, stride, flip_y)` | Sends one 240x240 image (a string). `pixels` defaults to `ofs.RGBA8888`. Returns `ofs.OK`, `ofs.NO_SCOPE` (nothing sent) or `ofs.ERROR`. |
| `scope:set_paused(p, paused)` | Pause for one player, or all with `p` = 0. |
| `scope:new_session()` | A new random session for a brand-new match (optional). |
| `scope:close()` | Closes the socket. |
| `scope.always_send` | Testing: send to the default ports 47811-47814 even with no scope. |
| `scope.ports`, `scope.daemon_seen` | Diagnostics: the video port of each player (0 = none), the daemon answers. |
| `scope.send_buffer_set` | `false` if this LuaSocket could not enlarge the send buffer (see macOS below). |

Pixel layouts: `ofs.RGB888` (3 bytes), `ofs.RGB565` (2 bytes, little-endian), `ofs.BGRX8888` (4 bytes B, G, R, X),
`ofs.RGBA8888` (4 bytes R, G, B, A: LÖVE's `ImageData`; sent as RGB888). `stride` is the number of bytes from one
row to the next (`nil` or 0 = tightly packed). `flip_y = true` when the first row is the bottom of the image.

### Good to know

- **Speed.** Sending RGB888, RGB565 or BGRX costs well under a millisecond. RGBA8888 has to lose its alpha byte in
  Lua, about 3 ms per frame: with LuaJIT's FFI it can be done faster, as `love/main.lua` shows.
- **Draw with your engine.** Building the image pixel by pixel in Lua is too slow for 60 fps; let the engine render
  the scope view (LÖVE: a 240x240 `Canvas`), or copy ready rows as `example_luasocket.lua` does.
- **macOS:** the scope datagrams are larger than 9 KB, which macOS sends only if the socket's send buffer is
  enlarged. The library does it with the `send-buffer-size` option of LuaSocket 3. If `scope.send_buffer_set` is
  `false` (an old LuaSocket), send `ofs.RGB565`: its datagrams are 7.5 KB.
- **Daemon closed or restarted:** after 3 s without answers `has_scope()` becomes `false`, and `true` again when the
  daemon is back.

## LÖVE

The scope view is a 240x240 `Canvas`. Each frame:

```lua
love.graphics.setCanvas(view)                  -- 240x240
love.graphics.clear(0, 0, 0)
love.graphics.push()
love.graphics.translate(120, 120)              -- the aim point goes to the centre of the scope...
love.graphics.scale(zoom)                      -- ...zoomed
love.graphics.translate(-aimX, -aimY)
love.graphics.draw(world)                      -- the world canvas (without the HUD)
love.graphics.pop()
-- draw the reticle here: the scope draws none
love.graphics.setCanvas()

local data = view:newImageData()               -- RGBA, top row first
scope:send_frame(1, data:getString(), ofs.RGBA8888)
data:release()
```

Without vsync LÖVE may draw hundreds of frames per second: send at most 60 images per second (the example shows how).

Run the example from this folder with `love love` (`love love --always` to see it in the daemon's Preview tab without
a scope). In your own game put `openfire_scope.lua` in the game folder.

## Run the plain Lua example

```
lua example_luasocket.lua                 # 60 fps, 20 s, pause from 10 s to 13 s
lua example_luasocket.lua --always        # also without scopes, for the daemon's Preview tab
luajit example_luasocket.lua --seconds 60
```

Without the daemon and without scopes: run `python ../Tools/fake_daemon.py --save frames` in another terminal first.
