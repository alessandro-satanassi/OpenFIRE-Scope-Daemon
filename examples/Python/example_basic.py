#!/usr/bin/env python3
"""example_basic.py - the smallest complete "game" for the OpenFIRE scopes, standard library only.

It pans a 240x240 window across a landscape, draws a moving target and a reticle, and sends the image at 60 fps to
every player that has a scope. From 10 s to 13 s it "pauses the game" (the scopes keep the last image), then plays
again; at the end it closes (the scopes show NO SIGNAL).

    python example_basic.py                     # 60 fps, 20 s
    python example_basic.py --always            # also without scopes: watch the daemon's Preview tab
    python example_basic.py --fps 30 --seconds 60

MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE
"""
import argparse
import math
import time

import openfire_scope as ofs

W, H = ofs.WIDTH, ofs.HEIGHT
PANO_W = 960   # the landscape is wider than the view and wraps around


def make_landscape():
    """A 960x240 RGB panorama, made once at start (pure Python: plain per-pixel loops are too slow per frame)."""
    out = bytearray(PANO_W * H * 3)
    for x in range(PANO_W):
        a = 2 * math.pi * x / PANO_W   # periodic, so that the wrap is invisible
        hill = int(130 + 25 * math.sin(3 * a) + 10 * math.sin(11 * a))
        for y in range(H):
            i = (y * PANO_W + x) * 3
            if y < hill:
                out[i:i + 3] = bytes((90 + y // 3, 150 + y // 4, 230))
            else:
                stripe = 15 if (x // 6 + y // 6) % 2 else 0
                out[i:i + 3] = bytes((40 + stripe, 125 - (y - 130) // 3, 40))
    return bytes(out)


def render_scope_view(landscape, player, t):
    """In a real game: a second camera with a narrow field of view, looking where the gun aims (see ../README.md).
    Here: a window that pans across the landscape, plus a target and the reticle."""
    pan = int(t * 40 + player * 240) % PANO_W
    row = W * 3
    frame = bytearray(W * H * 3)
    for y in range(H):   # copy each row of the window, wrapping around the panorama
        start = y * PANO_W * 3 + pan * 3
        end = y * PANO_W * 3 + PANO_W * 3
        part = landscape[start:min(end, start + row)]
        frame[y * row:y * row + len(part)] = part
        if len(part) < row:
            frame[y * row + len(part):(y + 1) * row] = landscape[y * PANO_W * 3:y * PANO_W * 3 + row - len(part)]
    # target: a red 24x24 square moving left and right
    tx, ty = int(120 + 70 * math.sin(t * 1.3 + player)) - 12, int(125 + 25 * math.cos(t * 0.9)) - 12
    red = b"\xe6\x28\x1e" * 24
    for y in range(ty, ty + 24):
        frame[(y * W + tx) * 3:(y * W + tx + 24) * 3] = red
    # reticle: the scope draws none, the game draws its own
    black = b"\x00" * 3
    for d in range(8, 70):
        for x, y in ((120 + d, 120), (120 - d, 120), (120, 120 + d), (120, 120 - d)):
            frame[(y * W + x) * 3:(y * W + x) * 3 + 3] = black
    return frame


def main():
    ap = argparse.ArgumentParser(description="A small OpenFIRE scope example.")
    ap.add_argument("--fps", type=int, default=60)
    ap.add_argument("--seconds", type=float, default=20)
    ap.add_argument("--always", action="store_true", help="send even without scopes (daemon Preview tab)")
    args = ap.parse_args()

    landscape = make_landscape()
    with ofs.Scope(always_send=args.always) as scope:
        start = next_frame = time.monotonic()
        last_print = 0.0
        while True:
            now = time.monotonic()
            t = now - start
            if t >= args.seconds:
                break
            paused = 10 <= t < 13   # the "pause menu"

            scope.update()   # discovery + pause packets, never blocks
            for p in range(1, ofs.MAX_PLAYERS + 1):
                if not scope.has_scope(p):
                    continue   # no scope for that player: render nothing
                if paused:
                    scope.set_paused(p)   # the scope keeps the last image
                else:
                    scope.send_frame(p, render_scope_view(landscape, p, t), ofs.RGB888)

            if now - last_print >= 1:
                last_print = now
                players = " ".join("P%d=%s" % (p, "yes" if scope.ports[p] else "-") for p in range(1, 5))
                print("%5.1f s  daemon %s  scopes: %s%s" % (t, "yes" if scope.daemon_seen else "no ", players,
                                                          "  (paused)" if paused else ""), flush=True)

            next_frame += 1 / args.fps   # fixed frame rate; a real game calls this from its own loop
            delay = next_frame - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            else:
                next_frame = time.monotonic()
    # leaving "with" closed the socket: the scopes show NO SIGNAL about 0.5 s later


if __name__ == "__main__":
    main()
