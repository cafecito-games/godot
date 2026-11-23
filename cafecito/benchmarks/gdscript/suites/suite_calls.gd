extends RefCounted

const BenchCase = preload("res://bench_case.gd")


class TargetWorker extends RefCounted:
	var factor: int = 3

	func compute(value_a: int, value_b: int) -> int:
		return (value_a * factor + value_b) & 0x3fffffff


class PolyBase extends RefCounted:
	func evaluate(value: int) -> int:
		return value + 1


class PolyDouble extends PolyBase:
	func evaluate(value: int) -> int:
		return value * 2 + 1


class PolyTriple extends PolyBase:
	func evaluate(value: int) -> int:
		return value * 3 - 2


class PolyXor extends PolyBase:
	func evaluate(value: int) -> int:
		return (value ^ 0x55) + 3


var _worker: TargetWorker = TargetWorker.new()
var _poly_objects: Array[PolyBase] = []
var _node: Node2D = null


static func build_cases(scale: float) -> Array:
	var suite := new()
	suite._poly_objects = [PolyDouble.new(), PolyTriple.new(), PolyXor.new()]
	var cases: Array = []

	var self_call_count := BenchCase.scaled_count(300000, scale)
	cases.append(BenchCase.new(
		"call_gdscript_self_typed",
		"calls",
		"Typed GDScript method call on self",
		self_call_count,
		func() -> int: return suite._bench_call_self_typed(self_call_count)
	))

	var other_call_count := BenchCase.scaled_count(300000, scale)
	cases.append(BenchCase.new(
		"call_gdscript_other_typed",
		"calls",
		"Typed GDScript method call on another script instance",
		other_call_count,
		func() -> int: return suite._bench_call_other_typed(other_call_count)
	))
	cases.append(BenchCase.new(
		"call_gdscript_other_untyped",
		"calls",
		"Untyped GDScript method call on another script instance",
		other_call_count,
		func() -> int: return suite._bench_call_other_untyped(other_call_count)
	))

	var poly_call_count := BenchCase.scaled_count(240000, scale)
	cases.append(BenchCase.new(
		"call_gdscript_polymorphic",
		"calls",
		"Polymorphic GDScript method dispatch across 3 subclasses",
		poly_call_count,
		func() -> int: return suite._bench_call_polymorphic(poly_call_count)
	))

	# Recursive fib(26) executes 392,835 calls; scale adjusts repeat count.
	var fib_repeats := BenchCase.scaled_count(1, scale)
	var fib_depth: int = 26 if scale >= 0.5 else 22
	cases.append(BenchCase.new(
		"call_gdscript_recursive_fib",
		"calls",
		"Recursive GDScript Fibonacci stressing call frame setup/teardown",
		fib_repeats * (392835 if fib_depth == 26 else 57313),
		func() -> int: return suite._bench_recursive_fib(fib_depth, fib_repeats)
	))

	var native_call_count := BenchCase.scaled_count(250000, scale)
	cases.append(BenchCase.new(
		"call_native_method_typed",
		"calls",
		"Typed native Node2D MethodBind calls (set_position/get_position/get_rotation)",
		native_call_count,
		func() -> int: return suite._bench_native_typed(native_call_count),
		func() -> void: suite._setup_node(),
		func() -> void: suite._teardown_node()
	))
	cases.append(BenchCase.new(
		"call_native_method_untyped",
		"calls",
		"Untyped native Node2D method calls",
		native_call_count,
		func() -> int: return suite._bench_native_untyped(native_call_count),
		func() -> void: suite._setup_node(),
		func() -> void: suite._teardown_node()
	))

	var utility_call_count := BenchCase.scaled_count(300000, scale)
	cases.append(BenchCase.new(
		"call_builtin_and_utility_typed",
		"calls",
		"Typed builtin methods and math utility functions (clampf/lerpf/absf/distance_squared_to)",
		utility_call_count,
		func() -> int: return suite._bench_builtin_and_utility_typed(utility_call_count)
	))

	var lambda_call_count := BenchCase.scaled_count(200000, scale)
	cases.append(BenchCase.new(
		"call_lambda_typed",
		"calls",
		"Callable.call on capturing and non-capturing GDScript lambdas",
		lambda_call_count,
		func() -> int: return suite._bench_lambda_typed(lambda_call_count)
	))

	return cases


func _setup_node() -> void:
	_teardown_node()
	_node = Node2D.new()


func _teardown_node() -> void:
	if _node != null:
		_node.free()
		_node = null


func _self_helper(value_a: int, value_b: int) -> int:
	return (value_a * 3 + value_b) & 0x3fffffff


func _bench_call_self_typed(iterations: int) -> int:
	var total: int = 1
	for index: int in range(iterations):
		total = _self_helper(total, index)
	return total


func _bench_call_other_typed(iterations: int) -> int:
	var worker: TargetWorker = _worker
	var total: int = 1
	for index: int in range(iterations):
		total = worker.compute(total, index)
	return total


func _bench_call_other_untyped(iterations):
	var worker = _worker
	var total = 1
	var index = 0
	while index < iterations:
		total = worker.compute(total, index)
		index += 1
	return total


func _bench_call_polymorphic(iterations: int) -> int:
	var objects: Array[PolyBase] = _poly_objects
	var object_count: int = objects.size()
	var total: int = 7
	for index: int in range(iterations):
		var target: PolyBase = objects[index % object_count]
		total = (total + target.evaluate(index & 1023)) & 0x3fffffff
	return total


func _fib_typed(number: int) -> int:
	if number <= 1:
		return number
	return _fib_typed(number - 1) + _fib_typed(number - 2)


func _bench_recursive_fib(depth: int, repeats: int) -> int:
	var total: int = 0
	for index: int in range(repeats):
		total += _fib_typed(depth)
	return total


func _bench_native_typed(iterations: int) -> int:
	var node: Node2D = _node
	var accumulator: float = 0.0
	for index: int in range(iterations):
		var step: float = float(index & 255)
		node.set_position(Vector2(step, step * 0.5))
		var current_position: Vector2 = node.get_position()
		accumulator += current_position.x + current_position.y + node.get_rotation()
	return int(accumulator)


func _bench_native_untyped(iterations):
	var node = _node
	var accumulator = 0.0
	var index = 0
	while index < iterations:
		var step = float(index & 255)
		node.set_position(Vector2(step, step * 0.5))
		var current_position = node.get_position()
		accumulator += current_position.x + current_position.y + node.get_rotation()
		index += 1
	return int(accumulator)


func _bench_builtin_and_utility_typed(iterations: int) -> int:
	var origin := Vector2(12.5, -4.25)
	var target := Vector2(-3.0, 8.5)
	var value: float = 0.25
	var accumulator: float = 0.0
	for index: int in range(iterations):
		value = clampf(lerpf(value, float(index & 15), 0.1), 0.0, 20.0)
		var distance_squared: float = origin.distance_squared_to(target + Vector2(value, -value))
		accumulator += absf(distance_squared - 100.0)
	return int(accumulator)


func _bench_lambda_typed(iterations: int) -> int:
	var multiplier: int = 5
	var non_capturing := func(value: int) -> int:
		return (value * 3 + 1) & 0x3fffffff
	var capturing := func(value: int) -> int:
		return (value * multiplier + 7) & 0x3fffffff
	var total: int = 1
	for index: int in range(iterations):
		if (index & 1) == 0:
			total = non_capturing.call(total)
		else:
			total = capturing.call(total)
	return total
