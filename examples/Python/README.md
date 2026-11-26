# Python: `openfire_scope.py`

One file, Python 3.8 or later, **standard library only**. It accepts `bytes`, `bytearray`, `memoryview` and numpy
arrays, so it works with pygame, Pillow, numpy, PyOpenGL, arcade, pyglet, Panda3D, Ursina and anything else that can
give you the pixels. Read the main [README](../README.md) first for what to show on the scope.

| File | What it is |
| --- | --- |
| `openfire_scope.py` | The library: class `Scope`. Copy it next to your game. |
| `example_basic.py` | The smallest complete "game", standard library only: a scope view at 60 fps for every player with a scope, a pause, the end. |
| `example_pygame.py` | A tiny pygame shooting gallery: the mouse is the lightgun, the scope of player 1 shows a zoom around the aim point. |

## Use it

```python
import openfire_scope as ofs

scope = ofs.Scope()                            # at game start: opens the socket, starts the discovery
                                               # ofs.Scope(47900) for another discovery port
# every game frame:
scope.update()                                 # never blocks
for p in range(1, ofs.MAX_PLAYERS + 1):
    if not scope.has_scope(p):
        continue                               # no scope for that player: render nothing
    pixels = render_scope_view(p)              # your 240x240 image
    scope.send_frame(p, pixels, ofs.RGB888)

scope.set_paused(0)                            # pause menu, all players; the next send_frame() ends it

scope.close()                                  # at game end (or use "with ofs.Scope() as scope:")
```

| Member | What it does |
| --- | --- |
| `Scope(discovery_port=47810, always_send=False)` | Opens a non-blocking UDP socket on 127.0.0.1 and starts the discovery. |
| `update()` | Once per frame: reads the daemon's answers, asks again once a second, sends the pause packets. Never blocks. |
| `has_scope(p)` | `True` if player `p` (1 to 4) has a paired scope now. |
| `send_frame(p, image, pixels=RGB888, stride=0, flip_y=False)` | Sends one 240x240 image. Returns `OK`, `NO_SCOPE` (nothing sent) or `ERROR`. |
| `set_paused(p, paused=True)` | Pause for one player, or all with `p` = 0. |
| `new_session()` | A new random session for a brand-new match (optional). |
| `close()` | Closes the socket. |
| `always_send` | Testing: send to the default ports 47811-47814 even with no scope. |
| `ports`, `daemon_seen` | Diagnostics: the video port of each player (index 1 to 4, 0 = none), the daemon answers. |

Pixel layouts: `ofs.RGB888` (3 bytes), `ofs.RGB565` (2 bytes, little-endian), `ofs.BGRX8888` (4 bytes B, G, R, X),
`ofs.RGBA8888` (4 bytes R, G, B, A, sent as RGB888). `stride` is the number of bytes from one row to the next (0 =
tightly packed); `flip_y=True` when the first row in memory is the bottom of the image. A numpy array must be
C-contiguous (`np.ascontiguousarray(a)` if you sliced it), otherwise `send_frame` returns `ERROR`.

### Where the pixels come from

| Library | Code |
| --- | --- |
| pygame | `scope.send_frame(p, pygame.image.tobytes(surface, "RGB"))` (`tostring` before pygame 2.1.3) |
| Pillow | `scope.send_frame(p, img.convert("RGB").tobytes())` |
| numpy | `scope.send_frame(p, array)` with `array.shape == (240, 240, 3)` and `dtype=uint8` |
| OpenGL (PyOpenGL, moderngl) | `glReadPixels(0, 0, 240, 240, GL_RGB, GL_UNSIGNED_BYTE)` then `send_frame(p, data, ofs.RGB888, flip_y=True)`; set `glPixelStorei(GL_PACK_ALIGNMENT, 1)` |

### Good to know

- **Speed.** Sending a frame costs about 0.1 ms in RGB888, RGB565 or BGRX, and about 1 ms in RGBA8888 (the alpha
  byte is removed in Python): ask your library for RGB when you can. `example_basic.py` sends 4 players at 60 fps in
  pure Python. Your own rendering is what costs: draw the scope view with your engine, not pixel by pixel in Python.
- **Nothing blocks**, and a strip the system has no room for is simply dropped.
- **Daemon closed or restarted:** after 3 s without answers `has_scope()` becomes `False`, and `True` again when the
  daemon is back.
- **One thread.** Call it from the game loop, or use one `Scope` per thread.

## Run the examples

```
python example_basic.py                   # 60 fps, 20 s, pause from 10 s to 13 s
python example_basic.py --always          # also without scopes, for the daemon's Preview tab

pip install pygame
python example_pygame.py                  # mouse = lightgun, click = shoot, wheel = zoom, P = pause, Q = quit
python example_pygame.py --demo 20        # aims by itself for 20 s
```

Without the daemon and without scopes: run `python ../Tools/fake_daemon.py --save frames` in another terminal first.
