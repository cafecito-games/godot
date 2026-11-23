extends RefCounted

const BenchCase = preload("res://bench_case.gd")


class SimActor extends RefCounted:
	var actor_id: int
	var position: Vector2
	var velocity: Vector2
	var health: float
	var state: int

	func _init(p_actor_id: int, p_position: Vector2, p_velocity: Vector2) -> void:
		actor_id = p_actor_id
		position = p_position
		velocity = p_velocity
		health = 100.0
		state = 0

	func integrate(delta_time: float, arena_size: float) -> void:
		velocity.y += 9.8 * delta_time
		velocity *= 0.995
		position += velocity * delta_time
		if position.x < 0.0:
			position.x = -position.x
			velocity.x = absf(velocity.x) * 0.9
		elif position.x > arena_size:
			position.x = arena_size * 2.0 - position.x
			velocity.x = -absf(velocity.x) * 0.9
		if position.y < 0.0:
			position.y = -position.y
			velocity.y = absf(velocity.y) * 0.9
		elif position.y > arena_size:
			position.y = arena_size * 2.0 - position.y
			velocity.y = -absf(velocity.y) * 0.9

	func apply_repulsion(other_position: Vector2, radius_squared: float) -> void:
		var delta: Vector2 = position - other_position
		var distance_squared: float = delta.length_squared()
		if distance_squared > 0.0001 and distance_squared < radius_squared:
			var push: float = (radius_squared - distance_squared) * 0.02
			velocity += delta * push
			health = maxf(0.0, health - 0.01)


class PacketRecord extends RefCounted:
	var entity_id: int
	var flags: int
	var tile_x: int
	var tile_y: int
	var health: int
	var delta_x: int
	var delta_y: int

	func _init(
		p_entity_id: int,
		p_flags: int,
		p_tile_x: int,
		p_tile_y: int,
		p_health: int,
		p_delta_x: int,
		p_delta_y: int
	) -> void:
		entity_id = p_entity_id
		flags = p_flags
		tile_x = p_tile_x
		tile_y = p_tile_y
		health = p_health
		delta_x = p_delta_x
		delta_y = p_delta_y


class StateEvent extends RefCounted:
	var kind: int
	var target_key: String
	var value: int

	func _init(p_kind: int, p_target_key: String, p_value: int) -> void:
		kind = p_kind
		target_key = p_target_key
		value = p_value


class HealthEvent extends StateEvent:
	var maximum_health: int

	func _init(p_target_key: String, p_value: int, p_maximum_health: int) -> void:
		super._init(1, p_target_key, p_value)
		maximum_health = p_maximum_health


class StatusEvent extends StateEvent:
	var active: bool

	func _init(p_target_key: String, p_value: int, p_active: bool) -> void:
		super._init(2, p_target_key, p_value)
		active = p_active


class UiBinding extends RefCounted:
	var key: String
	var control: Object
	var last_value: int = -1

	func _init(p_key: String, p_control: Object) -> void:
		key = p_key
		control = p_control

	func apply_event(event: StateEvent, state_store: Dictionary) -> int:
		return 0


class HealthBarBinding extends UiBinding:
	func apply_event(event: StateEvent, state_store: Dictionary) -> int:
		if event is HealthEvent:
			var health_event: HealthEvent = event as HealthEvent
			var clamped: int = clampi(health_event.value, 0, health_event.maximum_health)
			state_store[key] = clamped
			if clamped != last_value:
				last_value = clamped
				if control is Control:
					control.set_visible(clamped > 0)
					control.set_position(Vector2(float(clamped), 8.0))
			return clamped
		return 0


class StatusBadgeBinding extends UiBinding:
	func apply_event(event: StateEvent, state_store: Dictionary) -> int:
		if event is StatusEvent:
			var status_event: StatusEvent = event as StatusEvent
			state_store[key] = status_event.value
			if status_event.value != last_value:
				last_value = status_event.value
				if control is Control:
					control.set_visible(status_event.active)
					control.set_position(Vector2(float(status_event.value), 16.0))
			return status_event.value
		return 0


