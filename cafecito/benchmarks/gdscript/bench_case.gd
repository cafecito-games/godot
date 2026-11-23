extends RefCounted

var name: String
var category: String
var description: String
var operations_per_sample: int
var run_callable: Callable
var setup_callable: Callable
var before_sample_callable: Callable
var teardown_callable: Callable


func _init(
	p_name: String,
	p_category: String,
	p_description: String,
	p_operations_per_sample: int,
	p_run_callable: Callable,
	p_setup_callable: Callable = Callable(),
	p_teardown_callable: Callable = Callable(),
	p_before_sample_callable: Callable = Callable()
) -> void:
	name = p_name
	category = p_category
	description = p_description
	operations_per_sample = p_operations_per_sample
	run_callable = p_run_callable
	setup_callable = p_setup_callable
	teardown_callable = p_teardown_callable
	before_sample_callable = p_before_sample_callable


static func scaled_count(base_count: int, scale: float) -> int:
	return maxi(1, int(round(float(base_count) * scale)))
