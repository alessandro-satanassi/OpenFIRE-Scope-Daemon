#!/usr/bin/env python3
"""fake_daemon.py - a stand-in for the OpenFIRE Scope Daemon, to test a game without daemon, dongles or scopes.

It answers the discovery like the real daemon (as if a scope were paired for the chosen players), receives the
video on the same ports, over UDP and over TCP like the daemon, and checks every datagram (or every frame that comes
over TCP) with the same rules as the daemon. Once a second it prints, per player: complete frames per second,
incomplete frames, invalid datagrams and pause packets. With --save it also writes the last complete frame of each
player as a PNG (needs Pillow) or PPM file, so you can see exactly what the scope would receive.

    python fake_daemon.py                    # players 1-4, ports 47811-47814
    python fake_daemon.py --players 1,2      # only P1 and P2 have a "scope"
    python fake_daemon.py --save frames      # write frames/P1.png ... every second

Close the real daemon first: both want port 47810.

MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE
"""
import argparse
import os
import selectors
import socket
import struct
import time

W, H, STRIPS, ROWS = 240, 240, 15, 16
BPP = {0: 3, 1: 2, 2: 4}                # RGB888, RGB565, BGRX8888
NAMES = {0: "RGB888", 1: "RGB565", 2: "BGRX"}
RESTART_MS = 500                         # same as the daemon: no strip for 500 ms -> any frame number again


class Slot:
    def __init__(self, player, port):
        self.player, self.port = player, port
        self.session = None
        self.current = None
        self.mask = 0
        self.completed = False
        self.last_strip = 0.0
        self.strips = [None] * STRIPS
        self.format = None
        self.last_frame = None           # (format, list of strips) of the last complete frame
        self.complete = self.incomplete = self.invalid = self.presence = 0
        self.reasons = {}

    def bad(self, why):
        self.invalid += 1
        self.reasons[why] = self.reasons.get(why, 0) + 1

    def handle(self, p):
        # the same checks as UdpReceiver::handle() of the daemon
        if len(p) < 16 or p[0:2] != b"OF" or p[2] != 1:
            return self.bad("magic/version")
        fmt, frame, strip, strips, player, session = p[3], struct.unpack_from("<I", p, 4)[0], p[8], p[9], p[10], \
            struct.unpack_from("<I", p, 11)[0]
        presence = strip == 0xFF
        if fmt not in BPP:
            return self.bad("format %d" % fmt)
        if strips != STRIPS:
            return self.bad("strips %d" % strips)
        if player != self.player:
            return self.bad("player %d on the port of P%d" % (player, self.player))
        if not presence and strip >= STRIPS:
            return self.bad("strip %d" % strip)
        want = 16 if presence else 16 + W * ROWS * BPP[fmt]
        if len(p) != want:
            return self.bad("length %d instead of %d" % (len(p), want))
        if presence:
            self.presence += 1
            return
        now = time.monotonic() * 1000
        restart = self.current is None or session != self.session or now - self.last_strip > RESTART_MS
        self.last_strip = now
        if restart:
            if self.current is not None and not self.completed and self.mask:
                self.incomplete += 1
            self.session, self.current, self.mask, self.completed = session, frame, 0, False
        else:
            diff = (frame - self.current + 2**31) % 2**32 - 2**31
            if diff < 0:
                return                   # older frame: ignored
            if diff > 0:
                if not self.completed and self.mask:
                    self.incomplete += 1
                self.current, self.mask, self.completed = frame, 0, False
            elif self.completed or self.mask & (1 << strip):
                return                   # duplicate
        self.strips[strip] = (fmt, bytes(p[16:]))
        self.mask |= 1 << strip
        if self.mask == (1 << STRIPS) - 1:
            self.completed = True
            self.complete += 1
            self.last_frame = list(self.strips)


class TcpClient:
    """One TCP connection: whole frames (16-byte header + 240 x 240 pixels) or a pause header (strip 0xFF),
    cut into the same 15 strips as UDP and checked by the same Slot.handle(), as the daemon does."""

    def __init__(self, sock, slot):
        self.sock, self.slot, self.buf = sock, slot, bytearray()

    def feed(self, data):
        """False = header not valid: the stream cannot go on, the connection is closed (as the daemon)."""
        self.buf += data
        while len(self.buf) >= 16:
            h = bytes(self.buf[:16])
            if h[0:2] != b"OF" or h[2] != 1 or h[3] not in BPP:
                self.slot.bad("TCP header not valid, connection closed")
                return False
            if h[8] == 0xFF:
                del self.buf[:16]
                self.slot.handle(h)
                continue
            strip_bytes = W * ROWS * BPP[h[3]]
            if len(self.buf) < 16 + STRIPS * strip_bytes:
                break
            for k in range(STRIPS):
                part = self.buf[16 + k * strip_bytes:16 + (k + 1) * strip_bytes]
                self.slot.handle(h[:8] + bytes((k,)) + h[9:] + part)
            del self.buf[:16 + STRIPS * strip_bytes]
        return True


def to_rgb(strips):
    out = bytearray()
    for fmt, data in strips:
        if fmt == 0:
            out += data
        elif fmt == 2:
            for i in range(0, len(data), 4):
                out += bytes((data[i + 2], data[i + 1], data[i]))
        else:
            for i in range(0, len(data), 2):
                v = data[i] | (data[i + 1] << 8)
                r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
                out += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    return bytes(out)