var _sim_actors: Array[SimActor] = []
var _grid_blocked: PackedByteArray = PackedByteArray()
var _tree_root: Node2D = null
var _visibility_grid: PackedByteArray = PackedByteArray()
var _visibility_elevation: PackedInt32Array = PackedInt32Array()
var _packet_payload: PackedByteArray = PackedByteArray()
var _ui_root: Control = null
var _ui_bindings: Dictionary = {}
const GRID_WIDTH: int = 32
const GRID_HEIGHT: int = 32
const VISIBILITY_WIDTH: int = 64
const VISIBILITY_HEIGHT: int = 64


static func build_cases(scale: float) -> Array:
	var suite := new()
	suite._init_grid()
	suite._init_visibility_grid()
	suite._init_packet_payload(256)
	var cases: Array = []

	var sim_ticks := BenchCase.scaled_count(24, scale)
	var actor_count: int = 384
	cases.append(BenchCase.new(
		"macro_entity_sim_2d",
		"macro",
		"2D simulation of 384 actors with spatial hash buckets, repulsion, and integration",
		sim_ticks * actor_count,
		func() -> int: return suite._bench_entity_sim_2d(sim_ticks),
		Callable(),
		Callable(),
		func() -> void: suite._setup_sim_actors(actor_count)
	))

	var astar_runs := BenchCase.scaled_count(20, scale)
	cases.append(BenchCase.new(
		"macro_astar_grid_2d",
		"macro",
		"Pure GDScript A* pathfinding on a 32x32 obstacle grid with binary heap",
		astar_runs * GRID_WIDTH * GRID_HEIGHT,
		func() -> int: return suite._bench_astar_grid_2d(astar_runs)
	))

	var tree_ticks := BenchCase.scaled_count(120, scale)
	var node_count: int = 256
	cases.append(BenchCase.new(
		"macro_node2d_tree_update",
		"macro",
		"Traverse and update transforms across a 2-level tree of 256 Node2D nodes",
		tree_ticks * node_count,
		func() -> int: return suite._bench_node2d_tree_update(tree_ticks),
		func() -> void: suite._setup_node_tree(16, 15),
		func() -> void: suite._teardown_node_tree(),
		func() -> void: suite._reset_node_tree_transforms()
	))

	var codec_batches := BenchCase.scaled_count(14, scale)
	cases.append(BenchCase.new(
		"macro_varint_packet_codec",
		"macro",
		"Encode and decode binary varint packet streams into RefCounted records",
		codec_batches * 256,
		func() -> int: return suite._bench_varint_packet_codec(codec_batches)
	))

	var visibility_scans := BenchCase.scaled_count(96, scale)
	cases.append(BenchCase.new(
		"macro_tile_grid_visibility",
		"macro",
		"2D Vector2i tile grid line-of-sight rays and walkable frontier scan on 64x64 map",
		visibility_scans * 256,
		func() -> int: return suite._bench_tile_grid_visibility(visibility_scans)
	))

	var ui_frames := BenchCase.scaled_count(80, scale)
	cases.append(BenchCase.new(
		"macro_ui_state_dispatch",
		"macro",
		"Dispatch typed RefCounted state events through polymorphic bindings to Control nodes",
		ui_frames * 64,
		func() -> int: return suite._bench_ui_state_dispatch(ui_frames),
		func() -> void: suite._setup_ui_tree(64),
		func() -> void: suite._teardown_ui_tree(),
		func() -> void: suite._reset_ui_bindings()
	))

	return cases


func _setup_sim_actors(actor_count: int) -> void:
	_sim_actors.clear()
	_sim_actors.resize(actor_count)
	var seed_value: int = 424242
	for index: int in range(actor_count):
		seed_value = (seed_value * 1103515245 + 12345) & 0x7fffffff
		var position_x: float = float(seed_value % 640)
		seed_value = (seed_value * 1103515245 + 12345) & 0x7fffffff
		var position_y: float = float(seed_value % 640)
		seed_value = (seed_value * 1103515245 + 12345) & 0x7fffffff
		var velocity_x: float = float((seed_value % 200) - 100) * 0.5
		seed_value = (seed_value * 1103515245 + 12345) & 0x7fffffff
		var velocity_y: float = float((seed_value % 200) - 100) * 0.5
		_sim_actors[index] = SimActor.new(index, Vector2(position_x, position_y), Vector2(velocity_x, velocity_y))


