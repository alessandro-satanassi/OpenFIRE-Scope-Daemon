#!/usr/bin/env node
/*  example_basic.js - the smallest complete "game" for the OpenFIRE scopes, in Node.js.

    It draws an animated 240x240 RGBA scope view (the layout of a canvas ImageData) for every player that has a
    scope and sends it at 60 fps. From 10 s to 13 s it "pauses the game" (the scopes keep the last image); at the end
    it closes (the scopes show NO SIGNAL).

        node example_basic.js                 60 fps, 20 s
        node example_basic.js --always        also without scopes: watch the daemon's Preview tab
        node example_basic.js --seconds 60

    MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE
*/
'use strict';
const { Scope, Pixels, WIDTH, HEIGHT, MAX_PLAYERS } = require('./openfire-scope');

const args = process.argv.slice(2);
const always = args.includes('--always');
const seconds = args.includes('--seconds') ? Number(args[args.indexOf('--seconds') + 1]) : 20;

// In a real game this is a second camera with a narrow field of view looking where the gun aims, rendered into a
// 240x240 target (see ../README.md, "What to show on the scope"). Here we just draw something.
const image = new Uint8ClampedArray(WIDTH * HEIGHT * 4);
function renderScopeView(player, t) {
    const pan = t * 35 + player * 200, tx = 120 + 65 * Math.sin(t * 1.2 + player), ty = 120 + 30 * Math.cos(t * 0.8);
    let i = 0;
    for (let y = 0; y < HEIGHT; y++) {
        for (let x = 0; x < WIDTH; x++, i += 4) {
            const skyline = 125 + 20 * Math.sin((x + pan) * 0.02) + (Math.floor((x + pan) / 18) % 3) * 12;
            let r, g, b;
            if (y < skyline) { r = 255 - y / 2; g = 150 + y / 4; b = 120 + y / 2; }                 // sunset sky
            else { r = 50; g = 50 + ((Math.floor((x + pan) / 6) + Math.floor(y / 8)) % 2) * 25; b = 70; }   // buildings
            const dx = x - tx, dy = y - ty;
            if (dx * dx + dy * dy < 13 * 13) { r = 40; g = 230; b = 90; }                            // target
            const ax = Math.abs(x - 120), ay = Math.abs(y - 120);                                     // reticle
            if ((ax <= 1 && ay > 8 && ay < 65) || (ay <= 1 && ax > 8 && ax < 65)) r = g = b = 0;
            image[i] = r; image[i + 1] = g; image[i + 2] = b; image[i + 3] = 255;
        }
    }
    return image;
}

const scope = new Scope({ alwaysSend: always });
const start = Date.now();
let next = start, lastPrint = 0;

function frame() {
    const now = Date.now(), t = (now - start) / 1000;
    if (t >= seconds) {
        scope.close();   // the scopes show NO SIGNAL about 0.5 s later
        return;
    }
    const paused = t >= 10 && t < 13;   // the "pause menu"

    scope.update();   // discovery + pause packets, never blocks
    for (let p = 1; p <= MAX_PLAYERS; p++) {
        if (!scope.hasScope(p)) continue;   // no scope for that player: render nothing
        if (paused) scope.setPaused(p);     // the scope keeps the last image
        else scope.sendFrame(p, renderScopeView(p, t), Pixels.RGBA8888);
    }

    if (now - lastPrint >= 1000) {
        lastPrint = now;
        const list = [1, 2, 3, 4].map(p => `P${p}=${scope.ports[p] ? 'yes' : '-'}`).join(' ');
        console.log(`${t.toFixed(1).padStart(5)} s  daemon ${scope.daemonSeen ? 'yes' : 'no '}  scopes: ${list}${paused ? '  (paused)' : ''}`);
    }

    next += 1000 / 60;   // fixed 60 fps; in a game, call this from your own loop (requestAnimationFrame, ...)
    setTimeout(frame, Math.max(0, next - Date.now()));
}
frame();
