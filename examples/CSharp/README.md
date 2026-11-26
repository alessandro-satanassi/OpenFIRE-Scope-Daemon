# C#: `OpenFIREScope.cs`

One file, no dependencies, **.NET Standard 2.0**: it works in .NET Framework 4.6.1 or later, .NET 5 to 8 and later,
Mono, Unity, MonoGame/FNA, Stride and Godot C#. Read the main [README](../README.md) first for what to show on the
scope.

| File | What it is |
| --- | --- |
| `OpenFIREScope.cs` | The library: class `OpenFIRE.OpenFIREScope`. Copy it into your project. |
| `Example/` | A console "game" (`dotnet run`): a scope view at 60 fps for every player with a scope, P to pause, Q to quit. |

For Unity there is a ready component in [`../Unity`](../Unity) that uses this same file.

## Use it

```csharp
using OpenFIRE;

var scope = new OpenFIREScope();              // at game start: opens the socket, starts the discovery
                                              // new OpenFIREScope(47900) for another discovery port
// every game frame:
scope.Update();                               // never blocks
for (int p = 1; p <= OpenFIREScope.MaxPlayers; p++)
{
    if (!scope.HasScope(p)) continue;         // no scope for that player: render nothing
    RenderScopeView(p, pixels);               // your 240x240 image in a byte[]
    scope.SendFrame(p, pixels, ScopePixels.RGBA8888);
}

scope.SetPaused(0, true);                     // pause menu, all players; the next SendFrame() ends it

scope.Dispose();                              // at game end
```

| Member | What it does |
| --- | --- |
| `new OpenFIREScope(port = 47810)` | Opens a non-blocking UDP socket on 127.0.0.1 and starts the discovery. |
| `Update()` | Once per frame: reads the daemon's answers, asks again once a second, sends the pause packets. Never blocks. |
| `HasScope(p)` | `true` if player `p` (1 to 4) has a paired scope now. |
| `SendFrame(p, byte[] image, pixels, stride = 0, flipY = false, offset = 0)` | Sends one 240x240 image. Returns `SendResult.Ok`, `NoScope` (nothing sent) or `Error`. |
| `SendFrame(p, IntPtr image, pixels, stride = 0, flipY = false)` | The same from unmanaged memory (a mapped texture, a locked bitmap). |
| `SetPaused(p, on)` | Pause for one player, or all with `p` = 0. |
| `NewSession()` | A new random session for a brand-new match (optional). |
| `AlwaysSend` | Testing: send to the default ports 47811-47814 even with no scope. |
| `DaemonSeen`, `GetPort(p)`, `NowMs` | Diagnostics and the library's clock. |
| `Dispose()` | Closes the socket. |

`ScopePixels`:

| Value | Bytes | Layout | Typical source |
| --- | --- | --- | --- |
| `RGBA8888` | 4 | R, G, B, A | MonoGame `SurfaceFormat.Color`, Unity `RGBA32`, OpenGL (sent as RGB888) |
| `BGRX8888` | 4 | B, G, R, X | Direct3D `B8G8R8A8`, Unity `BGRA32`, `System.Drawing` 32 bpp bitmaps |
| `RGB888` | 3 | R, G, B | image libraries |
| `RGB565` | 2 | 16-bit little-endian | 16-bit renderers, emulators |

`stride` is the number of bytes from one row to the next (0 = tightly packed). `flipY` = `true` when the first row in
memory is the bottom of the image.

### Good to know

- **No garbage per frame.** `SendFrame` reuses its buffers: keep your own `byte[]` and reuse it too.
- **Nothing blocks**, and a strip the system has no room for is simply dropped.
- **Daemon closed or restarted:** after 3 s without answers `HasScope()` becomes `false`, and `true` again when the
  daemon is back.
- **Not thread-safe:** call it from the game loop, or use one instance per thread.

## Engine notes

**MonoGame / FNA.** Draw the scope view into a `RenderTarget2D(GraphicsDevice, 240, 240)` (a second `View` matrix
looking along the aim ray, and a projection with a narrow field of view), then:

```csharp
scopeTarget.GetData(pixels);                                   // byte[240 * 240 * 4], top row first
scope.SendFrame(p, pixels, ScopePixels.RGBA8888);
```

`GetData` waits for the GPU; at 240x240 it is short, but if you see a stall, read the target of the previous frame
(two targets used in turn).

**Windows Forms / WPF / any System.Drawing bitmap.**

```csharp
var data = bitmap.LockBits(new Rectangle(0, 0, 240, 240), ImageLockMode.ReadOnly, PixelFormat.Format32bppRgb);
scope.SendFrame(p, data.Scan0, ScopePixels.BGRX8888, data.Stride);
bitmap.UnlockBits(data);
```

**Godot 4 C#.** `GetViewport().GetTexture().GetImage()` of a 240x240 `SubViewport`, `image.Convert(Image.Format.Rgb8)`,
then `SendFrame(p, image.GetData(), ScopePixels.RGB888)`. See [`../Godot`](../Godot) for the scene setup.

**Unity.** See [`../Unity`](../Unity).

## Run the example

```
cd Example
dotnet run                          # 60 fps; P = pause, Q = quit
dotnet run -- --always              # also without scopes, for the daemon's Preview tab
dotnet run -- --format rgb565       # rgb888 (default), rgb565, bgrx, rgba
```

Without the daemon and without scopes: run `python ../../Tools/fake_daemon.py --save frames` first.
