extends RefCounted

const BenchCase = preload("res://bench_case.gd")


class DataRecord extends RefCounted:
	var counter: int = 0
	var weight: float = 1.5
	var _backed_value: int = 0
	var computed_value: int:
		get:
			return _backed_value
		set(new_value):
			_backed_value = new_value


class NativeMemberNode extends Node2D:
	func run_self_native_member_loop(iterations: int) -> int:
		var accumulator: float = 0.0
		for index: int in range(iterations):
			var offset: float = float(index & 255)
			position = Vector2(offset, offset * 0.5)
			rotation = offset * 0.01
			accumulator += position.x + position.y + rotation
		return int(accumulator)


var _self_counter: int = 0
var _self_weight: float = 1.25
var _record: DataRecord = DataRecord.new()
var _native_member_node: NativeMemberNode = null
var _typed_array: Array[int] = []
var _untyped_array: Array = []
var _packed_floats: PackedFloat32Array = PackedFloat32Array()
var _packed_vectors: PackedVector2Array = PackedVector2Array()
var _typed_dict: Dictionary[int, int] = {}
var _untyped_string_dict: Dictionary = {}
var _string_keys: Array[StringName] = []


static func build_cases(scale: float) -> Array:
	var suite := new()
	suite._init_containers()
	var cases: Array = []

	var self_member_count := BenchCase.scaled_count(500000, scale)
	cases.append(BenchCase.new(
		"member_self_direct",
		"access",
		"Direct read/write of self GDScript member variables (ADDR_TYPE_MEMBER)",
		self_member_count,
		func() -> int: return suite._bench_member_self_direct(self_member_count)
	))

	var native_member_count := BenchCase.scaled_count(200000, scale)
	cases.append(BenchCase.new(
		"member_native_self",
		"access",
		"Read/write inherited Node2D native properties via OPCODE_GET_MEMBER/SET_MEMBER",
		native_member_count,
		func() -> int: return suite._native_member_node.run_self_native_member_loop(native_member_count),
		func() -> void: suite._setup_native_node(),
		func() -> void: suite._teardown_native_node()
	))
	cases.append(BenchCase.new(
		"member_other_native_scripted",
		"access",
		"Read/write native Node2D properties on another scripted instance via GET_NAMED/SET_NAMED",
		native_member_count,
		func() -> int: return suite._bench_member_other_native_scripted(native_member_count),
		func() -> void: suite._setup_native_node(),
		func() -> void: suite._teardown_native_node()
	))

	var other_member_count := BenchCase.scaled_count(300000, scale)
	cases.append(BenchCase.new(
		"member_other_typed",
		"access",
		"Read/write fields on another typed GDScript instance (GET_NAMED/SET_NAMED)",
		other_member_count,
		func() -> int: return suite._bench_member_other_typed(other_member_count)
	))
	cases.append(BenchCase.new(
		"member_other_untyped",
		"access",
		"Read/write fields on another untyped GDScript instance",
		other_member_count,
		func() -> int: return suite._bench_member_other_untyped(other_member_count)
	))

	var getter_setter_count := BenchCase.scaled_count(200000, scale)
	cases.append(BenchCase.new(
		"member_getter_setter",
		"access",
		"Read/write property backed by GDScript getter and setter",
		getter_setter_count,
		func() -> int: return suite._bench_member_getter_setter(getter_setter_count)
	))

	var array_index_count := BenchCase.scaled_count(400000, scale)
	cases.append(BenchCase.new(
		"array_typed_index",
		"access",
		"Indexed read/write on typed Array[int]",
		array_index_count,
		func() -> int: return suite._bench_array_typed_index(array_index_count),
		Callable(),
		Callable(),
		func() -> void: suite._reset_arrays()
	))
	cases.append(BenchCase.new(
		"array_untyped_index",
		"access",
		"Indexed read/write on untyped Array",
		array_index_count,
		func() -> int: return suite._bench_array_untyped_index(array_index_count),
		Callable(),
		Callable(),
		func() -> void: suite._reset_arrays()
	))

	var array_iter_rounds := BenchCase.scaled_count(400, scale)
	cases.append(BenchCase.new(
		"array_typed_iterate",
		"access",
		"for-in iteration over typed Array[int] of 1024 elements",
		array_iter_rounds * 1024,
		func() -> int: return suite._bench_array_typed_iterate(array_iter_rounds),
		func() -> void: suite._reset_arrays()
	))

	var packed_index_count := BenchCase.scaled_count(400000, scale)
	cases.append(BenchCase.new(
		"packed_float32_index",
		"access",
		"Indexed read/write on PackedFloat32Array",
		packed_index_count,
		func() -> int: return suite._bench_packed_float32_index(packed_index_count),
		Callable(),
		Callable(),
		func() -> void: suite._reset_packed_floats()
	))

	var packed_vec_rounds := BenchCase.scaled_count(400, scale)
	cases.append(BenchCase.new(
		"packed_vector2_iterate",
		"access",
		"for-in iteration over PackedVector2Array of 1024 elements",
		packed_vec_rounds * 1024,
		func() -> int: return suite._bench_packed_vector2_iterate(packed_vec_rounds)
	))

	var dict_count := BenchCase.scaled_count(250000, scale)
	cases.append(BenchCase.new(
		"dict_typed_int_lookup",
		"access",
		"Lookup and update on Dictionary[int, int]",
		dict_count,
		func() -> int: return suite._bench_dict_typed_int(dict_count),
		Callable(),
		Callable(),
		func() -> void: suite._reset_dicts()
	))
	cases.append(BenchCase.new(
		"dict_untyped_string_lookup",
		"access",
		"Lookup and update on untyped Dictionary with StringName keys",
		dict_count,
		func() -> int: return suite._bench_dict_untyped_string(dict_count),
		Callable(),
		Callable(),
		func() -> void: suite._reset_dicts()
	))

	return cases


