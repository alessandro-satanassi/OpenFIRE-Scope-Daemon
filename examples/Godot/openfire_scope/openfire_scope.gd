## openfire_scope.gd - send a game's video to the OpenFIRE scopes through the OpenFIRE Scope Daemon.
##
## Godot 4, GDScript, no plugin needed. Windows and Linux (macOS: see README.md).
##
##     var scope := OpenFIREScope.new()             # opens the socket, starts the discovery
##     # every frame:
##     scope.update()                               # never blocks
##     for p in range(1, OpenFIREScope.MAX_PLAYERS + 1):
##         if scope.has_scope(p):
##             var img := sub_viewport.get_texture().get_image()
##             img.convert(Image.FORMAT_RGB8)
##             scope.send_frame(p, img.get_data(), OpenFIREScope.RGB888)
##     scope.close()                                # at game end
##
## Most games only need OpenFIREScopeSender (openfire_scope_sender.gd), which does all this for a SubViewport.
## See README.md next to this file.
##
## MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE
class_name OpenFIREScope
extends RefCounted

const WIDTH := 240                 ## the scope image is always 240 x 240
const HEIGHT := 240
const MAX_PLAYERS := 4             ## players 1..4
const DISCOVERY_PORT := 47810      ## default; the user can change it in the daemon settings

## Pixel layouts accepted by send_frame(). RGB888 is what Image.FORMAT_RGB8 gives.
const RGB888 := 0                  ## 3 bytes: R, G, B
const RGB565 := 1                  ## 2 bytes: 16-bit little-endian, R in the 5 high bits (not Godot's FORMAT_RGB565)
const BGRX8888 := 2                ## 4 bytes: B, G, R, X

## Results of send_frame().
const OK_SENT := 0
const NO_SCOPE := 1                ## no scope for that player right now: nothing sent (not an error)
const ERROR := -1                  ## bad arguments or closed

const _STRIPS := 15
const _STRIP_ROWS := 16
const _BPP := [3, 2, 4]
const _WHOAMI_EVERY_MS := 1000     # discovery request
const _ANSWER_TIMEOUT_MS := 3000   # no answer for so long: daemon gone, no scopes
const _PRESENCE_EVERY_MS := 200    # pause packet; the daemon forgets a source after 500 ms
const _LOCALHOST := "127.0.0.1"

## Testing only: send to the default ports 47811-47814 even without a scope (daemon's Preview tab, fake_daemon.py).
var always_send := false
## True while the daemon answers the discovery.
var daemon_seen := false
## Video port of each player (index 1..4), 0 = no scope.
var ports := [0, 0, 0, 0, 0]

var _udp := PacketPeerUDP.new()
var _open := false
var _discovery_port := DISCOVERY_PORT
var _session := 0
var _frame := [0, 0, 0, 0, 0]
var _format := [0, 0, 0, 0, 0]
var _paused := [false, false, false, false, false]
var _next_presence := [0, 0, 0, 0, 0]
var _next_whoami := 0
var _last_answer := 0


## Opens the UDP socket and sends the first discovery request. discovery_port 0 = 47810.
func _init(discovery_port := DISCOVERY_PORT) -> void:
	_discovery_port = discovery_port if discovery_port > 0 else DISCOVERY_PORT
	# any free port: the answers come back here. Godot's UDP sockets never block.
	_open = _udp.bind(0, _LOCALHOST) == OK
	if not _open:
		push_warning("OpenFIRE scope: cannot open a UDP socket, the scopes are off.")
		return
	if OS.get_name() == "macOS":
		push_warning("OpenFIRE scope: on macOS Godot cannot send datagrams over 9 KB, see the README (sysctl or C#).")
	new_session()
	update()   # first discovery request at once


## A new random session: call it when a brand-new match starts (optional; new() does it).
func new_session() -> void:
	var s := 0
	while s == 0 or s == _session:
		s = randi() & 0xFFFFFFFF
	_session = s


