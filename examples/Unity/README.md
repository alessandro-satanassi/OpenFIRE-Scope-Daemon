# Unity

Three small components over the C# library in [`../CSharp`](../CSharp). Unity 2021.3 or later, built-in render
pipeline, URP or HDRP, Windows, Linux and macOS. Read the main [README](../README.md) first for what to show on the
scope.

| File | What it is |
| --- | --- |
| `OpenFIREScopeClient.cs` | The one connection to the daemon. Created by itself; put it in your first scene only to change its settings. |
| `OpenFIREScopeSender.cs` | On a scope camera: renders it into a 240x240 texture and sends it to the scope of one player. |
| `OpenFIREScopeAim.cs` | On the same camera: points it where the gun aims, with a zoom. |
| `../CSharp/OpenFIREScope.cs` | The library. Needed too. |

## Setup (five minutes)

1. Copy `../CSharp/OpenFIREScope.cs` and the three `.cs` files of this folder into your project, for example into
   `Assets/OpenFIRE/`.
2. In your scene create an empty GameObject, name it `Scope Camera P1`, and add to it:
   - a **Camera** component (if you create it with *GameObject > Camera*, remove its *Audio Listener*);
   - **OpenFIRE Scope Sender**, with *Player* = 1;
   - **OpenFIRE Scope Aim**, with *Main Camera* = your game camera (empty = `Camera.main`) and *Zoom* = 4.
3. Press Play. When player 1 has a paired scope, the scope shows what is under the mouse, 4 times closer.

For more players, duplicate the scope camera, set *Player* = 2, 3, 4, and give each one its aim point (below). A
player without a scope costs nothing: its camera does not render.

### The aim point

`OpenFIREScopeAim` uses the mouse by default, which is what one OpenFIRE lightgun moves. With several lightguns,
untick *Use Mouse* and write the aim point of each player from your input code, in screen pixels like
`Input.mousePosition` (0,0 = bottom left):

```csharp
scopeCameraP2.GetComponent<OpenFIREScopeAim>().AimScreenPosition = player2AimOnScreen;
```

The camera is placed at the main camera and looks along the ray through the aim point. The image stays upright: do
not roll the scope camera with the gun, the scope does it by itself.

### The reticle

The scope draws nothing over the image: the reticle is yours. The simplest way is a UI Canvas with *Render Mode* =
*Screen Space - Camera* and *Render Camera* = the scope camera, with a crosshair `Image` in the centre. Anything else
drawn on that canvas (zoom level, range, ammunition) appears only on the scope.

### What the scope camera should see

- **Culling Mask:** leave out the layer of the player's own gun and arms, and the screen HUD.
- **Post-processing:** keep it light; the image is 240x240 and compressed.
- **Background:** the same skybox as the main camera.

### Pause

The scopes keep the last image while `Time.timeScale` is 0 (option *Pause When Time Scale Is Zero* of the client), or
while you set:

```csharp
OpenFIREScopeClient.Paused = true;    // pause menu open
OpenFIREScopeClient.Paused = false;   // back to the game
```

When the game quits, the client closes the socket and the scopes show NO SIGNAL (or the daemon's screen capture)
about half a second later.

## Components

**OpenFIRE Scope Client** (optional in the scene)

| Field | Meaning |
| --- | --- |
| *Discovery Port* | 47810, unless the user changed it in the daemon settings. |
| *Always Send* | Testing only: send to the default ports even when no scope is paired, to watch the images in the daemon's Preview tab. |
| *Pause When Time Scale Is Zero* | `Time.timeScale == 0` counts as the pause menu. |

From code: `OpenFIREScopeClient.Instance.HasScope(player)`, `OpenFIREScopeClient.Instance.Scope` (the
`OpenFIREScope` object of the library), `OpenFIREScopeClient.Paused`.

**OpenFIRE Scope Sender**

| Field | Meaning |
| --- | --- |
| *Player* | 1 to 4. |
| *Max Fps* | Images per second at most (60). The daemon shows 30 to 60 per second. |
| *Flip Y* | On by default: Unity reads textures back with the bottom row first. If the image is upside down in the daemon's Preview tab, untick it. |

It reads the image with `AsyncGPUReadback`, so the game never waits for the GPU (the scope gets the image one or two
frames later, which is not visible). On devices without it, it falls back to `Texture2D.ReadPixels`.

**OpenFIRE Scope Aim**

| Field | Meaning |
| --- | --- |
| *Main Camera* | The camera of the screen. Empty = `Camera.main`. |
| *Zoom* | 4 = a field of view 4 times narrower. |
| *Use Mouse* | Aim with the mouse; untick it to set `AimScreenPosition` from code. |

## Testing

- **Without the daemon and without scopes:** run `python Tools/fake_daemon.py --save frames` (from the `examples`
  folder), press Play, and look at `frames/P1.png`.
- **With the daemon but without scopes:** add *OpenFIRE Scope Client* to the scene, tick *Always Send*, press Play
  and open the daemon's Preview tab.
