## openfire_scope_aim.gd - points a scope Camera3D where the gun aims, with a zoom.
##
## Put it on the Camera3D inside the scope SubViewport. Each frame it places the camera at the main camera, turns it
## along the ray through the aim point on the screen and gives it a field of view "zoom" times narrower. The image
## stays upright: the scope itself rotates it with the roll of the gun.
##
## The aim point is the mouse by default (an OpenFIRE lightgun moves the mouse). With several lightguns, set
## use_mouse = false and write aim_screen_position every frame from your input code.
##
## MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE
class_name OpenFIREScopeAim
extends Camera3D

## The camera of the screen. Empty = the current camera of the main window.
@export var main_camera: Camera3D
## Magnification: 4 means a field of view 4 times narrower.
@export_range(1.0, 20.0) var zoom := 4.0
## Aim with the mouse; untick it to set aim_screen_position from code.
@export var use_mouse := true
## The aim point in pixels of the main window (0,0 = top left).
var aim_screen_position := Vector2.ZERO


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	current = true   # the camera of its SubViewport


func _process(_delta: float) -> void:
	var main: Camera3D = main_camera if main_camera else get_tree().root.get_camera_3d()
	if main == null or main == self:
		return
	if use_mouse:
		aim_screen_position = main.get_viewport().get_mouse_position()
	var origin := main.project_ray_origin(aim_screen_position)
	var direction := main.project_ray_normal(aim_screen_position)
	var up := main.global_transform.basis.y
	if absf(direction.dot(up)) > 0.999:
		up = main.global_transform.basis.z   # looking straight up or down
	global_transform = Transform3D(Basis.looking_at(direction, up), origin)
	# the same magnification for any main field of view: tan(fov/2) divided by the zoom
	fov = rad_to_deg(2.0 * atan(tan(deg_to_rad(main.fov) * 0.5) / maxf(1.0, zoom)))
	keep_aspect = Camera3D.KEEP_HEIGHT
	near = main.near
	far = main.far
