# C++: `openfire_scope.hpp`

A small C++17 class over the C library in [`../C`](../C). It adds nothing to the protocol: it only gives the C
functions a C++ shape (a class that closes the socket in its destructor, `enum class` for the pixel layouts). Read
[`../C/README.md`](../C/README.md) for every detail, and the main [README](../README.md) for what to show on the scope.

| File | What it is |
| --- | --- |
| `openfire_scope.hpp` | The class `openfire::Scope`. Needs `../C/openfire_scope.h`. |
| `example_crop.cpp` | The "crop around the aim point" technique: a 960x540 game screen, a 4x zoomed square around the aim point of each player, a reticle, sent in BGRX (the layout of a Direct3D back buffer). |

## Add it to your game

1. Copy `openfire_scope.hpp` **and** `../C/openfire_scope.h` into the same folder of your sources.
2. In **one** `.cpp` file write:
   ```cpp
   #define OPENFIRE_SCOPE_IMPLEMENTATION
   #include "openfire_scope.hpp"
   ```
   Everywhere else include it without the define.
3. On Windows link `ws2_32` (MinGW: `-lws2_32`; MSVC links it automatically).

## Use it

```cpp
openfire::Scope scope;                         // at game start: opens the socket, starts the discovery
                                               // (openfire::Scope scope(47900); for another discovery port)
// every game frame:
scope.update();                                // never blocks
for (int p = 1; p <= openfire::MaxPlayers; ++p) {
    if (!scope.hasScope(p)) continue;          // no scope for that player: render nothing
    renderScopeView(p, pixels);                // your 240x240 image
    scope.sendFrame(p, openfire::Pixels::RGBA8888, pixels.data());
}

scope.setPaused(0, true);                      // pause menu, all players; the next sendFrame() ends it
// the destructor closes the socket at game end
```

| Member | What it does |
| --- | --- |
| `Scope(port = 0)` | Opens the socket; `port` 0 means 47810. Check `isOpen()`. |
| `update()` | Once per frame: discovery, answers, pause packets. Never blocks. |
| `hasScope(p)` | `true` if player `p` (1 to 4) has a paired scope now. |
| `sendFrame(p, pixels, image, stride = 0, flipY = false)` | Sends one 240x240 image. Returns `Result::Ok`, `Result::NoScope` or `Result::Error`. |
| `setPaused(p, on)` | Pause for one player, or all with `p` = 0. |
| `newSession()` | A new random session for a brand-new match (optional). |
| `setAlwaysSend(on)` | Testing: send to the default ports even with no scope. |
| `daemonSeen()`, `port(p)` | Diagnostics: the daemon answers, the video port of a player. |
| `Scope::nowMs()` | The monotonic clock of the library, in ms. |

`openfire::Pixels`: `RGB888`, `RGB565`, `BGRX8888` (Direct3D `B8G8R8A8`), `RGBA8888` (OpenGL/Vulkan
`R8G8B8A8`). With `glReadPixels` pass `flipY = true`. With a GPU read-back that pads its rows, pass the row pitch as
`stride`.

### Getting the image from the GPU

- **Direct3D 11:** render the scope camera into a 240x240 `DXGI_FORMAT_B8G8R8A8_UNORM` texture, `CopyResource` it
  into a staging texture (`D3D11_USAGE_STAGING`, `D3D11_CPU_ACCESS_READ`), `Map` it and call
  `sendFrame(p, Pixels::BGRX8888, mapped.pData, mapped.RowPitch)`. To avoid waiting for the GPU, use two or three
  staging textures in turn and map the one copied a frame or two before.
- **OpenGL:** render into a 240x240 FBO, `glReadPixels(0, 0, 240, 240, GL_RGBA, GL_UNSIGNED_BYTE, buf)` and
  `sendFrame(p, Pixels::RGBA8888, buf, 0, true)`. A pixel buffer object (`GL_PIXEL_PACK_BUFFER`) read one frame later
  avoids the stall.
- **Vulkan:** copy the 240x240 image into a host-visible buffer with `vkCmdCopyImageToBuffer` and send it after the
  fence of that frame, as `RGBA8888` or `BGRX8888` depending on the image format.

A one-frame delay on the scope is not visible; a stalled game loop is.

## Build and run the example

```
Linux / macOS :  c++ -std=c++17 -O2 -I../C -o example_crop example_crop.cpp
Windows MinGW :  g++ -std=c++17 -O2 -I../C -o example_crop.exe example_crop.cpp -lws2_32
Windows MSVC  :  cl /std:c++17 /O2 /EHsc /I..\C example_crop.cpp
```

```
./example_crop                 # 60 fps, 20 s, 4x zoom
./example_crop --zoom 8        # stronger zoom
./example_crop --always        # also without scopes, for the daemon's Preview tab
```

Without the daemon and without scopes: run `python ../Tools/fake_daemon.py --save frames` first, then look at
`frames/P1.png` ... `frames/P4.png`.
