/*  openfire-scope.js - send a game's video to the OpenFIRE scopes through the OpenFIRE Scope Daemon.

    Node.js 16 or later, no dependencies (only the built-in "dgram" module). Works in Electron's main process and in
    NW.js. A web page in a normal browser cannot send UDP: see README.md for how to do it from Electron.

        const { Scope, Pixels } = require('./openfire-scope');
        const scope = new Scope();                    // opens the socket, starts the discovery
        // every game frame:
        scope.update();                               // never blocks
        for (let p = 1; p <= 4; p++)
            if (scope.hasScope(p)) scope.sendFrame(p, renderScopeView(p), Pixels.RGBA8888);
        scope.close();                                // at game end

    See README.md next to this file.

    MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE
*/
'use strict';

const dgram = require('dgram');
const crypto = require('crypto');

const WIDTH = 240, HEIGHT = 240;          // the scope image is always 240 x 240
const MAX_PLAYERS = 4;                    // players 1..4
const DISCOVERY_PORT = 47810;             // default; the user can change it in the daemon settings

/** How the image is laid out in memory. */
const Pixels = Object.freeze({
    RGB888: 0,        // 3 bytes: R, G, B
    RGB565: 1,        // 2 bytes: 16-bit little-endian, R in the 5 high bits
    BGRX8888: 2,      // 4 bytes: B, G, R, X
    RGBA8888: 3,      // 4 bytes: R, G, B, A - canvas ImageData, WebGL readPixels (sent as RGB888)
});

/** Results of sendFrame(). */
const Result = Object.freeze({ OK: 0, NO_SCOPE: 1, ERROR: -1 });

const STRIPS = 15, STRIP_ROWS = 16, HEADER = 16;
const BPP = [3, 2, 4, 4];
const WHOAMI_EVERY_MS = 1000;             // discovery request
const ANSWER_TIMEOUT_MS = 3000;           // no answer for so long: daemon gone, no scopes
const PRESENCE_EVERY_MS = 200;            // pause packet; the daemon forgets a source after 500 ms
const LOCALHOST = '127.0.0.1';

const nowMs = () => Number(process.hrtime.bigint() / 1000000n);   // monotonic

class Scope {
    /**
     * Opens the UDP socket and sends the first discovery request.
     * @param {object} [options]
     * @param {number} [options.discoveryPort=47810]
     * @param {boolean} [options.alwaysSend=false] send to the default ports 47811-47814 even without a scope
     *        (testing with the daemon's Preview tab or Tools/fake_daemon.py only)
     */
    constructor(options = {}) {
        this.discoveryPort = options.discoveryPort || DISCOVERY_PORT;
        this.alwaysSend = !!options.alwaysSend;
        /** video port of each player (index 1..4), 0 = no scope */
        this.ports = new Array(MAX_PLAYERS + 1).fill(0);
        /** true while the daemon answers the discovery */
        this.daemonSeen = false;

        this._frame = new Uint32Array(MAX_PLAYERS + 1);
        this._format = new Array(MAX_PLAYERS + 1).fill(Pixels.RGB888);
        this._paused = new Array(MAX_PLAYERS + 1).fill(false);
        this._nextPresence = new Array(MAX_PLAYERS + 1).fill(0);
        this._nextWhoami = 0;
        this._lastAnswer = 0;
        this._session = 0;
        this._closed = false;
        this._pool = new Map();   // datagram length -> free Buffers
        this.newSession();

        // The answers arrive as events (Node never blocks); update() only checks the timeout.
        this._sock = dgram.createSocket({ type: 'udp4', sendBufferSize: 4 * 1024 * 1024 });
        this._sock.on('message', (msg, rinfo) => {
            if (rinfo.port !== this.discoveryPort) return;   // only the daemon's answers
            this._parseAnswer(msg.toString('ascii'));
            this._lastAnswer = nowMs();
            this.daemonSeen = true;
        });
        this._sock.on('error', () => {});                    // a lost datagram is not an error for us
        this._sock.bind(0, LOCALHOST, () => this.update());   // any free port; first discovery at once
    }

    /** Call once per game frame. Never blocks: asks the daemon again once a second and sends the pause packet
     *  every 200 ms for the paused players. */
    update() {
        if (this._closed) return;
        const now = nowMs();
        if (this.daemonSeen && now - this._lastAnswer > ANSWER_TIMEOUT_MS) {   // daemon closed
            this.ports.fill(0);
            this.daemonSeen = false;
        }
        if (now >= this._nextWhoami) {   // scopes can be paired, switched off or moved at any time
            this._send(Buffer.from('WHOAMI', 'ascii'), this.discoveryPort);
            this._nextWhoami = now + WHOAMI_EVERY_MS;
        }
        for (let p = 1; p <= MAX_PLAYERS; p++) {   // pause packets: the 16-byte header alone, strip 0xFF
            if (!this._paused[p] || !this.hasScope(p) || now < this._nextPresence[p]) continue;
            const h = Buffer.alloc(HEADER);
            this._header(h, this._format[p], this._frame[p], 0xFF, p);
            this._send(h, this._portOf(p));
            this._nextPresence[p] = now + PRESENCE_EVERY_MS;
        }
    }

    /** true if player 1..4 has a paired scope right now (or alwaysSend is on). */
    hasScope(player) {
        return player >= 1 && player <= MAX_PLAYERS && (this.ports[player] !== 0 || this.alwaysSend);
    }

