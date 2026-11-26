## example_main.gd - a small 3D scene with an OpenFIRE scope for player 1.
##
## Everything is built in code so that the whole setup is in one place; in your game you can make the same nodes in
## the editor (see README.md). The mouse is the lightgun (an OpenFIRE lightgun moves the mouse too): the scope shows
## a 4x zoom of what is under it. Mouse wheel = zoom, P or Esc = pause menu (the scope keeps the last image).
##
##     godot --path .                       normal run
##     godot --path . -- --always           also without a scope: watch the daemon's Preview tab
##     godot --path . -- --demo 20          aims by itself and quits after 20 s
##
## MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE
extends Node3D

var _main_camera: Camera3D
var _scope_viewport: SubViewport
var _aim: OpenFIREScopeAim
var _sender: OpenFIREScopeSender
var _targets: Array[Node3D] = []
var _label: Label
var _demo_seconds := -1.0
var _time := 0.0


## The reticle: drawn inside the scope viewport, so it appears only on the scope (the scope draws none).
class Reticle extends Control:
	func _draw() -> void:
		var c := Vector2(120, 120)
		for d in [Vector2(1, 0), Vector2(-1, 0), Vector2(0, 1), Vector2(0, -1)]:
			draw_line(c + d * 10, c + d * 90, Color.BLACK, 2.0)
		draw_circle(c, 2.5, Color(1, 0.15, 0.15))


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS   # the pause key keeps working while the tree is paused
	var args := OS.get_cmdline_user_args()
	var always := args.has("--always")
	var i := args.find("--demo")
	if i >= 0:
		_demo_seconds = float(args[i + 1]) if i + 1 < args.size() else 20.0
	_build_world()

	# --- the scope: a 240x240 SubViewport that sees the same world, with its own camera and the reticle ---
	_scope_viewport = SubViewport.new()
	_scope_viewport.size = Vector2i(240, 240)
	add_child(_scope_viewport)
	_aim = OpenFIREScopeAim.new()
	_aim.main_camera = _main_camera
	_aim.zoom = 4.0
	_scope_viewport.add_child(_aim)
	var reticle := Reticle.new()
	reticle.size = Vector2(240, 240)
	_scope_viewport.add_child(reticle)

	# --- the sender: player 1 gets this viewport ---
	_sender = OpenFIREScopeSender.new()
	_sender.player = 1
	_sender.viewport = _scope_viewport
	_sender.always_send = always
	add_child(_sender)

	# --- HUD on the screen: a small copy of the scope view and the state ---
	var hud := CanvasLayer.new()
	add_child(hud)
	var preview := TextureRect.new()
	preview.texture = _scope_viewport.get_texture()
	preview.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	preview.position = Vector2(960 - 170, 10)
	preview.size = Vector2(160, 160)
	hud.add_child(preview)
	_label = Label.new()
	_label.position = Vector2(10, 10)
	_label.add_theme_color_override("font_color", Color.BLACK)
	hud.add_child(_label)


func _build_world() -> void:
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_SKY
	env.environment.sky = Sky.new()
	env.environment.sky.sky_material = ProceduralSkyMaterial.new()
	add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	add_child(sun)

	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(400, 400)
	ground.mesh = plane
	var grass := StandardMaterial3D.new()
	grass.albedo_color = Color(0.25, 0.55, 0.25)
	ground.material_override = grass
	add_child(ground)

	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	for n in 12:   # targets far away: small on the screen, big in the scope
		var disc := CylinderMesh.new()
		disc.top_radius = 1.0
		disc.bottom_radius = 1.0
		disc.height = 0.2
		var paint := StandardMaterial3D.new()
		paint.albedo_color = Color(0.9, 0.15, 0.1) if n % 2 == 0 else Color(0.95, 0.95, 0.95)
		var target := MeshInstance3D.new()
		target.mesh = disc
		target.material_override = paint
		target.rotation_degrees = Vector3(90, 0, 0)
		target.position = Vector3(rng.randf_range(-40, 40), rng.randf_range(1.5, 6), -rng.randf_range(40, 90))
		add_child(target)
		_targets.append(target)

	_main_camera = Camera3D.new()
	_main_camera.position = Vector3(0, 2, 10)
	_main_camera.fov = 60
	add_child(_main_camera)
	_main_camera.current = true


func _process(delta: float) -> void:
	if not get_tree().paused:
		_time += delta
		for n in _targets.size():
			_targets[n].position.x += sin(_time * (0.5 + n * 0.1)) * delta * 3.0
	if _demo_seconds >= 0:
		_aim.use_mouse = false
		_aim.aim_screen_position = Vector2(480 + 300 * sin(_time * 0.5), 200 + 60 * sin(_time * 0.9))
		if _time >= _demo_seconds:
			get_tree().quit()
	var scope := OpenFIREScopeSender.client()
	var state := "none"
	if scope.ports[1] != 0:
		state = "port %d" % scope.ports[1]
	elif scope.always_send:
		state = "always_send"
	_label.text = "zoom %.1fx   scope P1: %s%s" % [_aim.zoom, state, "   PAUSED - press P" if get_tree().paused else ""]


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo and event.keycode in [KEY_P, KEY_ESCAPE]:
		get_tree().paused = not get_tree().paused
	elif event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_aim.zoom = minf(10.0, _aim.zoom + 0.5)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_aim.zoom = maxf(2.0, _aim.zoom - 0.5)
