# Tools

## `fake_daemon.py`

A stand-in for the OpenFIRE Scope Daemon, to test a game without the daemon, without dongles and without scopes.
Python 3, standard library only (Pillow only to write PNG files; without it the frames are written as PPM).

It answers the discovery like the real daemon, as if the chosen players had a paired scope, receives the video on
the same ports, over UDP and over TCP like the daemon, and checks every datagram (or every frame that comes over
TCP) with the same rules as the daemon. Once a second it prints, for each player: complete frames per second, the
pixel format, incomplete frames, invalid datagrams (with the reason) and pause packets.

```
python fake_daemon.py                    # players 1-4, video ports 47811-47814
python fake_daemon.py --players 1,2      # only P1 and P2 have a "scope"
python fake_daemon.py --save frames      # also writes frames/P1.png ... once a second
```

```
fake daemon: discovery on 127.0.0.1:47810, scopes for P1,P2,P3,P4
10:42:07 P1  60.0 fps RGB888 | incomplete 0 invalid 0 pause 0 || P2  60.0 fps RGB888 | incomplete 0 invalid 0 pause 0
10:42:08 P1   0.0 fps RGB888 | incomplete 0 invalid 0 pause 5 || P2   0.0 fps RGB888 | incomplete 0 invalid 0 pause 5
```

- **fps** should match the rate your game sends (up to 60).
- **incomplete** should stay at 0: a frame is shown only when all its 15 strips arrive.
- **invalid** must be 0; the reason printed next to it says what is wrong (length, player, format...).
- **pause** is about 5 per second while your game is in its pause menu.
- With `--save`, `frames/P1.png` is exactly the image the scope of player 1 would receive: check that it is upright
  and that the colours are right (red and blue swapped means a wrong pixel format).

Close the real daemon first: both want port 47810. `--discovery` and `--first-port` change the ports.