    /**
     * Sends one 240x240 image to the scope of a player. It also ends the pause of that player.
     * @param {number} player 1..4
     * @param {Buffer|Uint8Array|Uint8ClampedArray|ArrayBuffer} image the pixels (canvas ImageData.data works)
     * @param {number} [pixels=Pixels.RGBA8888]
     * @param {number} [stride=0] bytes from one row to the next, 0 = tightly packed
     * @param {boolean} [flipY=false] the first row in memory is the bottom of the image (WebGL readPixels)
     * @returns {number} Result.OK, Result.NO_SCOPE (nothing sent) or Result.ERROR
     */
    sendFrame(player, image, pixels = Pixels.RGBA8888, stride = 0, flipY = false) {
        if (this._closed || !(player >= 1 && player <= MAX_PLAYERS) || !(pixels >= 0 && pixels <= 3)) return Result.ERROR;
        const src = image instanceof ArrayBuffer ? new Uint8Array(image)
            : ArrayBuffer.isView(image) ? new Uint8Array(image.buffer, image.byteOffset, image.byteLength) : null;
        const row = WIDTH * BPP[pixels];
        stride = stride || row;
        if (!src || stride < row || src.length < stride * (HEIGHT - 1) + row) return Result.ERROR;
        if (!this.hasScope(player)) return Result.NO_SCOPE;

        const wire = pixels === Pixels.RGBA8888 ? Pixels.RGB888 : pixels;   // format written in the header
        const bppWire = BPP[wire];
        const port = this._portOf(player);
        this._paused[player] = false;
        this._format[player] = wire;
        this._frame[player]++;   // one number per frame and per player; Uint32Array wraps by itself

        for (let strip = 0; strip < STRIPS; strip++) {
            // dgram keeps the Buffer until the datagram has really been sent: buffers come back to a pool then
            const pkt = this._take(HEADER + WIDTH * STRIP_ROWS * bppWire);
            this._header(pkt, wire, this._frame[player], strip, player);
            let o = HEADER;
            for (let r = 0; r < STRIP_ROWS; r++) {
                const y = strip * STRIP_ROWS + r;
                let s = (flipY ? HEIGHT - 1 - y : y) * stride;
                if (pixels === Pixels.RGBA8888) {
                    for (let x = 0; x < WIDTH; x++, s += 4) {
                        pkt[o++] = src[s]; pkt[o++] = src[s + 1]; pkt[o++] = src[s + 2];
                    }
                } else {
                    pkt.set(src.subarray(s, s + row), o);
                    o += row;
                }
            }
            this._send(pkt, port, true);
        }
        return Result.OK;
    }

    /** Game paused (menu, pause screen) for one player 1..4, or for all with player 0: the scope keeps the last
     *  image instead of showing NO SIGNAL. The next sendFrame() of that player ends it. */
    setPaused(player, paused = true) {
        for (let p = 1; p <= MAX_PLAYERS; p++) {
            if (player !== 0 && p !== player) continue;
            if (paused && !this._paused[p]) this._nextPresence[p] = 0;   // first packet at the next update()
            this._paused[p] = !!paused;
        }
    }

    /** A new random session: call it when a brand-new match starts (optional; the constructor does it). */
    newSession() {
        let s = 0;
        while (s === 0 || s === this._session) s = crypto.randomBytes(4).readUInt32LE(0);
        this._session = s;
    }

    /** Closes the socket. The scopes show NO SIGNAL (or the daemon's screen capture) about 0.5 s later. */
    close() {
        if (this._closed) return;
        this._closed = true;
        this._sock.close();
        this.ports.fill(0);
        this.daemonSeen = false;
    }

    // ------------------------------------------------------------------------------------------------- private

    // "OF_SCOPE:P1;PORT=47811;FW=...\nOF_SCOPE:UNPAIRED\n..." -> ports
    _parseAnswer(text) {
        const ports = new Array(MAX_PLAYERS + 1).fill(0);
        for (const line of text.split('\n')) {
            const m = /^OF_SCOPE:P([1-4])(;|$)/.exec(line.trim());
            const port = /;PORT=(\d+)/.exec(line);
            if (m && port && +port[1] > 0 && +port[1] < 65536) ports[+m[1]] = +port[1];
        }
        this.ports = ports;
    }

    _portOf(player) { return this.ports[player] || DISCOVERY_PORT + player; }

    _header(h, format, frame, strip, player) {
        h[0] = 0x4F; h[1] = 0x46; h[2] = 1; h[3] = format;
        h.writeUInt32LE(frame >>> 0, 4);
        h[8] = strip; h[9] = STRIPS; h[10] = player;
        h.writeUInt32LE(this._session >>> 0, 11);
        h[15] = 0;
    }

    _take(length) {
        const free = this._pool.get(length);
        return free && free.length ? free.pop() : Buffer.allocUnsafe(length);
    }

    _send(buf, port, recycle = false) {
        // errors are ignored on purpose: a lost strip only makes that frame incomplete
        const done = !recycle ? undefined : () => {
            let free = this._pool.get(buf.length);
            if (!free) this._pool.set(buf.length, free = []);
            if (free.length < 64) free.push(buf);
        };
        try { this._sock.send(buf, port, LOCALHOST, done); } catch (e) { /* socket closed */ }
    }
}

module.exports = { Scope, Pixels, Result, WIDTH, HEIGHT, MAX_PLAYERS, DISCOVERY_PORT };