def save(folder, slot):
    rgb = to_rgb(slot.last_frame)
    try:
        from PIL import Image
        Image.frombytes("RGB", (W, H), rgb).save(os.path.join(folder, "P%d.png" % slot.player))
    except ImportError:
        with open(os.path.join(folder, "P%d.ppm" % slot.player), "wb") as f:
            f.write(b"P6 240 240 255\n" + rgb)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--players", default="1,2,3,4", help="players with a 'scope', e.g. 1,3")
    ap.add_argument("--discovery", type=int, default=47810, help="discovery port (default 47810)")
    ap.add_argument("--first-port", type=int, default=47811, help="video port of P1 (P2 = +1 ...)")
    ap.add_argument("--save", metavar="FOLDER", help="write the last frame of each player every second")
    args = ap.parse_args()
    players = [int(x) for x in args.players.split(",") if x.strip()]

    sel = selectors.DefaultSelector()
    disc = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    disc.bind(("127.0.0.1", args.discovery))
    disc.setblocking(False)
    sel.register(disc, selectors.EVENT_READ, None)
    tcp_disc = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp_disc.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    tcp_disc.bind(("127.0.0.1", args.discovery))
    tcp_disc.listen(4)
    tcp_disc.setblocking(False)
    sel.register(tcp_disc, selectors.EVENT_READ, ("listen", None))
    clients = {}                         # player -> TcpClient: one at a time, a new one replaces it
    slots = {}
    for p in range(1, 5):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 * 1024 * 1024)
        s.bind(("127.0.0.1", args.first_port + p - 1))
        s.setblocking(False)
        slots[p] = Slot(p, args.first_port + p - 1)
        sel.register(s, selectors.EVENT_READ, slots[p])
        t = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        t.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        t.bind(("127.0.0.1", args.first_port + p - 1))
        t.listen(4)
        t.setblocking(False)
        sel.register(t, selectors.EVENT_READ, ("listen", slots[p]))
    if args.save:
        os.makedirs(args.save, exist_ok=True)
    print("fake daemon: discovery on 127.0.0.1:%d (UDP and TCP), scopes for P%s"
          % (args.discovery, ",P".join(map(str, players))))

    def answer():
        lines = ["OF_SCOPE:P%d;PORT=%d;FW=fake" % (p, slots[p].port) for p in players]
        return "\n".join(lines or ["OF_SCOPE:NONE"]).encode()

    def drop(sock):
        sel.unregister(sock)
        sock.close()

    last = time.monotonic()
    prev = {p: (0, 0, 0, 0) for p in slots}
    while True:
        for key, _ in sel.select(timeout=0.1):
            sock, slot = key.fileobj, key.data
            if isinstance(slot, tuple):              # TCP: a listener, a video client or a discovery client
                kind, owner = slot
                if kind == "listen":
                    conn, _ = sock.accept()
                    conn.setblocking(False)
                    if owner is None:
                        sel.register(conn, selectors.EVENT_READ, ("whoami", None))
                    else:
                        if owner.player in clients:
                            drop(clients[owner.player].sock)
                        clients[owner.player] = TcpClient(conn, owner)
                        sel.register(conn, selectors.EVENT_READ, ("video", clients[owner.player]))
                    continue
                try:
                    data = sock.recv(1 << 20)
                except (BlockingIOError, InterruptedError):
                    continue
                except OSError:
                    data = b""
                if kind == "whoami":
                    if not data:
                        drop(sock)
                    elif b"WHOAMI" in [x.strip() for x in data.split(b"\n")]:
                        sock.setblocking(True)
                        sock.sendall(answer())
                        drop(sock)                   # the end of the answer, as the daemon
                    continue
                if not data or not owner.feed(data):
                    if clients.get(owner.slot.player) is owner:
                        del clients[owner.slot.player]
                    drop(sock)
                continue
            while True:
                try:
                    data, addr = sock.recvfrom(65536)
                except BlockingIOError:
                    break
                if slot is None:
                    if data.strip() == b"WHOAMI":
                        sock.sendto(answer(), addr)
                else:
                    slot.handle(data)
        now = time.monotonic()
        if now - last >= 1.0:
            dt, last = now - last, now
            parts = []
            for p, s in slots.items():
                c, i, v, pr = s.complete - prev[p][0], s.incomplete - prev[p][1], s.invalid - prev[p][2], \
                    s.presence - prev[p][3]
                prev[p] = (s.complete, s.incomplete, s.invalid, s.presence)
                if c or i or v or pr:
                    fmt = NAMES.get(s.last_frame[0][0], "?") if s.last_frame else "-"
                    parts.append("P%d %5.1f fps %s | incomplete %d invalid %d pause %d" % (p, c / dt, fmt, i, v, pr))
                    if args.save and s.last_frame:
                        save(args.save, s)
                for why, n in s.reasons.items():
                    parts.append("P%d invalid: %s (x%d)" % (p, why, n))
                s.reasons.clear()
            print(time.strftime("%H:%M:%S"), " || ".join(parts) if parts else "no video")


if __name__ == "__main__":
    main()
