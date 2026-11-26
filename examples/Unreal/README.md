# Unreal Engine 5

A C++ actor component that captures the scope view of a player and sends it, plus a small client on Unreal's own
sockets. Unreal Engine 5.0 or later, Windows, Linux and macOS. The component can be used from Blueprints too, but the
files must be compiled in a C++ project (*Tools > New C++ Class* turns a Blueprint project into one). Read the main
[README](../README.md) first for what to show on the scope.

| File | What it is |
| --- | --- |
| `Source/OpenFIREScopeComponent.h/.cpp` | `UOpenFIREScopeComponent`: the scope view of one player (capture, aim, zoom, reticle, pause). |
| `Source/OpenFIREScopeClient.h/.cpp` | `FOpenFIREScopeClient`: discovery, video, pause. Shared by all the components. |

## Add it to your game

1. Copy the four files of `Source` into your game module, for example `Source/MyGame/OpenFIRE/`.
2. In `Source/MyGame/MyGame.Build.cs` add the socket modules:
   ```csharp
   PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "Sockets", "Networking" });
   ```
3. Build the project (from your IDE, or *Tools > Compile* in the editor).
4. Open the player's pawn Blueprint (or C++ class), *Add Component > OpenFIRE Scope*, and set *Player* (1 to 4) and
   *Zoom*.

Press Play. When that player has a paired scope, the scope shows what is under the mouse cursor, zoomed. A player
without a scope costs nothing: the component does not capture.

For more players, add one component per player (on each player's pawn, or several on one actor) with a different
*Player* and its own aim point.

## How it works

Every frame, for a player that has a scope, at most *Max Fps* times per second:

1. **Aim.** It places a `SceneCaptureComponent2D` at the player's camera, looking along the ray through the aim point
   on the screen (`DeprojectScreenPositionToWorld`), with a field of view *Zoom* times narrower. The image stays
   upright: the scope rotates it with the roll of the gun.
2. **Capture** into a 240x240 `RGBA8` render target (`SCS_FinalColorLDR`, the same colours as the screen; the owning
   pawn is hidden so the player's own gun does not fill the view).
3. **Read** it back as `FColor` (`ReadPixels`), which in memory is B, G, R, A: exactly the BGRX layout of the daemon.
4. **Reticle:** it draws a simple crosshair over the pixels (the scope draws none).
5. **Send** it with the client.

During the pause menu (`SetGamePaused`) the component keeps ticking and tells the scope to keep the last image.
When the game ends, the scopes show NO SIGNAL (or the daemon's screen capture) about half a second later.

## Properties

| Property | Meaning |
| --- | --- |
| *Player* | 1 to 4. |
| *Zoom* | 4 = a field of view 4 times narrower than the player's camera. |
| *Max Fps* | Images per second at most (60). The daemon shows 30 to 60 per second. |
| *Use Mouse* | Aim with the mouse cursor of player controller 0 (what one OpenFIRE lightgun moves). Untick it and set *Aim Screen Position* (viewport pixels, 0,0 = top left) from your input code with several lightguns. |
| *Draw Reticle*, *Reticle Color* | The crosshair drawn by the component. Untick it to draw your own (below). |
| *Always Send* | Testing only: send even when no scope is paired, to watch the images in the daemon's Preview tab. |

`HasScope()` (Blueprint pure) tells whether the player has a scope now; `GetCapture()` gives the scene capture for
your own settings: show flags, a post-process material, more hidden actors.

## Your own reticle and overlays

The simplest way is a **post-process material** on the capture (`GetCapture()->PostProcessSettings.AddBlendable(...)`)
that draws the reticle, the zoom level or a range-finder over the image; then untick *Draw Reticle*. Anything else can
be drawn directly into the `FColor` pixels before they are sent.

## Faster read-back

`ReadPixels` waits for the GPU to finish the capture. For a 240x240 target the wait is short and the component reads
only for players with a scope. If your game needs every fraction of a millisecond, replace it with an asynchronous
read: enqueue an `FRHIGPUTextureReadback` copy of the render target after `CaptureScene()`, check `IsReady()` on the
next frames, and send the pixels when they arrive. The scope does not show a one-frame delay.

## Using the client alone

```cpp
#include "OpenFIREScopeClient.h"

TSharedRef<FOpenFIREScopeClient> Scope = FOpenFIREScopeClient::Acquire();   // keep it while you need it
// every frame:
Scope->Update();                                                           // never blocks
if (Scope->HasScope(1))
    Scope->SendFrame(1, Colors.GetData(), FOpenFIREScopeClient::EPixels::BGRX8888);   // TArray<FColor>, 240x240
Scope->SetPaused(0, true);                                                 // pause menu, all players
// releasing the last reference closes the socket
```

## Testing

- **Without the daemon and without scopes:** run `python Tools/fake_daemon.py --save frames` (from the `examples`
  folder), press Play, and look at `frames/P1.png`.
- **With the daemon but without scopes:** tick *Always Send* and open the daemon's Preview tab.
