# MAME: Silent Scope on the OpenFIRE scope

`openfire_scope` is a MAME plugin (Lua, no extra files) for games whose arcade board draws the view through the rifle
scope on a screen of its own. It was made for **Silent Scope** and **Silent Scope 2** (Konami Hornet hardware): MAME
emulates the scope monitor of the cabinet as a second screen, `rscreen` (768 x 236 pixels, next to the main screen
`lscreen`). Every emulated frame the plugin cuts a square out of that screen, scales it to 240 x 240 and sends it
to the OpenFIRE Scope Daemon, which sends it to the scope of player 1.

The plugin uses **TCP**: MAME's Lua can open TCP connections but not UDP. The daemon accepts both on the same ports
(see "TCP" in the [main README](../README.md#tcp-for-programs-without-udp)).

Note: MAME currently marks these games as *not working*; how well they run depends on the MAME version and the PC.

## Install

1. Copy the folder `openfire_scope` (with `init.lua` and `plugin.json`) into the `plugins` folder of MAME.
2. Turn it on, in one of these ways:
   - on the command line: `mame sscope -plugin openfire_scope`
   - or in MAME: **Configure Options > Plugins**, turn on *OpenFIRE scope*, then exit MAME and start it again
     (MAME writes this into `plugin.ini`).
3. Start the OpenFIRE Scope Daemon **before** the game, and pair the scope as usual.

With any other game the plugin does nothing. If the daemon is not running when the game starts, the plugin writes
a message in MAME's console and does nothing until the next reset of the game (**F3**): start the daemon, then press
F3. On Windows a connection attempt to a daemon that is not running can take about 2 seconds, so the plugin does not
keep trying during the game.

On Linux and macOS, close MAME before the daemon: if the daemon closes while the game is sending, the system ends
MAME too (MAME does not ignore the "broken connection" signal, and a Lua plugin cannot change that). On Windows
nothing happens: the plugin stops sending until the next reset.

## The scope screen must stay on screen

MAME draws an emulated screen only while some window shows it: a scope screen that no window shows stays frozen,
and so does the image on the OpenFIRE scope. The default view of Silent Scope shows the two screens side by side.
To have the main screen alone on your monitor (as the lightgun needs), give MAME two windows:

```
mame sscope -plugin openfire_scope -numscreens 2
```

With two windows MAME gives each screen a window of its own: window 0 shows the main screen and window 1 the scope
screen. With a second monitor, window 1 can go there (MAME's `-screen1` option chooses the monitor); with one monitor,
add `-window` and keep the second window small and out of the way. Check it once: the image on the OpenFIRE scope
must move.

## Settings

At the top of `init.lua`:

| Setting | Default | Meaning |
| --- | --- | --- |
| `player` | `1` | the OpenFIRE player whose scope gets the picture |
| `screens` | `{ ':rscreen' }` | the screens to look for; the first one found is used |
| `crop_width`, `crop_height` | `576`, `nil` | the square cut out of the screen, centred, in emulated pixels (`nil` = the whole width or height). The scope screen is 768 x 236 pixels on a 4:3 monitor, so 576 x 236 is the square in the middle |
| `offset_x`, `offset_y` | `0`, `0` | moves the square right / down |
| `send_every` | `2` | sends one emulated frame out of this many: 2 = about 30 frames per second, as many as the radio link of the scope carries in practice |
| `discovery_port` | `47810` | as in the daemon settings |

The game draws its own reticle on the scope screen, so the plugin draws nothing.

## What it does, in short

- Once a second it asks the daemon which players have a scope (`WHOAMI` over TCP on port 47810), as every game
  does. Nothing is made or sent while player 1 has no scope.
- Every frame (or every `send_every` frames) it reads the pixels of the scope screen (`screen:pixels()`, 32-bit
  pixels that are BGRX on a PC), picks the nearest pixel for each of the 240 x 240 points with `string.unpack` and
  `string.pack` (both in C) and writes one TCP message: the 16-byte header and the 240 x 240 pixels, format BGRX8888.
  It costs a few milliseconds of Lua per sent frame.
- While MAME is paused it sends the 16-byte pause header every 200 ms: the scope keeps the last image.
- When MAME exits, the connection closes and after half a second the scope shows the daemon's screen capture
  (Windows) or NO SIGNAL.

## Testing without MAME and without scopes

`python ../Tools/fake_daemon.py --players 1 --save frames` accepts TCP like the daemon, so it shows whether the
plugin sends (about 30 fps, no invalid packets, pause packets during a pause of MAME) and `frames/P1.png` is the image
the scope would receive.