func _bench_entity_sim_2d(ticks: int) -> int:
	var actors: Array[SimActor] = _sim_actors
	var cell_size: float = 32.0
	var arena_size: float = 640.0
	var radius_squared: float = 256.0
	var buckets: Dictionary[int, Array] = {}

	for tick: int in range(ticks):
		buckets.clear()
		for actor: SimActor in actors:
			var cell_x: int = int(actor.position.x / cell_size)
			var cell_y: int = int(actor.position.y / cell_size)
			var cell_key: int = (cell_y << 8) | cell_x
			if not buckets.has(cell_key):
				buckets[cell_key] = [actor]
			else:
				buckets[cell_key].append(actor)

		for actor: SimActor in actors:
			var cell_x: int = int(actor.position.x / cell_size)
			var cell_y: int = int(actor.position.y / cell_size)
			var cell_key: int = (cell_y << 8) | cell_x
			var neighbors: Array = buckets[cell_key]
			for other_variant: Variant in neighbors:
				var other: SimActor = other_variant as SimActor
				if other.actor_id != actor.actor_id:
					actor.apply_repulsion(other.position, radius_squared)
			actor.integrate(0.016, arena_size)

	var checksum: float = 0.0
	for actor: SimActor in actors:
		checksum += actor.position.x * 0.1 + actor.position.y * 0.2 + actor.health
	return int(checksum)


func _init_grid() -> void:
	_grid_blocked.resize(GRID_WIDTH * GRID_HEIGHT)
	_grid_blocked.fill(0)
	# Deterministic maze-like walls with open passages.
	for row: int in range(GRID_HEIGHT):
		for col: int in range(GRID_WIDTH):
			if (col == 8 and row != 5 and row != 26) or (col == 16 and row != 22 and row != 3) or (col == 24 and row != 12 and row != 29):
				_grid_blocked[row * GRID_WIDTH + col] = 1
			elif row == 15 and col > 4 and col < 28 and col != 12 and col != 20:
				_grid_blocked[row * GRID_WIDTH + col] = 1


func _astar_find_path(start_x: int, start_y: int, goal_x: int, goal_y: int) -> int:
	var cell_count: int = GRID_WIDTH * GRID_HEIGHT
	var g_scores := PackedInt32Array()
	g_scores.resize(cell_count)
	g_scores.fill(1000000)

	var closed := PackedByteArray()
	closed.resize(cell_count)
	closed.fill(0)

	var heap_nodes: Array[int] = []
	var heap_priorities: Array[int] = []

	var start_index: int = start_y * GRID_WIDTH + start_x
	var goal_index: int = goal_y * GRID_WIDTH + goal_x
	g_scores[start_index] = 0
	heap_nodes.append(start_index)
	heap_priorities.append(absi(goal_x - start_x) + absi(goal_y - start_y))

	var neighbor_dx: Array[int] = [1, -1, 0, 0]
	var neighbor_dy: Array[int] = [0, 0, 1, -1]
	var expanded: int = 0

	while not heap_nodes.is_empty():
		var current_index: int = heap_nodes[0]
		var last_index: int = heap_nodes.size() - 1
		if last_index == 0:
			heap_nodes.pop_back()
			heap_priorities.pop_back()
		else:
			heap_nodes[0] = heap_nodes.pop_back()
			heap_priorities[0] = heap_priorities.pop_back()
			var parent: int = 0
			var heap_size: int = heap_nodes.size()
			while true:
				var left_child: int = parent * 2 + 1
				if left_child >= heap_size:
					break
				var smallest: int = left_child
				var right_child: int = left_child + 1
				if right_child < heap_size and heap_priorities[right_child] < heap_priorities[left_child]:
					smallest = right_child
				if heap_priorities[parent] <= heap_priorities[smallest]:
					break
				var swap_node: int = heap_nodes[parent]
				heap_nodes[parent] = heap_nodes[smallest]
				heap_nodes[smallest] = swap_node
				var swap_priority: int = heap_priorities[parent]
				heap_priorities[parent] = heap_priorities[smallest]
				heap_priorities[smallest] = swap_priority
				parent = smallest

		if closed[current_index] == 1:
			continue
		closed[current_index] = 1
		expanded += 1

		if current_index == goal_index:
			return g_scores[current_index] * 1000 + expanded

		var current_x: int = current_index % GRID_WIDTH
		var current_y: int = current_index / GRID_WIDTH
		var tentative_g: int = g_scores[current_index] + 1

		for direction: int in range(4):
			var next_x: int = current_x + neighbor_dx[direction]
			var next_y: int = current_y + neighbor_dy[direction]
			if next_x < 0 or next_x >= GRID_WIDTH or next_y < 0 or next_y >= GRID_HEIGHT:
				continue
			var next_index: int = next_y * GRID_WIDTH + next_x
			if _grid_blocked[next_index] == 1 or closed[next_index] == 1:
				continue
			if tentative_g < g_scores[next_index]:
				g_scores[next_index] = tentative_g
				var priority: int = tentative_g + absi(goal_x - next_x) + absi(goal_y - next_y)
				heap_nodes.append(next_index)
				heap_priorities.append(priority)
				var child: int = heap_nodes.size() - 1
				while child > 0:
					var parent_index: int = (child - 1) >> 1
					if heap_priorities[parent_index] <= heap_priorities[child]:
						break
					var parent_node: int = heap_nodes[parent_index]
					heap_nodes[parent_index] = heap_nodes[child]
					heap_nodes[child] = parent_node
					var parent_priority: int = heap_priorities[parent_index]
					heap_priorities[parent_index] = heap_priorities[child]
					heap_priorities[child] = parent_priority
					child = parent_index

	return -expanded


