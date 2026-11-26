## openfire_scope_sender.gd - sends a SubViewport to the OpenFIRE scope of one player.
##
## Add it as a child node anywhere in the scene and set "viewport" to a 240x240 SubViewport that contains the scope
## camera (a Camera3D or Camera2D). It sends the viewport at most max_fps times per second while that player has a
## scope, turns the viewport's rendering off while it has none, and pauses the scope while the scene tree is paused.
## All the senders of the game share one connection to the daemon.
##
## MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE
class_name OpenFIREScopeSender
extends Node

## The player whose scope shows the viewport.
@export_range(1, 4) var player := 1
## The scope view: a SubViewport of 240 x 240 pixels.
@export var viewport: SubViewport
## Images per second at most. The daemon shows 30 to 60 per second; more is wasted.
@export_range(10, 60) var max_fps := 60
## Testing only: send even without a scope (daemon's Preview tab, Tools/fake_daemon.py). Shared by all senders.
@export var always_send := false

static var _scope: OpenFIREScope        # one connection for the whole game
static var _updated_frame := -1
static var _senders := 0

var _next_send := 0.0


## The shared library object, for has_scope(), ports, daemon_seen...
static func client() -> OpenFIREScope:
	if _scope == null:
		_scope = OpenFIREScope.new()
	return _scope


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS   # keep running in the pause menu, to keep the scope's last image
	_senders += 1
	client()
	if viewport:
		viewport.size = Vector2i(OpenFIREScope.WIDTH, OpenFIREScope.HEIGHT)
		viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED


func _exit_tree() -> void:
	_senders -= 1
	if _senders == 0 and _scope != null:
		_scope.close()   # the scopes show NO SIGNAL about 0.5 s later
		_scope = null


func _process(_delta: float) -> void:
	var scope := client()
	if always_send:
		scope.always_send = true
	if _updated_frame != Engine.get_process_frames():   # once per frame, whatever the number of senders
		_updated_frame = Engine.get_process_frames()
		scope.update()   # discovery once a second and pause packets; never blocks
	if viewport == null:
		return

	var active := scope.has_scope(player) and not get_tree().paused
	# no scope (or paused): the viewport does not render at all
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS if active else SubViewport.UPDATE_DISABLED
	if scope.has_scope(player) and get_tree().paused:
		scope.set_paused(player, true)   # pause menu: the scope keeps the last image
	if not active:
		return

	var now := Time.get_ticks_usec() / 1000000.0
	if now < _next_send:
		return
	_next_send = maxf(_next_send + 1.0 / max_fps, now - 1.0 / max_fps)
	# the image of the last rendered frame (one frame late: not visible on the scope)
	var image := viewport.get_texture().get_image()
	if image == null or image.get_width() != OpenFIREScope.WIDTH or image.get_height() != OpenFIREScope.HEIGHT:
		return
	image.convert(Image.FORMAT_RGB8)   # top row first, R G B: exactly what the daemon wants
	scope.send_frame(player, image.get_data(), OpenFIREScope.RGB888)
