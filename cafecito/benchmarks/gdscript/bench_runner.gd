extends SceneTree

const BenchCaseScript = preload("res://bench_case.gd")
const SuiteArithmeticScript = preload("res://suites/suite_arithmetic.gd")
const SuiteCallsScript = preload("res://suites/suite_calls.gd")
const SuiteAccessScript = preload("res://suites/suite_access.gd")
const SuiteObjectsScript = preload("res://suites/suite_objects.gd")
const SuiteMacroScript = preload("res://suites/suite_macro.gd")

const RESULT_BEGIN_MARKER := "CAFECITO_BENCH_JSON_BEGIN"
const RESULT_END_MARKER := "CAFECITO_BENCH_JSON_END"


func _initialize() -> void:
	var config := _parse_args(OS.get_cmdline_user_args())
	if config["error"] != "":
		printerr("Argument error: %s" % config["error"])
		quit(2)
		return

	var scale: float = float(config["scale"])
	var all_cases: Array = _collect_cases(scale)
	var filter_text: String = str(config["filter"])
	var selected_cases: Array = []
	for bench_case in all_cases:
		if filter_text.is_empty() or bench_case.name.contains(filter_text) or bench_case.category.contains(filter_text):
			selected_cases.append(bench_case)

	if bool(config["list_only"]):
		for bench_case in selected_cases:
			print("%-12s %-32s %s" % [bench_case.category, bench_case.name, bench_case.description])
		quit(0)
		return

	if selected_cases.is_empty():
		printerr("No benchmarks matched filter: %s" % filter_text)
		quit(2)
		return

	var warmup_count: int = int(config["warmup"])
	var sample_count: int = int(config["samples"])
	var results: Array = []

	for bench_case in selected_cases:
		var case_result := _run_case(bench_case, warmup_count, sample_count)
		if not bool(case_result["checksum_consistent"]):
			printerr(
				"Non-deterministic checksum in benchmark '%s': expected %d, got %d" % [
					bench_case.name,
					int(case_result["checksum"]),
					int(case_result["mismatched_checksum"]),
				]
			)
			quit(3)
			return
		results.append(case_result)

	var payload := {
		"schema_version": 1,
		"engine_version": Engine.get_version_info(),
		"os_name": OS.get_name(),
		"processor_name": OS.get_processor_name(),
		"processor_count": OS.get_processor_count(),
		"debug_build": OS.is_debug_build(),
		"warmup": warmup_count,
		"samples": sample_count,
		"scale": scale,
		"benchmarks": results,
	}

	var json_text := JSON.stringify(payload, "  ")
	var json_out: String = str(config["json_out"])
	if not json_out.is_empty():
		var file := FileAccess.open(json_out, FileAccess.WRITE)
		if file == null:
			printerr("Failed to open output path for writing: %s" % json_out)
			quit(4)
			return
		file.store_string(json_text + "\n")
		file.close()
	else:
		print(RESULT_BEGIN_MARKER)
		print(json_text)
		print(RESULT_END_MARKER)

	quit(0)


func _collect_cases(scale: float) -> Array:
	var cases: Array = []
	cases.append_array(SuiteArithmeticScript.build_cases(scale))
	cases.append_array(SuiteCallsScript.build_cases(scale))
	cases.append_array(SuiteAccessScript.build_cases(scale))
	cases.append_array(SuiteObjectsScript.build_cases(scale))
	cases.append_array(SuiteMacroScript.build_cases(scale))
	return cases


func _run_case(bench_case: Variant, warmup_count: int, sample_count: int) -> Dictionary:
	if bench_case.setup_callable.is_valid():
		bench_case.setup_callable.call()

	var expected_checksum: int = 0
	var has_checksum: bool = false
	var checksum_consistent: bool = true
	var mismatched_checksum: int = 0

	for warmup_index: int in range(warmup_count):
		if bench_case.before_sample_callable.is_valid():
			bench_case.before_sample_callable.call()
		var warmup_checksum: int = int(bench_case.run_callable.call())
		if not has_checksum:
			expected_checksum = warmup_checksum
			has_checksum = true
		elif warmup_checksum != expected_checksum:
			checksum_consistent = false
			mismatched_checksum = warmup_checksum

	var sample_usec: Array[int] = []
	sample_usec.resize(sample_count)
	for sample_index: int in range(sample_count):
		if bench_case.before_sample_callable.is_valid():
			bench_case.before_sample_callable.call()
		var start_usec: int = Time.get_ticks_usec()
		var sample_checksum: int = int(bench_case.run_callable.call())
		var elapsed_usec: int = Time.get_ticks_usec() - start_usec
		sample_usec[sample_index] = elapsed_usec
		if not has_checksum:
			expected_checksum = sample_checksum
			has_checksum = true
		elif sample_checksum != expected_checksum:
			checksum_consistent = false
			mismatched_checksum = sample_checksum

	if bench_case.teardown_callable.is_valid():
		bench_case.teardown_callable.call()

	return {
		"name": bench_case.name,
		"category": bench_case.category,
		"description": bench_case.description,
		"operations": bench_case.operations_per_sample,
		"checksum": expected_checksum,
		"checksum_consistent": checksum_consistent,
		"mismatched_checksum": mismatched_checksum,
		"samples_usec": sample_usec,
	}


func _parse_args(args: PackedStringArray) -> Dictionary:
	var config := {
		"filter": "",
		"warmup": 2,
		"samples": 7,
		"scale": 1.0,
		"json_out": "",
		"list_only": false,
		"error": "",
	}
	var index: int = 0
	while index < args.size():
		var arg: String = args[index]
		if arg == "--list":
			config["list_only"] = true
			index += 1
		elif arg == "--filter" and index + 1 < args.size():
			config["filter"] = args[index + 1]
			index += 2
		elif arg == "--warmup" and index + 1 < args.size():
			config["warmup"] = maxi(0, int(args[index + 1]))
			index += 2
		elif arg == "--samples" and index + 1 < args.size():
			config["samples"] = maxi(1, int(args[index + 1]))
			index += 2
		elif arg == "--scale" and index + 1 < args.size():
			config["scale"] = maxf(0.05, float(args[index + 1]))
			index += 2
		elif arg == "--json-out" and index + 1 < args.size():
			config["json_out"] = args[index + 1]
			index += 2
		else:
			config["error"] = "Unknown or incomplete argument: %s" % arg
			return config
	return config