func _bench_astar_grid_2d(runs: int) -> int:
	var total: int = 0
	for run_index: int in range(runs):
		if (run_index & 1) == 0:
			total += _astar_find_path(1, 1, 30, 30)
		else:
			total += _astar_find_path(30, 2, 2, 29)
	return total


func _setup_node_tree(branch_count: int, leaves_per_branch: int) -> void:
	_teardown_node_tree()
	_tree_root = Node2D.new()
	for branch_index: int in range(branch_count):
		var branch := Node2D.new()
		_tree_root.add_child(branch)
		for leaf_index: int in range(leaves_per_branch):
			var leaf := Node2D.new()
			branch.add_child(leaf)
	_reset_node_tree_transforms()


func _reset_node_tree_transforms() -> void:
	var branches := _tree_root.get_children()
	for branch_index: int in range(branches.size()):
		var branch: Node2D = branches[branch_index] as Node2D
		branch.position = Vector2(float(branch_index) * 10.0, 0.0)
		branch.rotation = 0.0
		var leaves := branch.get_children()
		for leaf_index: int in range(leaves.size()):
			var leaf: Node2D = leaves[leaf_index] as Node2D
			leaf.position = Vector2(float(leaf_index) * 2.0, float(branch_index))
			leaf.rotation = 0.0


func _teardown_node_tree() -> void:
	if _tree_root != null:
		_tree_root.free()
		_tree_root = null


func _bench_node2d_tree_update(ticks: int) -> int:
	var accumulator: float = 0.0
	for tick: int in range(ticks):
		var phase: float = float(tick) * 0.05
		for branch_node: Node in _tree_root.get_children():
			var branch: Node2D = branch_node as Node2D
			branch.rotation = phase
			branch.position = Vector2(branch.position.x + 0.1, phase)
			for leaf_node: Node in branch.get_children():
				var leaf: Node2D = leaf_node as Node2D
				leaf.position = Vector2(leaf.position.x + 0.05, leaf.position.y - 0.05)
				leaf.rotation = phase * 0.5
				accumulator += leaf.position.x + leaf.rotation
	return int(accumulator)


func _write_varint(buffer: PackedByteArray, offset: int, raw_value: int) -> int:
	var value: int = raw_value
	while value >= 0x80:
		buffer[offset] = (value & 0x7F) | 0x80
		offset += 1
		value = value >> 7
	buffer[offset] = value & 0x7F
	return offset + 1


