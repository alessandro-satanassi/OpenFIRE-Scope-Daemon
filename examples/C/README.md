# C: `openfire_scope.h`

A single-file C library (C99, no dependencies) that does everything a game needs: discovery, video, pause.
It works on Windows, Linux and macOS, and from C++ too. The other examples in this folder (C++, Unreal, ...) use it
or copy its logic.

| File | What it is |
| --- | --- |
| `openfire_scope.h` | The library. Copy it into your game. |
| `example_basic.c` | A complete small "game": animated scope view at 60 fps, a pause, the end. |

## Add it to your game

1. Copy `openfire_scope.h` into your sources.
2. In **one** `.c` or `.cpp` file write:
   ```c
   #define OPENFIRE_SCOPE_IMPLEMENTATION
   #include "openfire_scope.h"
   ```
   Everywhere else include it without the define.
3. On Windows link `ws2_32` (MinGW: `-lws2_32`; MSVC links it automatically).

## Use it

```c
ofs_client scope;
ofs_open(&scope, 0);                          // at game start; 0 = discovery port 47810

// every game frame:
ofs_update(&scope);                           // never blocks
for (int p = 1; p <= 4; p++) {
    if (!ofs_has_scope(&scope, p)) continue;  // that player has no scope: render nothing
    render_scope_view(p, pixels);             // your 240x240 image for player p
    ofs_send_frame(&scope, p, OFS_PIXELS_RGBA8888, pixels, 0, 0);
}

// pause menu: the scope keeps the last image instead of showing NO SIGNAL
ofs_set_paused(&scope, 0, 1);                 // 0 = all players; the next ofs_send_frame() ends it

ofs_close(&scope);                            // at game end
```

### Functions

| Function | What it does |
| --- | --- |
| `ofs_open(c, port)` | Opens a non-blocking UDP socket on 127.0.0.1 and starts the discovery. `port` 0 means 47810. Returns 0, or -1 on error. |
| `ofs_update(c)` | Call it once per frame. It reads the daemon's answers, asks again once a second, and sends the pause packet every 200 ms for paused players. |
| `ofs_has_scope(c, p)` | 1 if player `p` (1 to 4) has a paired scope right now. |
| `ofs_send_frame(c, p, pixels, image, stride, flags)` | Sends one 240x240 image to the scope of player `p`. Returns `OFS_OK`, `OFS_NO_SCOPE` (nothing sent) or `OFS_ERROR`. |
| `ofs_set_paused(c, p, on)` | Pause on or off for one player, or for all with `p` = 0. |
| `ofs_new_session(c)` | A new random session. Call it when a brand-new match starts (optional). |
| `ofs_close(c)` | Closes the socket. |
| `ofs_now_ms()` | The monotonic clock in ms that the library uses. |

`pixels` tells the library how your image is laid out in memory:

| `pixels` | Bytes per pixel | Layout | Typical source |
| --- | --- | --- | --- |
| `OFS_PIXELS_RGBA8888` | 4 | R, G, B, A | OpenGL/Vulkan read-back, most engines (sent as RGB888) |
| `OFS_PIXELS_BGRX8888` | 4 | B, G, R, X | Direct3D `B8G8R8A8` textures, Windows bitmaps |
| `OFS_PIXELS_RGB888` | 3 | R, G, B | image libraries |
| `OFS_PIXELS_RGB565` | 2 | 16-bit little-endian | 16-bit renderers, emulators |

- `stride` is the number of bytes from one row to the next. Use 0 for tightly packed rows; GPU read-backs often add
  padding at the end of each row.
- `flags` = `OFS_FLIP_Y` when the first row in memory is the **bottom** of the image, as with `glReadPixels`.

### Good to know

- **Nothing blocks.** `ofs_update()` and `ofs_send_frame()` never wait for the daemon, so the game loop never stalls.
  If the system has no room for a strip, it is dropped and the daemon waits for the next frame.
- **Frame numbers and sessions** are handled for you. Each player has its own frame counter, and the session is new
  at every `ofs_open()`.
- **Daemon closed or restarted:** after 3 s without answers every `ofs_has_scope()` becomes 0, and it becomes 1 again
  as soon as the daemon is back. The game does not need to do anything.
- **Testing without scopes:** set `scope.always_send = 1` after `ofs_open()`. The frames then go to the default ports
  47811 to 47814 even with no scope, and you can watch them in the daemon's **Preview** tab, or with
  `../Tools/fake_daemon.py` without the daemon at all.
- **Thread safety:** an `ofs_client` belongs to one thread. Use one client per thread if you send from several.

## Build and run the example

```
Linux / macOS :  cc -O2 -o example_basic example_basic.c -lm
Windows MinGW :  gcc -O2 -o example_basic.exe example_basic.c -lws2_32
Windows MSVC  :  cl /O2 example_basic.c
```

```
./example_basic                       # 60 fps, 20 s, RGBA pixels
./example_basic --format rgb565       # the same in RGB565 (also: rgb888, bgrx, rgba)
./example_basic --always              # also without scopes, for the Preview tab
```

It prints once a second which players have a scope. From 10 s to 13 s it simulates a pause menu.