## Call once per frame. Never blocks: reads the daemon's answers, asks again once a second, and sends the pause
## packet every 200 ms for the paused players.
func update() -> void:
	if not _open:
		return
	var now := Time.get_ticks_msec()

	# 1. answers of the daemon
	while _udp.get_available_packet_count() > 0:
		var data := _udp.get_packet()
		if _udp.get_packet_port() != _discovery_port:
			continue   # only the daemon's answers
		_parse_answer(data.get_string_from_ascii())
		_last_answer = now
		daemon_seen = true
	if daemon_seen and now - _last_answer > _ANSWER_TIMEOUT_MS:   # daemon closed
		ports = [0, 0, 0, 0, 0]
		daemon_seen = false

	# 2. discovery request once a second: scopes can be paired, switched off or moved at any time
	if now >= _next_whoami:
		_send("WHOAMI".to_ascii_buffer(), _discovery_port)
		_next_whoami = now + _WHOAMI_EVERY_MS

	# 3. pause packets: the 16-byte header alone, strip 0xFF
	for p in range(1, MAX_PLAYERS + 1):
		if _paused[p] and has_scope(p) and now >= _next_presence[p]:
			_send(_header(_format[p], _frame[p], 0xFF, p), _port_of(p))
			_next_presence[p] = now + _PRESENCE_EVERY_MS


## True if player 1..4 has a paired scope right now (or always_send is on).
func has_scope(player: int) -> bool:
	return player >= 1 and player <= MAX_PLAYERS and (ports[player] != 0 or always_send)


## Sends one 240x240 image to the scope of a player, top row first, no padding. It also ends the pause of that
## player. Returns OK_SENT, NO_SCOPE (nothing sent) or ERROR.
func send_frame(player: int, image: PackedByteArray, pixels := RGB888) -> int:
	if not _open or player < 1 or player > MAX_PLAYERS or pixels < 0 or pixels > 2:
		return ERROR
	var strip_bytes: int = WIDTH * _STRIP_ROWS * _BPP[pixels]
	if image.size() < strip_bytes * _STRIPS:
		return ERROR
	if not has_scope(player):
		return NO_SCOPE
	var port := _port_of(player)
	_paused[player] = false
	_format[player] = pixels
	_frame[player] = (_frame[player] + 1) & 0xFFFFFFFF   # one number per frame and per player
	for strip in _STRIPS:
		var packet := _header(pixels, _frame[player], strip, player)
		packet.append_array(image.slice(strip * strip_bytes, (strip + 1) * strip_bytes))
		_send(packet, port)
	return OK_SENT


## Game paused (menu, pause screen) for one player 1..4, or for all with player 0: the scope keeps the last image
## instead of showing NO SIGNAL. The next send_frame() of that player ends it.
func set_paused(player: int, paused := true) -> void:
	for p in range(1, MAX_PLAYERS + 1):
		if player == 0 or player == p:
			if paused and not _paused[p]:
				_next_presence[p] = 0   # first packet at the next update()
			_paused[p] = paused


## Closes the socket. The scopes show NO SIGNAL (or the daemon's screen capture) about 0.5 s later.
func close() -> void:
	if _open:
		_udp.close()
	_open = false
	ports = [0, 0, 0, 0, 0]
	daemon_seen = false


# "OF_SCOPE:P1;PORT=47811;FW=...\nOF_SCOPE:UNPAIRED\n..." -> ports
func _parse_answer(text: String) -> void:
	var found := [0, 0, 0, 0, 0]
	for line in text.split("\n", false):
		var fields := line.strip_edges().split(";")
		var head := fields[0]
		if head.length() != 11 or not head.begins_with("OF_SCOPE:P") or not "1234".contains(head[10]):
			continue
		for f in fields:
			if f.begins_with("PORT=") and f.substr(5).is_valid_int():
				var port := f.substr(5).to_int()
				if port > 0 and port < 65536:
					found[head[10].to_int()] = port
	ports = found


func _port_of(player: int) -> int:
	return ports[player] if ports[player] != 0 else DISCOVERY_PORT + player


func _header(fmt: int, frame: int, strip: int, player: int) -> PackedByteArray:
	return PackedByteArray([0x4F, 0x46, 1, fmt,
		frame & 0xFF, (frame >> 8) & 0xFF, (frame >> 16) & 0xFF, (frame >> 24) & 0xFF,
		strip, _STRIPS, player,
		_session & 0xFF, (_session >> 8) & 0xFF, (_session >> 16) & 0xFF, (_session >> 24) & 0xFF, 0])


func _send(data: PackedByteArray, port: int) -> void:
	# errors are ignored on purpose: a lost strip only makes that frame incomplete
	_udp.set_dest_address(_LOCALHOST, port)
	_udp.put_packet(data)
