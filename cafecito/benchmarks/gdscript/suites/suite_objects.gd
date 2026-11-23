extends RefCounted

const BenchCase = preload("res://bench_case.gd")


class SmallEntity extends RefCounted:
	var entity_id: int
	var position: Vector2
	var health: float

	func _init(p_entity_id: int, p_position: Vector2, p_health: float) -> void:
		entity_id = p_entity_id
		position = p_position
		health = p_health


class BaseComponent extends RefCounted:
	var kind: int = 1


class DerivedComponent extends BaseComponent:
	var bonus: int = 2


var _mixed_values: Array = []
var _node_objects: Array[Node2D] = []


static func build_cases(scale: float) -> Array:
	var suite := new()
	var cases: Array = []

	var instantiate_count := BenchCase.scaled_count(80000, scale)
	cases.append(BenchCase.new(
		"object_instantiate_refcounted",
		"objects",
		"Allocate, initialize, and release RefCounted GDScript instances",
		instantiate_count,
		func() -> int: return suite._bench_instantiate_refcounted(instantiate_count)
	))

	var type_check_count := BenchCase.scaled_count(200000, scale)
	cases.append(BenchCase.new(
		"type_check_and_cast",
		"objects",
		"Runtime `is` and `as` checks across builtin, native Node2D, and GDScript classes",
		type_check_count,
		func() -> int: return suite._bench_type_check_and_cast(type_check_count),
		func() -> void: suite._setup_type_objects(),
		func() -> void: suite._teardown_type_objects()
	))

	var string_count := BenchCase.scaled_count(100000, scale)
	cases.append(BenchCase.new(
		"string_ops",
		"objects",
		"String concatenation, formatting, and StringName comparisons",
		string_count,
		func() -> int: return suite._bench_string_ops(string_count)
	))

	var construct_count := BenchCase.scaled_count(100000, scale)
	cases.append(BenchCase.new(
		"container_construct",
		"objects",
		"Literal Array and Dictionary construction in a loop",
		construct_count,
		func() -> int: return suite._bench_container_construct(construct_count)
	))

	return cases


func _setup_type_objects() -> void:
	_teardown_type_objects()
	var node_a := Node2D.new()
	var node_b := Node2D.new()
	_node_objects = [node_a, node_b]
	_mixed_values = [
		42,
		"hello",
		Vector2(1.0, 2.0),
		node_a,
		BaseComponent.new(),
		DerivedComponent.new(),
		node_b,
		3.14,
	]


func _teardown_type_objects() -> void:
	_mixed_values.clear()
	for node: Node2D in _node_objects:
		if node != null:
			node.free()
	_node_objects.clear()


func _bench_instantiate_refcounted(iterations: int) -> int:
	var total: int = 0
	for index: int in range(iterations):
		var entity := SmallEntity.new(index, Vector2(float(index), 1.0), 100.0)
		total = (total + entity.entity_id) & 0x3fffffff
	return total


func _bench_type_check_and_cast(iterations: int) -> int:
	var values: Array = _mixed_values
	var total: int = 0
	for index: int in range(iterations):
		var item: Variant = values[index & 7]
		if item is int:
			total += int(item)
		elif item is Node2D:
			var node_item := item as Node2D
			if node_item != null:
				total += 11
		elif item is DerivedComponent:
			var derived := item as DerivedComponent
			if derived != null:
				total += derived.bonus
		elif item is BaseComponent:
			var base_item := item as BaseComponent
			if base_item != null:
				total += base_item.kind
		else:
			total += 1
	return total & 0x3fffffff


func _bench_string_ops(iterations: int) -> int:
	var prefix: String = "entity_"
	var target_name := &"entity_42"
	var total: int = 0
	for index: int in range(iterations):
		var suffix_id: int = index & 63
		var built: String = prefix + str(suffix_id)
		var name_value := StringName(built)
		if name_value == target_name:
			total += 10
		total += built.length()
	return total & 0x3fffffff


func _bench_container_construct(iterations: int) -> int:
	var total: int = 0
	for index: int in range(iterations):
		var pair_array: Array[int] = [index, index + 1, index + 2, index + 3]
		var entry: Dictionary = { "id": index, "value": pair_array[2] }
		total = (total + int(entry["value"])) & 0x3fffffff
	return total
