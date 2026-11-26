# JavaScript: `openfire-scope.js`

One file for **Node.js 16 or later**, no dependencies (only the built-in `dgram` module). It works in Node games, in
the main process of **Electron** and in **NW.js**. Read the main [README](../README.md) first for what to show on the
scope.

| File | What it is |
| --- | --- |
| `openfire-scope.js` | The library: class `Scope`. Copy it into your project. |
| `example_basic.js` | The smallest complete "game": an RGBA scope view at 60 fps for every player with a scope, a pause, the end. |

> A web page in a normal browser cannot send UDP datagrams, so a pure browser game cannot talk to the daemon. Package
> it with Electron or NW.js (see below).

## Use it

```js
const { Scope, Pixels, MAX_PLAYERS } = require('./openfire-scope');

const scope = new Scope();                         // at game start: opens the socket, starts the discovery
                                                   // new Scope({ discoveryPort: 47900 }) for another port
// every game frame:
scope.update();                                    // never blocks
for (let p = 1; p <= MAX_PLAYERS; p++) {
    if (!scope.hasScope(p)) continue;              // no scope for that player: render nothing
    const pixels = renderScopeView(p);             // 240x240 RGBA, e.g. ctx.getImageData(0, 0, 240, 240).data
    scope.sendFrame(p, pixels, Pixels.RGBA8888);
}

scope.setPaused(0);                                // pause menu, all players; the next sendFrame() ends it

scope.close();                                     // at game end
```

| Member | What it does |
| --- | --- |
| `new Scope({ discoveryPort, alwaysSend })` | Opens a UDP socket on 127.0.0.1 and starts the discovery. Both options are optional. |
| `update()` | Once per frame: asks the daemon again once a second and sends the pause packets. The answers arrive as events. |
| `hasScope(p)` | `true` if player `p` (1 to 4) has a paired scope now. |
| `sendFrame(p, image, pixels = RGBA8888, stride = 0, flipY = false)` | Sends one 240x240 image (`Buffer`, `Uint8Array`, `Uint8ClampedArray` or `ArrayBuffer`). Returns `Result.OK`, `Result.NO_SCOPE` (nothing sent) or `Result.ERROR`. |
| `setPaused(p, paused = true)` | Pause for one player, or all with `p` = 0. |
| `newSession()` | A new random session for a brand-new match (optional). |
| `close()` | Closes the socket. |
| `alwaysSend` | Testing: send to the default ports 47811-47814 even with no scope. |
| `ports`, `daemonSeen` | Diagnostics: the video port of each player (index 1 to 4, 0 = none), the daemon answers. |

`Pixels`: `RGBA8888` (canvas `ImageData`, WebGL `readPixels`; sent as RGB888), `RGB888`, `BGRX8888`, `RGB565`
(little-endian). `stride` is the number of bytes from one row to the next (0 = tightly packed). `flipY = true` when the
first row in memory is the bottom of the image, as with WebGL `readPixels`.

### Where the pixels come from

| Source | Code |
| --- | --- |
| 2D canvas (also Phaser, PixiJS with `renderer.extract.canvas`) | `ctx.drawImage(gameCanvas, aimX - 30, aimY - 30, 60, 60, 0, 0, 240, 240)` on a 240x240 canvas, then `ctx.getImageData(0, 0, 240, 240).data` |
| WebGL | render into a 240x240 framebuffer, `gl.readPixels(0, 0, 240, 240, gl.RGBA, gl.UNSIGNED_BYTE, buf)`, `flipY = true` |
| three.js | `renderer.setRenderTarget(target)` (a 240x240 `WebGLRenderTarget`), `renderer.render(scene, scopeCamera)`, `renderer.readRenderTargetPixels(target, 0, 0, 240, 240, buf)`, `flipY = true` |
| node-canvas, sharp, jimp | their raw RGBA buffer |

## Electron

The renderer process (your game page) draws the scope view; the main process owns the socket. Send each frame over
IPC as an `ArrayBuffer`:

```js
// preload.js
const { contextBridge, ipcRenderer } = require('electron');
contextBridge.exposeInMainWorld('openfireScope', {
    sendFrame: (player, rgba, flipY = false) => ipcRenderer.send('scope-frame', player, rgba.buffer, flipY),
    setPaused: (player, paused) => ipcRenderer.send('scope-pause', player, paused),
    onScopes: (callback) => ipcRenderer.on('scope-ports', (_event, ports) => callback(ports)),
});

// main.js
const { Scope, Pixels } = require('./openfire-scope');
const scope = new Scope();
setInterval(() => {                                     // the main process keeps the discovery and the pause going
    scope.update();
    win.webContents.send('scope-ports', scope.ports);  // the game knows which players have a scope
}, 50);
ipcMain.on('scope-frame', (_e, player, buffer, flipY) => scope.sendFrame(player, buffer, Pixels.RGBA8888, 0, flipY));
ipcMain.on('scope-pause', (_e, player, paused) => scope.setPaused(player, paused));
app.on('before-quit', () => scope.close());

// the game page, every frame, for each player p with ports[p] !== 0:
window.openfireScope.sendFrame(p, ctx.getImageData(0, 0, 240, 240).data);
```

A frame is 230 KB, a small copy for Electron's IPC even at 60 fps for four players.

## Good to know

- **Nothing blocks.** `sendFrame` only queues the datagrams; Node sends them in the background and reuses the buffers.
- **Daemon closed or restarted:** after 3 s without answers `hasScope()` becomes `false`, and `true` again when the
  daemon is back.
- **Call `close()`** at the end, otherwise the open socket keeps the Node process alive.

## Run the example

```
node example_basic.js                 # 60 fps, 20 s, pause from 10 s to 13 s
node example_basic.js --always        # also without scopes, for the daemon's Preview tab
```

Without the daemon and without scopes: run `python ../Tools/fake_daemon.py --save frames` in another terminal first.
