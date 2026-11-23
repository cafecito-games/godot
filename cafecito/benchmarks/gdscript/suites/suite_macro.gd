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


var _sim_actors: Array[SimActor] = []
var _grid_blocked: PackedByteArray = PackedByteArray()
var _tree_root: Node2D = null
const GRID_WIDTH: int = 32
const GRID_HEIGHT: int = 32


static func build_cases(scale: float) -> Array:
	var suite := new()
	suite._init_grid()
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