func _init_packet_payload(record_count: int) -> void:
	var scratch := PackedByteArray()
	scratch.resize(record_count * 32)
	var offset: int = 0
	var seed_value: int = 918273
	for index: int in range(record_count):
		seed_value = (seed_value * 1103515245 + 12345) & 0x7fffffff
		var entity_id: int = 1000 + index * 37 + (seed_value & 0xFF)
		var flags: int = (seed_value >> 8) & 0x3F
		var tile_x: int = (seed_value >> 14) & 0x3FF
		var tile_y: int = (seed_value >> 18) & 0x3FF
		var health: int = 10 + ((seed_value >> 4) % 990)
		var delta_x: int = ((seed_value >> 3) % 31) - 15
		var delta_y: int = ((seed_value >> 9) % 31) - 15
		var zigzag_x: int = (delta_x << 1) ^ (delta_x >> 63)
		var zigzag_y: int = (delta_y << 1) ^ (delta_y >> 63)

		offset = _write_varint(scratch, offset, (1 << 3) | 0)
		offset = _write_varint(scratch, offset, entity_id)
		offset = _write_varint(scratch, offset, (2 << 3) | 0)
		offset = _write_varint(scratch, offset, flags)
		offset = _write_varint(scratch, offset, (3 << 3) | 0)
		offset = _write_varint(scratch, offset, tile_x)
		offset = _write_varint(scratch, offset, (4 << 3) | 0)
		offset = _write_varint(scratch, offset, tile_y)
		offset = _write_varint(scratch, offset, (5 << 3) | 0)
		offset = _write_varint(scratch, offset, health)
		offset = _write_varint(scratch, offset, (6 << 3) | 0)
		offset = _write_varint(scratch, offset, zigzag_x)
		offset = _write_varint(scratch, offset, (7 << 3) | 0)
		offset = _write_varint(scratch, offset, zigzag_y)
		scratch[offset] = 0
		offset += 1
	scratch.resize(offset)
	_packet_payload = scratch


func _bench_varint_packet_codec(batches: int) -> int:
	var payload: PackedByteArray = _packet_payload
	var payload_size: int = payload.size()
	var encode_buffer := PackedByteArray()
	encode_buffer.resize(payload_size + 64)
	var checksum: int = 0

	for batch_index: int in range(batches):
		var decoded: Array[PacketRecord] = []
		var cursor: int = 0
		while cursor < payload_size:
			var entity_id: int = 0
			var flags: int = 0
			var tile_x: int = 0
			var tile_y: int = 0
			var health: int = 0
			var delta_x: int = 0
			var delta_y: int = 0
			while cursor < payload_size:
				var tag: int = payload[cursor]
				cursor += 1
				if tag == 0:
					break
				var field_number: int = tag >> 3
				var value: int = 0
				var shift: int = 0
				while true:
					var byte_value: int = payload[cursor]
					cursor += 1
					value |= (byte_value & 0x7F) << shift
					if (byte_value & 0x80) == 0:
						break
					shift += 7
				match field_number:
					1:
						entity_id = value
					2:
						flags = value
					3:
						tile_x = value
					4:
						tile_y = value
					5:
						health = value
					6:
						delta_x = (value >> 1) ^ -(value & 1)
					7:
						delta_y = (value >> 1) ^ -(value & 1)
			decoded.append(PacketRecord.new(entity_id, flags, tile_x, tile_y, health, delta_x, delta_y))

		var write_offset: int = 0
		for record: PacketRecord in decoded:
			var updated_health: int = clampi(record.health + record.delta_x - record.delta_y, 0, 1000)
			write_offset = _write_varint(encode_buffer, write_offset, record.entity_id)
			write_offset = _write_varint(encode_buffer, write_offset, updated_health)
			checksum = (checksum + record.tile_x * 3 + record.tile_y * 5 + updated_health + record.flags) & 0x3FFFFFFF
		checksum = (checksum + write_offset + batch_index) & 0x3FFFFFFF

	return checksum


func _init_visibility_grid() -> void:
	var cell_count: int = VISIBILITY_WIDTH * VISIBILITY_HEIGHT
	_visibility_grid.resize(cell_count)
	_visibility_elevation.resize(cell_count)
	for row: int in range(VISIBILITY_HEIGHT):
		for col: int in range(VISIBILITY_WIDTH):
			var cell_index: int = row * VISIBILITY_WIDTH + col
			var is_pillar: bool = ((col % 9) == 4 and (row % 7) == 3) or ((col % 13) == 8 and (row % 11) == 5)
			_visibility_grid[cell_index] = 1 if is_pillar else 0
			_visibility_elevation[cell_index] = ((col * 7 + row * 11) & 3)


