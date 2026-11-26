# OpenFIRE scopes: examples for game developers

An OpenFIRE scope is a small round display (240 x 240 pixels) mounted on an OpenFIRE lightgun. Your game can show
on it what the player sees through the scope: a zoomed view of the point the gun is aiming at.

The game does not talk to the scopes directly. It sends plain images over UDP (or TCP, for programs that have only
TCP), on the same PC, to the **OpenFIRE Scope Daemon**. The daemon compresses them and sends them through a USB
dongle to the scope of each player.

```
 your game  --UDP 127.0.0.1-->  OpenFIRE Scope Daemon  --USB-->  dongle  --Wi-Fi-->  scope on the lightgun
 (240x240 images,                (compression, bandwidth,                            (decodes, rotates
  one stream per player)          pause, up to 4 players)                             with the gun roll)
```

This folder has ready-to-use code for the most common languages and engines. Pick yours, copy one or two files into
your game, and read the README in that folder.

| Folder | For | What is inside |
| --- | --- | --- |
| [`C`](C) | C, and every engine that can call C | `openfire_scope.h`, a single-file library (C99, no dependencies), and a complete small example |
| [`CPP`](CPP) | C++17 | a small class over the C library, and an example |
| [`CSharp`](CSharp) | C#, .NET, MonoGame, Stride, Unity | `OpenFIREScope.cs` (.NET Standard 2.0, no dependencies), and a console example |
| [`Unity`](Unity) | Unity 2021.3 or later | a component that renders a scope camera and sends it, plus an aiming helper |
| [`Godot`](Godot) | Godot 4.1 or later (GDScript) | a library, a sender node for a `SubViewport`, an aiming camera, and an example project |
| [`Unreal`](Unreal) | Unreal Engine 5 (C++, Blueprints) | an actor component with a `SceneCaptureComponent2D`, and a client on Unreal's sockets |
| [`Python`](Python) | Python 3.8 or later, pygame | `openfire_scope.py` (standard library only, numpy optional), and two examples |
| [`JavaScript`](JavaScript) | Node.js 16 or later, Electron | `openfire-scope.js` (no dependencies), and an example |
| [`Lua`](Lua) | Lua 5.1 to 5.4, LuaJIT, LÖVE 11 | `openfire_scope.lua` (LuaSocket), and examples for plain Lua and for LÖVE |
| [`MAME`](MAME) | MAME, Silent Scope 1 and 2 | a Lua plugin that sends the scope screen of the game (over TCP) |
| [`Tools`](Tools) | everyone | `fake_daemon.py`: test your game without the daemon and without any hardware |

Every library does the same things with the same names (adapted to the style of each language), so what you read here
is valid for all of them.

## The whole job in five steps

1. **At game start** open a UDP socket and start the discovery (`open`).
2. **Every game frame** call `update`. It asks the daemon once a second which players have a scope, and never blocks.
3. **For every player that has a scope** (`has_scope`) render a 240 x 240 image of the scope view and send it
   (`send_frame`). Players without a scope cost nothing: render nothing for them.
4. **In the pause menu** call `set_paused`. The scope keeps the last image instead of showing NO SIGNAL. The next
   `send_frame` ends the pause.
5. **At game end** close the socket (`close`), or simply stop sending. After about half a second the scope shows the
   daemon's screen capture (Windows) or NO SIGNAL.

That is all. Compression, bandwidth, frame rate on the radio link, lost packets and the gun roll are the daemon's and
the scope's job.

## What to show on the scope

- **The view through the scope.** The usual way is a second camera at the player's eye (or at the gun), looking along
  the ray that goes through the aim point on the screen, with a narrow field of view: 5 to 15 degrees gives a 4x to 10x
  zoom compared with a 60-degree game camera. Render it into a 240 x 240 target. A cheaper way for 2D games is to cut a
  square around the aim point out of your own frame and scale it up.
- **Upright.** Render the image as if the gun were held straight. The scope knows the roll angle of the gun and
  rotates the image by itself, so the horizon stays level when the player tilts the gun.
- **Your own reticle.** The scope draws nothing over the image: draw the crosshair (or any reticle you like) in the
  middle of your image. The centre is pixel (119.5, 119.5).
- **Round.** Only the circle inscribed in the square is visible. Keep the important things inside it; the corners are
  never shown (and they cost no bandwidth).
- **What the player expects.** Darken the image when the player is hit, show the zoom level, a range-finder or the
  ammunition around the edge of the circle: it is your image.

## Images

- **Size:** always 240 x 240 pixels, first row at the top. If your image is upside down in memory (OpenGL
  `glReadPixels`, Unity read-backs), every library has a `flip_y` option.
- **Pixel layouts:** RGB888, RGB565 (little-endian) and BGRX8888 go to the daemon as they are; RGBA8888 is converted
  to RGB888 by the library. Send what you already have in memory: RGB888 or BGRX give the best quality, RGB565 is for
  games that already render in 16 bits.
