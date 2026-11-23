extends RefCounted

const BenchCase = preload("res://bench_case.gd")


static func build_cases(scale: float) -> Array:
	var suite := new()
	var cases: Array = []

	var int_count := BenchCase.scaled_count(500000, scale)
	cases.append(BenchCase.new(
		"arith_int_typed",
		"arithmetic",
		"Tight typed integer arithmetic, bitwise ops, and branches",
		int_count,
		func() -> int: return suite._bench_int_typed(int_count)
	))
	cases.append(BenchCase.new(
		"arith_int_untyped",
		"arithmetic",
		"Tight untyped integer arithmetic, bitwise ops, and branches",
		int_count,
		func() -> int: return suite._bench_int_untyped(int_count)
	))

	var float_count := BenchCase.scaled_count(500000, scale)
	cases.append(BenchCase.new(
		"arith_float_typed",
		"arithmetic",
		"Tight typed float arithmetic and comparisons",
		float_count,
		func() -> int: return suite._bench_float_typed(float_count)
	))
	cases.append(BenchCase.new(
		"arith_float_untyped",
		"arithmetic",
		"Tight untyped float arithmetic and comparisons",
		float_count,
		func() -> int: return suite._bench_float_untyped(float_count)
	))

	var vector2_count := BenchCase.scaled_count(250000, scale)
	cases.append(BenchCase.new(
		"vector2_math_typed",
		"arithmetic",
		"Typed Vector2 addition, scaling, dot product, and length_squared",
		vector2_count,
		func() -> int: return suite._bench_vector2_typed(vector2_count)
	))
	cases.append(BenchCase.new(
		"vector2_math_untyped",
		"arithmetic",
		"Untyped Vector2 addition, scaling, dot product, and length_squared",
		vector2_count,
		func() -> int: return suite._bench_vector2_untyped(vector2_count)
	))

	var vector3_count := BenchCase.scaled_count(200000, scale)
	cases.append(BenchCase.new(
		"vector3_math_typed",
		"arithmetic",
		"Typed Vector3 addition, scaling, cross product, and dot product",
		vector3_count,
		func() -> int: return suite._bench_vector3_typed(vector3_count)
	))

	var loop_count := BenchCase.scaled_count(1000000, scale)
	cases.append(BenchCase.new(
		"loop_range_typed",
		"arithmetic",
		"Typed for-in-range loop counter accumulation",
		loop_count,
		func() -> int: return suite._bench_loop_range_typed(loop_count)
	))
	cases.append(BenchCase.new(
		"loop_while_typed",
		"arithmetic",
		"Typed while loop counter accumulation",
		loop_count,
		func() -> int: return suite._bench_loop_while_typed(loop_count)
	))

	return cases


func _bench_int_typed(iterations: int) -> int:
	var accumulator: int = 1234567
	var state: int = 987654321
	for index: int in range(iterations):
		state = ((state * 1103515245 + 12345) & 0x7fffffff)
		var delta: int = (state ^ index) - (index & 255)
		if (state & 1) == 0:
			accumulator = (accumulator + delta) & 0x3fffffff
		else:
			accumulator = (accumulator - delta) & 0x3fffffff
	return accumulator


func _bench_int_untyped(iterations):
	var accumulator = 1234567
	var state = 987654321
	var index = 0
	while index < iterations:
		state = ((state * 1103515245 + 12345) & 0x7fffffff)
		var delta = (state ^ index) - (index & 255)
		if (state & 1) == 0:
			accumulator = (accumulator + delta) & 0x3fffffff
		else:
			accumulator = (accumulator - delta) & 0x3fffffff
		index += 1
	return accumulator


func _bench_float_typed(iterations: int) -> int:
	var position_x: float = 0.25
	var velocity_x: float = 1.0001
	var damping: float = 0.99999
	var accumulator: float = 0.0
	for index: int in range(iterations):
		velocity_x = velocity_x * damping + 0.00003
		position_x = position_x + velocity_x * 0.016
		if position_x > 100.0:
			position_x -= 100.0
		accumulator += position_x * 0.5 - velocity_x * 0.25
	return int(accumulator * 1000.0)


func _bench_float_untyped(iterations):
	var position_x = 0.25
	var velocity_x = 1.0001
	var damping = 0.99999
	var accumulator = 0.0
	var index = 0
	while index < iterations:
		velocity_x = velocity_x * damping + 0.00003
		position_x = position_x + velocity_x * 0.016
		if position_x > 100.0:
			position_x -= 100.0
		accumulator += position_x * 0.5 - velocity_x * 0.25
		index += 1
	return int(accumulator * 1000.0)


func _bench_vector2_typed(iterations: int) -> int:
	var position := Vector2(1.0, 2.0)
	var velocity := Vector2(0.35, -0.25)
	var gravity := Vector2(0.0, 0.01)
	var accumulator: float = 0.0
	for index: int in range(iterations):
		velocity = (velocity + gravity) * 0.9999
		position = position + velocity * 0.016
		var dot_value: float = position.dot(velocity)
		if position.length_squared() > 2500.0:
			position = position * 0.1
		accumulator += dot_value
	return int(accumulator * 100.0)


func _bench_vector2_untyped(iterations):
	var position = Vector2(1.0, 2.0)
	var velocity = Vector2(0.35, -0.25)
	var gravity = Vector2(0.0, 0.01)
	var accumulator = 0.0
	var index = 0
	while index < iterations:
		velocity = (velocity + gravity) * 0.9999
		position = position + velocity * 0.016
		var dot_value = position.dot(velocity)
		if position.length_squared() > 2500.0:
			position = position * 0.1
		accumulator += dot_value
		index += 1
	return int(accumulator * 100.0)


func _bench_vector3_typed(iterations: int) -> int:
	var vector_a := Vector3(1.0, 2.0, 3.0)
	var vector_b := Vector3(0.25, -0.5, 0.75)
	var step := Vector3(0.001, -0.002, 0.003)
	var accumulator: float = 0.0
	for index: int in range(iterations):
		vector_a = (vector_a + step) * 0.9999
		var crossed: Vector3 = vector_a.cross(vector_b)
		accumulator += crossed.dot(vector_b)
		if vector_a.length_squared() > 1000.0:
			vector_a = Vector3(1.0, 2.0, 3.0)
	return int(accumulator * 100.0)


func _bench_loop_range_typed(iterations: int) -> int:
	var total: int = 0
	for index: int in range(iterations):
		total += index & 7
	return total


func _bench_loop_while_typed(iterations: int) -> int:
	var total: int = 0
	var index: int = 0
	while index < iterations:
		total += index & 7
		index += 1
	return total
