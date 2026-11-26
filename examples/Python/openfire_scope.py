"""openfire_scope.py - send a game's video to the OpenFIRE scopes through the OpenFIRE Scope Daemon.

One file, Python 3.8 or later, standard library only (numpy arrays are accepted too, but numpy is not needed).
Windows, Linux and macOS. Copy it next to your game.

    import openfire_scope as ofs

    scope = ofs.Scope()                       # opens the socket, starts the discovery
    # every game frame:
    scope.update()                            # never blocks
    for p in range(1, ofs.MAX_PLAYERS + 1):
        if scope.has_scope(p):
            pixels = render_scope_view(p)     # 240x240, e.g. pygame.image.tobytes(surface, "RGB")
            scope.send_frame(p, pixels, ofs.RGB888)
    scope.close()                             # at game end

See README.md next to this file.

MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE
"""
import random
import socket
import time

__all__ = ["Scope", "WIDTH", "HEIGHT", "MAX_PLAYERS", "DISCOVERY_PORT",
           "RGB888", "RGB565", "BGRX8888", "RGBA8888", "OK", "NO_SCOPE", "ERROR"]

WIDTH = HEIGHT = 240          # the scope image is always 240 x 240
MAX_PLAYERS = 4               # players 1..4
DISCOVERY_PORT = 47810        # default; the user can change it in the daemon settings

# Pixel layouts accepted by send_frame(). The first three go to the daemon as they are; RGBA8888 is converted to
# RGB888 by the library.
RGB888 = 0                    # 3 bytes: R, G, B (pygame "RGB", Pillow "RGB")
RGB565 = 1                    # 2 bytes: 16-bit little-endian, R in the 5 high bits
BGRX8888 = 2                  # 4 bytes: B, G, R, X (pygame "BGRA" surfaces, Windows bitmaps)
RGBA8888 = 3                  # 4 bytes: R, G, B, A (pygame "RGBA", Pillow "RGBA", OpenGL)

# Results of send_frame().
OK = 0
NO_SCOPE = 1                  # no scope for that player right now: nothing sent (not an error)
ERROR = -1                    # bad arguments or closed

_STRIPS, _STRIP_ROWS = 15, 16
_BPP = {RGB888: 3, RGB565: 2, BGRX8888: 4, RGBA8888: 4}
_WHOAMI_EVERY = 1.0           # discovery request, seconds
_ANSWER_TIMEOUT = 3.0         # no answer for so long: daemon gone, no scopes
_PRESENCE_EVERY = 0.2         # pause packet; the daemon forgets a source after 0.5 s