func _cast_grid_ray(origin: Vector2i, target: Vector2i) -> int:
	var current: Vector2i = origin
	var delta_x: int = absi(target.x - origin.x)
	var delta_y: int = absi(target.y - origin.y)
	var step_x: int = 1 if origin.x < target.x else -1
	var step_y: int = 1 if origin.y < target.y else -1
	var error: int = delta_x - delta_y
	var visible_steps: int = 0

	while true:
		var cell_index: int = current.y * VISIBILITY_WIDTH + current.x
		if _visibility_grid[cell_index] != 0:
			return visible_steps
		visible_steps += 1 + _visibility_elevation[cell_index]
		if current == target:
			return visible_steps
		var doubled_error: int = error * 2
		if doubled_error > -delta_y:
			error -= delta_y
			current = Vector2i(current.x + step_x, current.y)
		if doubled_error < delta_x:
			error += delta_x
			current = Vector2i(current.x, current.y + step_y)
	return visible_steps


func _bench_tile_grid_visibility(scans: int) -> int:
	var directions: Array[Vector2i] = [
		Vector2i(1, 0),
		Vector2i(-1, 0),
		Vector2i(0, 1),
		Vector2i(0, -1),
	]
	var checksum: int = 0

	for scan_index: int in range(scans):
		var origin := Vector2i(16 + (scan_index % 32), 16 + ((scan_index * 7) % 32))
		var visited: Dictionary = {}
		var frontier: Array[Vector2i] = [origin]
		visited[origin] = 0
		var read_head: int = 0

		while read_head < frontier.size():
			var tile: Vector2i = frontier[read_head]
			read_head += 1
			var distance: int = visited[tile]
			if distance >= 7:
				continue
			for direction: Vector2i in directions:
				var neighbor: Vector2i = tile + direction
				if neighbor.x < 0 or neighbor.x >= VISIBILITY_WIDTH or neighbor.y < 0 or neighbor.y >= VISIBILITY_HEIGHT:
					continue
				if visited.has(neighbor):
					continue
				var cell_index: int = neighbor.y * VISIBILITY_WIDTH + neighbor.x
				if _visibility_grid[cell_index] != 0:
					continue
				visited[neighbor] = distance + 1
				frontier.append(neighbor)

		for ray_index: int in range(24):
			var offset_x: int = clampi(((ray_index * 5 + scan_index) % 21) - 10, -10, 10)
			var offset_y: int = clampi(((ray_index * 9 + scan_index * 3) % 21) - 10, -10, 10)
			var target := Vector2i(
				clampi(origin.x + offset_x, 0, VISIBILITY_WIDTH - 1),
				clampi(origin.y + offset_y, 0, VISIBILITY_HEIGHT - 1)
			)
			checksum = (checksum + _cast_grid_ray(origin, target)) & 0x3FFFFFFF

		checksum = (checksum + frontier.size() * 13 + visited.size()) & 0x3FFFFFFF

	return checksum


func _setup_ui_tree(binding_count: int) -> void:
	_teardown_ui_tree()
	_ui_root = Control.new()
	_ui_bindings.clear()
	for index: int in range(binding_count):
		var control := Control.new()
		_ui_root.add_child(control)
		var key: String = "slot_" + str(index)
		if (index & 1) == 0:
			_ui_bindings[key] = HealthBarBinding.new(key, control)
		else:
			_ui_bindings[key] = StatusBadgeBinding.new(key, control)


func _reset_ui_bindings() -> void:
	for key: String in _ui_bindings:
		var binding: UiBinding = _ui_bindings[key]
		binding.last_value = -1


func _teardown_ui_tree() -> void:
	_ui_bindings.clear()
	if _ui_root != null:
		_ui_root.free()
		_ui_root = null


func _bench_ui_state_dispatch(frames: int) -> int:
	var state_store: Dictionary = {}
	var keys: Array[String] = []
	for key_variant: Variant in _ui_bindings.keys():
		keys.append(str(key_variant))
	var key_count: int = keys.size()
	var checksum: int = 0

	for frame_index: int in range(frames):
		var events: Array[StateEvent] = []
		for event_index: int in range(key_count):
			var target_key: String = keys[event_index]
			if (event_index & 1) == 0:
				events.append(HealthEvent.new(target_key, (frame_index * 17 + event_index * 13) % 120, 100))
			else:
				events.append(StatusEvent.new(target_key, (frame_index + event_index) % 16, ((frame_index + event_index) & 1) == 0))

		for event: StateEvent in events:
			var binding: UiBinding = _ui_bindings.get(event.target_key)
			if binding != null:
				checksum = (checksum + binding.apply_event(event, state_store)) & 0x3FFFFFFF

	for key: String in keys:
		checksum = (checksum + int(state_store.get(key, 0))) & 0x3FFFFFFF
	return checksum