func _init_containers() -> void:
	_typed_array.resize(1024)
	_untyped_array.resize(1024)
	_packed_floats.resize(1024)
	_packed_vectors.resize(1024)
	for index: int in range(1024):
		_packed_vectors[index] = Vector2(float(index), float(index) * -0.5)
	_string_keys.clear()
	for key: int in range(256):
		_string_keys.append(StringName("key_%d" % key))
	_reset_arrays()
	_reset_packed_floats()
	_reset_dicts()


func _reset_arrays() -> void:
	for index: int in range(1024):
		var initial_value: int = index * 3 + 1
		_typed_array[index] = initial_value
		_untyped_array[index] = initial_value


func _reset_packed_floats() -> void:
	for index: int in range(1024):
		_packed_floats[index] = float(index) * 0.25


func _reset_dicts() -> void:
	for key: int in range(256):
		var initial_value: int = key * 7
		_typed_dict[key] = initial_value
		_untyped_string_dict[_string_keys[key]] = initial_value


func _setup_native_node() -> void:
	_teardown_native_node()
	_native_member_node = NativeMemberNode.new()


func _teardown_native_node() -> void:
	if _native_member_node != null:
		_native_member_node.free()
		_native_member_node = null


func _bench_member_self_direct(iterations: int) -> int:
	_self_counter = 1
	_self_weight = 1.25
	for index: int in range(iterations):
		_self_counter = (_self_counter + (index & 15)) & 0x3fffffff
		_self_weight = _self_weight * 0.9999 + 0.01
	return _self_counter + int(_self_weight * 100.0)


func _bench_member_other_native_scripted(iterations: int) -> int:
	var node: NativeMemberNode = _native_member_node
	var accumulator: float = 0.0
	for index: int in range(iterations):
		var offset: float = float(index & 255)
		node.position = Vector2(offset, offset * 0.5)
		node.rotation = offset * 0.01
		accumulator += node.position.x + node.position.y + node.rotation
	return int(accumulator)


func _bench_member_other_typed(iterations: int) -> int:
	var record: DataRecord = _record
	record.counter = 1
	record.weight = 1.5
	for index: int in range(iterations):
		record.counter = (record.counter + (index & 15)) & 0x3fffffff
		record.weight = record.weight * 0.9999 + 0.01
	return record.counter + int(record.weight * 100.0)


func _bench_member_other_untyped(iterations):
	var record = _record
	record.counter = 1
	record.weight = 1.5
	var index = 0
	while index < iterations:
		record.counter = (record.counter + (index & 15)) & 0x3fffffff
		record.weight = record.weight * 0.9999 + 0.01
		index += 1
	return record.counter + int(record.weight * 100.0)


func _bench_member_getter_setter(iterations: int) -> int:
	var record: DataRecord = _record
	record.computed_value = 1
	for index: int in range(iterations):
		record.computed_value = (record.computed_value + (index & 7)) & 0x3fffffff
	return record.computed_value


func _bench_array_typed_index(iterations: int) -> int:
	var data: Array[int] = _typed_array
	var total: int = 0
	for index: int in range(iterations):
		var slot: int = index & 1023
		var value: int = data[slot]
		data[slot] = (value + 1) & 0xffff
		total = (total + value) & 0x3fffffff
	return total


func _bench_array_untyped_index(iterations):
	var data = _untyped_array
	var total = 0
	var index = 0
	while index < iterations:
		var slot = index & 1023
		var value = data[slot]
		data[slot] = (value + 1) & 0xffff
		total = (total + value) & 0x3fffffff
		index += 1
	return total


func _bench_array_typed_iterate(rounds: int) -> int:
	var data: Array[int] = _typed_array
	var total: int = 0
	for round_index: int in range(rounds):
		for value: int in data:
			total = (total + value) & 0x3fffffff
	return total


func _bench_packed_float32_index(iterations: int) -> int:
	var data: PackedFloat32Array = _packed_floats
	var accumulator: float = 0.0
	for index: int in range(iterations):
		var slot: int = index & 1023
		var value: float = data[slot]
		data[slot] = value * 0.999 + 0.001
		accumulator += value
	return int(accumulator * 10.0)


func _bench_packed_vector2_iterate(rounds: int) -> int:
	var data: PackedVector2Array = _packed_vectors
	var accumulator: float = 0.0
	for round_index: int in range(rounds):
		for point: Vector2 in data:
			accumulator += point.x - point.y
	return int(accumulator)


func _bench_dict_typed_int(iterations: int) -> int:
	var table: Dictionary[int, int] = _typed_dict
	var total: int = 0
	for index: int in range(iterations):
		var key: int = index & 255
		var current: int = table[key]
		table[key] = (current + 1) & 0xffff
		total = (total + current) & 0x3fffffff
	return total


func _bench_dict_untyped_string(iterations):
	var table = _untyped_string_dict
	var keys = _string_keys
	var total = 0
	var index = 0
	while index < iterations:
		var key = keys[index & 255]
		var current = table[key]
		table[key] = (current + 1) & 0xffff
		total = (total + current) & 0x3fffffff
		index += 1
	return total