- **Frame rate:** send one image per game frame, up to 60 per second. If your game runs faster, send every second
  frame. The daemon always takes the newest image and sends to the scope 30 to 60 per second, depending on the
  movement and on the radio link. A still scene costs nothing extra: just keep sending.
- **Rows with padding:** GPU read-backs often add bytes at the end of each row. Pass the `stride` (bytes from one row
  to the next) and the library skips them.

## Pause, menus and the end of the game

| The game ... | Do | The scope shows |
| --- | --- | --- |
| plays | `send_frame` every frame | your images |
| is in a pause menu | `set_paused(player, true)` and keep calling `update` | the last image, frozen |
| goes back to playing | `send_frame` again | your images |
| starts a brand-new match | `new_session` (optional), then `send_frame` | your images |
| closes, or stops sending | `close`, or nothing at all | after about 0.5 s the daemon's screen capture (Windows) or NO SIGNAL |

The pause lasts only while the game keeps calling `update` (it sends a small packet every 200 ms). If the game
freezes or crashes during a pause, the scope shows NO SIGNAL about a second later instead of a frozen image.

## Games you cannot change: MAME, emulators, existing PC games

You do not need any code for a game that cannot be changed. On **Windows** the daemon can capture the screen
around the aim point of each lightgun and send that to the scope: a zoomed view of where the player is aiming.

How to turn it on, in the daemon: **Settings > Screen capture**, then the menu of the tray icon.

| Setting | What to choose |
| --- | --- |
| *Allow the screen capture* | On (it is on by default). Without it the capture never runs. |
| *Start the capture by itself when a player has no video from the game for 500 ms* | On, if the capture must start by itself. Off by default: then tick **Screen capture on** in the menu of the tray icon when you want it (it is off at every start of the daemon), or send `CAPTURE;ON` (see the protocol below). |
| *Game monitor* | The monitor the game runs on, and the one the lightguns are calibrated on. |
| *Zoom* | 1x to 4x (default 2x): the daemon cuts a square of 240 / zoom pixels around the aim point and enlarges it to 240 x 240. |
| *Lightguns (VID:PID → player)* | Which lightgun is which player. The default is the OpenFIRE lightguns, `F143:0001` to `F143:0004` for players 1 to 4; add a row for any other lightgun. |

- The aim point comes from the lightguns themselves (Windows Raw Input), so it works even when the daemon is in the
  background and with any game. The lightgun must move the mouse in **absolute** mode, the usual mode of OpenFIRE.
- A game that sends its own video always wins: the capture is used only for a player whose game sends nothing over
  UDP (and is not paused). The daemon's window and the discovery answer then show the source `DXGI`.
- The capture sees what is on the screen, including the crosshair drawn by the game or the emulator. For the cleanest
  view, hide the game's crosshair if the game or the emulator allows it.
- `--no-capture` on the daemon's command line turns it off for one run. On Linux and macOS there is no capture: only
  games that send their own video.

### Silent Scope and other games with a real scope screen

- **Versions that draw the scope on the main screen** (the PlayStation 2 versions in PCSX2, for example) show the
  zoomed lens around the aim point: the capture around the aim point picks it up. Try zoom 1x first, so that the
  daemon does not enlarge an image the game has already enlarged.
- **MAME, Silent Scope 1 and 2 (Konami Hornet hardware):** the arcade board draws the scope picture on a second
  screen of its own, and MAME emulates it as a second screen (tag `rscreen`, 768 x 236 pixels, next to the main
  screen `lscreen`). The capture around the aim point does not show that picture: the MAME plugin in
  [`MAME`](MAME) reads the second screen and sends it to the daemon over TCP. Note also that MAME currently marks
  these games as not working.

## Testing

- **Without the daemon and without hardware:** run `python Tools/fake_daemon.py`. It answers the discovery as if all
  four players had a scope, checks every packet with the same rules as the daemon and prints, once a second, the frames
  per second it receives and any error. `--save frames` writes the last image of each player as a PNG file, so you see
  exactly what the scope would receive. It accepts TCP too, like the daemon. Close the real daemon first: both use
  port 47810.
- **With the daemon but without scopes:** turn on the `always_send` option of the library. The images go to the
  default ports 47811 to 47814 even if no scope is paired, and you can watch them in the daemon's **Preview** tab.
- **With the scopes:** pair them (they pair by themselves with any free dongle), start the game and look. The daemon's
  window shows, per player, the source (`UDP` while your game is sending, `PAUSE` during a pause), the frames per
  second and the quality.
- **The daemon's own test program:** `of_test_sender`, built with the daemon, sends test images like a game would.
  Useful to check that the daemon and the scopes work before you test your game.

## The protocol, for those who write their own library

You do not need this if you use one of the libraries in this folder. Everything is little-endian, everything goes to
and from `127.0.0.1` only.

### Discovery

Send the text `WHOAMI` (6 bytes, no terminator) to UDP port **47810** (the user can change it in the daemon
settings). The daemon answers to the address and port the request came from, with one line per dongle, separated by
`\n`:

```
OF_SCOPE:P1;PORT=47811;FW=1.0;CH=36;SRC=UDP;FPS=42;Q=61;C=520;DROP=0;QFULL=0;CAP=0
OF_SCOPE:P3;PORT=47813;FW=1.0;CH=149;SRC=NONE;FPS=0;Q=0;C=450;DROP=0;QFULL=0;CAP=0
OF_SCOPE:UNPAIRED;FW=1.0;CH=44
```

- `OF_SCOPE:Pn;PORT=x` : player n (1 to 4) has a paired scope; send its video to UDP port x. **Always use the port
  of the answer.** It is 47810 + n by default, but the daemon takes other ports when those are busy.
- `OF_SCOPE:UNPAIRED` : a dongle without a scope. `OF_SCOPE:NONE` : no dongle at all.
- Ignore the other fields (they are for diagnostics and more may be added): `SRC` is what the scope is showing now
  (`NONE`, `UDP`, `PAUSE`, `DXGI` screen capture, `CLIP` test clip), `FPS` the frames per second sent to the scope,
  `Q` the compression quality (1 to 100), `C` the estimated bandwidth in KB/s, `DROP` and `QFULL` frames per second
  dropped on the way, `CAP` 1 while the screen capture of the daemon can run (Windows), 0 otherwise.
- To the same port, a program can also send `CAPTURE;ON` and `CAPTURE;OFF`: they tick and untick *Screen capture on*
  in the daemon (Windows), used for the players without video from the game. No answer. `CAPTURE;ON` is ignored while
  the user does not allow the capture; while the automatic start is on, the capture starts by itself anyway.

Ask again every second: scopes can be paired, switched off or moved to another player at any time. If the daemon has
not answered for 3 seconds, consider every scope gone.

### Video

One image = 15 UDP datagrams ("strips") of 16 rows each, sent to the port of that player. Each datagram is a 16-byte
header followed by the pixels of its 16 rows, top to bottom, left to right, no padding:

| Offset | Size | Field | Value |
| --- | --- | --- | --- |
| 0 | 2 | magic | `'O'`, `'F'` |
| 2 | 1 | version | 1 |
| 3 | 1 | format | 0 = RGB888, 1 = RGB565 little-endian, 2 = BGRX8888 |
| 4 | 4 | frame | frame number, +1 for every new image (u32, may wrap) |
| 8 | 1 | strip | 0 to 14, or 0xFF for a pause packet |
| 9 | 1 | strips | always 15 |
| 10 | 1 | player | 1 to 4, the same as the port |
| 11 | 4 | session | any random non-zero u32, the same for the whole run |
| 15 | 1 | reserved | 0 |

The datagram must have exactly the right length: 16 + 240 x 16 x 3 = **11536** bytes for RGB888, **7696** for
RGB565, **15376** for BGRX8888. Anything else is dropped.

- The daemon shows an image only when all 15 strips of the same frame have arrived. A strip of an older frame is
  ignored, a strip of a newer frame starts a new image.
- A new session value, or no strips for 500 ms, starts from scratch: then any frame number is accepted.
- Use a separate frame counter for each player.
- On macOS a datagram larger than 9216 bytes needs a larger send buffer: set `SO_SNDBUF` to at least 64 KB (the
  libraries set 4 MB). On Windows turn off `SIO_UDP_CONNRESET`, otherwise a datagram sent while the daemon is closed
  makes the next receive fail.

### Pause

While the game is paused, send every 200 ms only the 16-byte header, with strip = 0xFF (same format, player and
session; the frame number is not checked). The daemon forgets a source after 500 ms without packets.

### TCP for programs without UDP

For programs that can open TCP connections but not UDP sockets (a MAME plugin, for example). The UDP stays the
normal way; the daemon accepts both on the same ports, always on `127.0.0.1`.

- **Discovery:** connect to TCP port 47810 and write `WHOAMI`. The daemon writes the same answer as over UDP, then
  closes the connection: read until it closes. `SUSPEND`, `RESUME`, `CAPTURE;ON` and `CAPTURE;OFF` work too. One request per line (or per write).
- **Video:** connect to the `PORT` of the answer (TCP, the same number as the UDP port) and write, for every image,
  the same 16-byte header followed by **all** the 240 x 240 pixels in one go: 16 + 240 x 240 x 3 = **172816** bytes
  for RGB888, **115216** for RGB565, **230416** for BGRX8888. There is no length field: the length comes from the
  format. The strip byte is 0 (any value except 0xFF); the daemon cuts the image into the 15 strips itself, and from
  there on everything works as for UDP (frame number, session, player).
- **Pause:** the 16-byte header alone, with strip = 0xFF, every 200 ms, as over UDP.
- One connection per player at a time: a new connection replaces the old one. A header that is not valid (magic,
  version or format) closes the connection, since the stream cannot go on after it.

## License

The examples in this folder are released under the MIT license (see [LICENSE](LICENSE)), so you can copy them into
any game, open or closed source. The daemon itself is GPL v3; your game only talks to it over UDP or TCP.
