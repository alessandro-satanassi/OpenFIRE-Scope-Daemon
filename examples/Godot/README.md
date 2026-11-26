# Godot 4

GDScript for **Godot 4.1 or later**, no plugin and no GDExtension. This folder is also a small Godot project that
shows the whole setup. Read the main [README](../README.md) first for what to show on the scope.

| File | What it is |
| --- | --- |
| `openfire_scope/openfire_scope.gd` | The library: class `OpenFIREScope` (discovery, video, pause). |
| `openfire_scope/openfire_scope_sender.gd` | Node `OpenFIREScopeSender`: sends a 240x240 `SubViewport` to the scope of one player. |
| `openfire_scope/openfire_scope_aim.gd` | Camera `OpenFIREScopeAim`: a `Camera3D` that looks where the gun aims, with a zoom. |
| `project.godot`, `example_main.tscn`, `example_main.gd` | The example: a 3D scene, the mouse as lightgun, a 4x scope for player 1. |

## Add it to your game

Copy the folder `openfire_scope` into your project. The three classes appear in the *Create New Node* dialog.

For each player with a scope:

1. Add a **SubViewport** to your scene (anywhere; it sees the same 3D world as the main window). The sender sets its
   size to 240x240.
2. Inside it add an **OpenFIREScopeAim** (it is a `Camera3D`). Set *Main Camera* to your game camera (empty = the
   current camera of the window) and *Zoom* to 4.
3. Inside the SubViewport also add your **reticle**: a `Control`, `TextureRect` or `Sprite2D` centred at (120, 120).
   It appears only on the scope; the scope draws none by itself.
4. Add an **OpenFIREScopeSender** anywhere, set *Player* and drag the SubViewport into *Viewport*.

```
Main (Node3D)
├── Camera3D                       the screen
├── ...                            your world
├── ScopeP1 (SubViewport)
│   ├── OpenFIREScopeAim           zoom 4, main camera = Camera3D
│   └── Reticle (TextureRect)      crosshair in the centre
└── OpenFIREScopeSender            player 1, viewport = ScopeP1
```

That is all. While a player has no scope, its SubViewport does not render at all, so it costs nothing.

### The aim point

`OpenFIREScopeAim` uses the mouse by default, which is what one OpenFIRE lightgun moves. With several lightguns,
untick *Use Mouse* and write each player's aim point, in pixels of the window, from your input code:

```gdscript
$ScopeP2/OpenFIREScopeAim.aim_screen_position = player2_aim
```

The image stays upright: do not roll the scope camera with the gun, the scope does it by itself.

### 2D games

Put a `Camera2D` in the SubViewport instead of `OpenFIREScopeAim`, with `world_2d` of the SubViewport set to the
main viewport's (`sub_viewport.world_2d = get_viewport().world_2d`), its position on the aim point and its `zoom` at
4.

### Pause

The sender keeps running while the scene tree is paused (`get_tree().paused = true`) and tells the scope to keep the
last image. When the game quits, the scopes show NO SIGNAL (or the daemon's screen capture) about half a second
later.

## The sender

| Property | Meaning |
| --- | --- |
| *Player* | 1 to 4. |
| *Viewport* | The 240x240 SubViewport. |
| *Max Fps* | Images per second at most (60). The daemon shows 30 to 60 per second. |
| *Always Send* | Testing only: send even when no scope is paired, to watch the images in the daemon's Preview tab. |

From code, `OpenFIREScopeSender.client()` gives the shared `OpenFIREScope` object: `has_scope(player)`, `ports`,
`daemon_seen`.

The image is read with `get_texture().get_image()`, which waits for the GPU; at 240x240 the wait is short.

## The library alone

For your own setup (a 2D game that crops its frame, an image you build yourself):

```gdscript
var scope := OpenFIREScope.new()              # OpenFIREScope.new(47900) for another discovery port

func _process(_delta):
    scope.update()                            # never blocks
    for p in range(1, OpenFIREScope.MAX_PLAYERS + 1):
        if scope.has_scope(p):
            var img: Image = my_scope_image(p)    # 240x240
            img.convert(Image.FORMAT_RGB8)
            scope.send_frame(p, img.get_data(), OpenFIREScope.RGB888)

# pause menu: scope.set_paused(0, true); the next send_frame() ends it
# game end:   scope.close()
```

`send_frame()` returns `OpenFIREScope.OK_SENT`, `NO_SCOPE` or `ERROR` (not `OK`, which is Godot's own constant). It
takes the pixels top row first, without padding, as `RGB888`, `BGRX8888` or `RGB565` (little-endian with red in the
high bits: Godot's own `FORMAT_RGB565` is laid out differently, so do not use it).

## macOS

The scope datagrams are larger than 9 KB. macOS sends them only from a socket with a larger send buffer, and Godot
offers no way to set it, so on macOS a GDScript game sends nothing. Two ways out:

- run once, in the Terminal, `sudo sysctl -w net.inet.udp.maxdgram=65535` (it lasts until the next restart);
- or use **Godot .NET** with the C# library in [`../CSharp`](../CSharp), which sets the buffer by itself.

Windows and Linux are not affected.

## Run the example

Open this folder with the Godot editor and press *Run*, or from a terminal:

```
godot --path .                     # mouse = lightgun, wheel = zoom, P = pause
godot --path . -- --always         # also without a scope, for the daemon's Preview tab
godot --path . -- --demo 20        # aims by itself for 20 s
```

Without the daemon and without scopes: run `python ../Tools/fake_daemon.py --save frames` first.