class Scope:
    """The connection of a game to the OpenFIRE Scope Daemon: discovery of the scopes, video, pause.

    Use it from one thread (normally the game loop). It can be used as a context manager: `with Scope() as s:`.
    """

    def __init__(self, discovery_port=DISCOVERY_PORT, always_send=False):
        #: True = send to the default ports 47811-47814 even without a scope (testing with the daemon's Preview tab
        #: or Tools/fake_daemon.py only)
        self.always_send = always_send
        #: True while the daemon answers the discovery
        self.daemon_seen = False
        #: video port of each player (index 1..4), 0 = no scope
        self.ports = [0] * (MAX_PLAYERS + 1)

        self._discovery = ("127.0.0.1", discovery_port or DISCOVERY_PORT)
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.bind(("127.0.0.1", 0))   # any free port: the answers come back here
        try:
            # 15 datagrams in a row always fit; on macOS it is also needed for datagrams over 9 KB
            self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 4 * 1024 * 1024)
        except OSError:
            pass
        self._sock.setblocking(False)
        self._frame = [0] * (MAX_PLAYERS + 1)
        self._format = [RGB888] * (MAX_PLAYERS + 1)
        self._paused = [False] * (MAX_PLAYERS + 1)
        self._next_presence = [0.0] * (MAX_PLAYERS + 1)
        self._next_whoami = 0.0
        self._last_answer = 0.0
        self._session = 0
        self.new_session()
        self.update()   # first discovery request at once

    # ------------------------------------------------------------------------------------------------ public

    def update(self):
        """Call once per game frame. Never blocks: reads the daemon's answers, asks again once a second, and sends
        the pause packet every 200 ms for the paused players."""
        if self._sock is None:
            return
        now = time.monotonic()

        # 1. answers of the daemon
        while True:
            try:
                data, addr = self._sock.recvfrom(2048)
            except (BlockingIOError, InterruptedError):
                break
            except ConnectionResetError:
                continue   # Windows: an earlier datagram found the daemon closed; this error is consumed
            except OSError:
                break
            if addr[1] != self._discovery[1]:
                continue   # only the daemon's answers
            self._parse_answer(data.decode("ascii", "replace"))
            self._last_answer = now
            self.daemon_seen = True
        if self.daemon_seen and now - self._last_answer > _ANSWER_TIMEOUT:   # daemon closed
            self.ports = [0] * (MAX_PLAYERS + 1)
            self.daemon_seen = False

        # 2. discovery request once a second: scopes can be paired, switched off or moved at any time
        if now >= self._next_whoami:
            self._send(b"WHOAMI", self._discovery[1])
            self._next_whoami = now + _WHOAMI_EVERY

        # 3. pause packets: the 16-byte header alone, strip 0xFF
        for p in range(1, MAX_PLAYERS + 1):
            if self._paused[p] and self.has_scope(p) and now >= self._next_presence[p]:
                self._send(self._header(self._format[p], self._frame[p], 0xFF, p), self._port_of(p))
                self._next_presence[p] = now + _PRESENCE_EVERY

    def has_scope(self, player):
        """True if player 1..4 has a paired scope right now (or always_send is on)."""
        return 1 <= player <= MAX_PLAYERS and (self.ports[player] != 0 or self.always_send)

    def send_frame(self, player, image, pixels=RGB888, stride=0, flip_y=False):
        """Sends one 240x240 image to the scope of a player.

        image:  bytes, bytearray, memoryview, array.array or a C-contiguous numpy array (any shape) with the pixels.
        pixels: RGB888, RGB565, BGRX8888 or RGBA8888.
        stride: bytes from one row to the next, 0 = tightly packed.
        flip_y: True when the first row in memory is the bottom of the image (OpenGL).
        Returns OK, NO_SCOPE (nothing sent) or ERROR. It also ends the pause of that player."""
        if self._sock is None or not 1 <= player <= MAX_PLAYERS or pixels not in _BPP:
            return ERROR
        try:
            src = memoryview(image).cast("B")
        except (TypeError, ValueError):
            return ERROR
        row = WIDTH * _BPP[pixels]
        stride = stride or row
        if stride < row or len(src) < stride * (HEIGHT - 1) + row:
            return ERROR
        if not self.has_scope(player):
            return NO_SCOPE

        # a tightly packed, top-down copy in the format sent to the daemon
        if stride == row and not flip_y:
            frame = src[:row * HEIGHT]
        else:
            order = range(HEIGHT - 1, -1, -1) if flip_y else range(HEIGHT)
            frame = memoryview(b"".join(src[y * stride:y * stride + row] for y in order))
        wire = pixels
        if pixels == RGBA8888:   # drop the alpha byte: RGBA -> RGB
            rgb = bytearray(WIDTH * HEIGHT * 3)
            rgb[0::3], rgb[1::3], rgb[2::3] = frame[0::4], frame[1::4], frame[2::4]
            frame, wire = memoryview(rgb), RGB888

        self._paused[player] = False
        self._format[player] = wire
        self._frame[player] = (self._frame[player] + 1) & 0xFFFFFFFF   # one number per frame and per player
        port = self._port_of(player)
        strip_bytes = WIDTH * _STRIP_ROWS * _BPP[wire]
        for strip in range(_STRIPS):
            payload = frame[strip * strip_bytes:(strip + 1) * strip_bytes]
            self._send(self._header(wire, self._frame[player], strip, player) + payload, port)
        return OK

    def set_paused(self, player, paused=True):
        """Game paused (menu, pause screen) for one player 1..4, or for all with player 0: the scope keeps the last
        image instead of showing NO SIGNAL. The next send_frame() of that player ends it."""
        for p in range(1, MAX_PLAYERS + 1):
            if player in (0, p):
                if paused and not self._paused[p]:
                    self._next_presence[p] = 0.0   # first packet at the next update()
                self._paused[p] = bool(paused)

    def new_session(self):
        """A new random session: call it when a brand-new match starts (optional; the constructor does it)."""
        s = 0
        while s in (0, self._session):
            s = random.getrandbits(32)
        self._session = s

    def close(self):
        """Closes the socket. The scopes show NO SIGNAL (or the daemon's screen capture) about 0.5 s later."""
        if self._sock is not None:
            self._sock.close()
            self._sock = None
        self.ports = [0] * (MAX_PLAYERS + 1)
        self.daemon_seen = False

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # ----------------------------------------------------------------------------------------------- private

    def _parse_answer(self, text):
        # "OF_SCOPE:P1;PORT=47811;FW=...\nOF_SCOPE:UNPAIRED\n..." -> ports
        ports = [0] * (MAX_PLAYERS + 1)
        for line in text.split("\n"):
            fields = line.strip().split(";")
            if len(fields[0]) != 11 or not fields[0].startswith("OF_SCOPE:P") or fields[0][10] not in "1234":
                continue
            for f in fields[1:]:
                if f.startswith("PORT=") and f[5:].isdigit() and 0 < int(f[5:]) < 65536:
                    ports[int(fields[0][10])] = int(f[5:])
        self.ports = ports

    def _port_of(self, player):
        return self.ports[player] or DISCOVERY_PORT + player

    def _header(self, fmt, frame, strip, player):
        s = self._session
        return bytes((0x4F, 0x46, 1, fmt,
                      frame & 255, frame >> 8 & 255, frame >> 16 & 255, frame >> 24 & 255,
                      strip, _STRIPS, player,
                      s & 255, s >> 8 & 255, s >> 16 & 255, s >> 24 & 255, 0))

    def _send(self, data, port):
        # errors are ignored on purpose: a lost strip only makes that frame incomplete
        try:
            self._sock.sendto(data, ("127.0.0.1", port))
        except OSError:
            pass
