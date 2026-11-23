/**************************************************************************/
/*  test_gdscript_bytecode_abi.h                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "../gdscript.h"
#include "../gdscript_bytecode_abi.gen.h"
#include "../gdscript_bytecode_format.h"
#include "../gdscript_export_compile_session.h"
#include "../gdscript_function.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/object/method_bind.h"
#include "core/string/string_builder.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

class GDScriptBytecodeAbiTestAccessor {
public:
	static const Vector<int> &get_code(const GDScriptFunction *p_function) { return p_function->code; }
	static int get_stack_size(const GDScriptFunction *p_function) { return p_function->_stack_size; }
	static int get_instruction_args_size(const GDScriptFunction *p_function) { return p_function->_instruction_args_size; }
	static const Vector<Variant::ValidatedOperatorEvaluator> &get_operator_funcs(const GDScriptFunction *p_function) { return p_function->operator_funcs; }
	static const Vector<Variant::ValidatedSetter> &get_setters(const GDScriptFunction *p_function) { return p_function->setters; }
	static const Vector<Variant::ValidatedGetter> &get_getters(const GDScriptFunction *p_function) { return p_function->getters; }
	static const Vector<Variant::ValidatedKeyedSetter> &get_keyed_setters(const GDScriptFunction *p_function) { return p_function->keyed_setters; }
	static const Vector<Variant::ValidatedKeyedGetter> &get_keyed_getters(const GDScriptFunction *p_function) { return p_function->keyed_getters; }
	static const Vector<Variant::ValidatedIndexedSetter> &get_indexed_setters(const GDScriptFunction *p_function) { return p_function->indexed_setters; }
	static const Vector<Variant::ValidatedIndexedGetter> &get_indexed_getters(const GDScriptFunction *p_function) { return p_function->indexed_getters; }
	static const Vector<Variant::ValidatedBuiltInMethod> &get_builtin_methods(const GDScriptFunction *p_function) { return p_function->builtin_methods; }
	static const Vector<Variant::ValidatedConstructor> &get_constructors(const GDScriptFunction *p_function) { return p_function->constructors; }
	static const Vector<Variant::ValidatedUtilityFunction> &get_utilities(const GDScriptFunction *p_function) { return p_function->utilities; }
	static const Vector<GDScriptUtilityFunctions::FunctionPtr> &get_gds_utilities(const GDScriptFunction *p_function) { return p_function->gds_utilities; }
	static const Vector<MethodBind *> &get_methods(const GDScriptFunction *p_function) { return p_function->methods; }
};

namespace TestGDScriptBytecodeABI {

static Ref<GDScript> compile_inline_script(const String &p_source) {
	GDScriptLanguage::get_singleton()->init();
	Ref<GDScript> script = memnew(GDScript);
	script->set_source_code(p_source);
	const Error reload_error = script->reload();
	REQUIRE_MESSAGE(reload_error == OK, "Test GDScript should compile cleanly.");
	return script;
}

static void verify_bytecode_layout_walk(const GDScriptFunction *p_function) {
	const Vector<int> &code = GDScriptBytecodeAbiTestAccessor::get_code(p_function);
	const int instruction_args_limit = GDScriptBytecodeAbiTestAccessor::get_instruction_args_size(p_function);
	REQUIRE(code.size() >= 1);

	int instruction_pointer = 0;
	bool saw_terminal_end = false;
	while (instruction_pointer < code.size()) {
		const int raw_opcode = code[instruction_pointer];
		REQUIRE(raw_opcode >= 0);
		REQUIRE(raw_opcode <= GDScriptFunction::OPCODE_END);

		const GDScriptFunction::Opcode opcode = static_cast<GDScriptFunction::Opcode>(raw_opcode);
		const GDScriptBytecodeFormat::OpcodeLayout &layout = GDScriptFunction::get_opcode_layout(opcode);

		if (opcode == GDScriptFunction::OPCODE_END) {
			CHECK(instruction_pointer + 1 == code.size());
			saw_terminal_end = true;
			instruction_pointer += 1;
			break;
		}

		if (!layout.is_vararg) {
			REQUIRE(instruction_pointer + layout.word_count <= code.size());
			instruction_pointer += layout.word_count;
		} else {
			REQUIRE(instruction_pointer + 2 <= code.size());
			const int instruction_arg_count = code[instruction_pointer + 1];
			CHECK(instruction_arg_count >= 0);
			CHECK(instruction_arg_count <= instruction_args_limit);

			const int tail_offset = instruction_pointer + 2 + instruction_arg_count;
			REQUIRE(tail_offset + layout.word_count <= code.size());

			const int count_value = code[tail_offset + layout.count_tail_offset];
			CHECK(count_value >= 0);
			CHECK(instruction_arg_count == count_value * layout.count_multiplier + layout.count_addend);

			instruction_pointer = tail_offset + layout.word_count;
		}
	}

	CHECK(saw_terminal_end);
	CHECK(instruction_pointer == code.size());
}

TEST_SUITE("[Modules][GDScript][BytecodeABI]") {
	TEST_CASE("Semantic ABI digest is non-zero and constructor counts match runtime Variant registration") {
		static_assert(sizeof(GDSCRIPT_BYTECODE_ABI_ID) == 16, "ABI digest must be a 16-byte SHA-256 prefix.");

		bool has_nonzero_byte = false;
		for (uint8_t digest_byte : GDSCRIPT_BYTECODE_ABI_ID) {
			if (digest_byte != 0) {
				has_nonzero_byte = true;
				break;
			}
		}
		CHECK_MESSAGE(has_nonzero_byte, "GDSCRIPT_BYTECODE_ABI_ID must not be all zeroes.");

		for (int type_index = 0; type_index < Variant::VARIANT_MAX; type_index++) {
			const Variant::Type variant_type = static_cast<Variant::Type>(type_index);
			const int runtime_count = Variant::get_constructor_count(variant_type);
			REQUIRE(runtime_count >= 0);
			CHECK_MESSAGE(
					GDSCRIPT_BYTECODE_ABI_CONSTRUCTOR_COUNTS[type_index] == static_cast<uint32_t>(runtime_count),
					vformat("Constructor count mismatch for Variant::%s", Variant::get_type_name(variant_type)));
		}

		CHECK(GDScriptFunction::OPERATOR_INLINE_CACHE_PTR_WORDS == 2);
		CHECK(sizeof(Variant::ValidatedOperatorEvaluator) <= sizeof(int) * GDScriptFunction::OPERATOR_INLINE_CACHE_PTR_WORDS);

		const GDScriptBytecodeFormat::OpcodeLayout &operator_layout = GDScriptFunction::get_opcode_layout(GDScriptFunction::OPCODE_OPERATOR);
		CHECK_FALSE(operator_layout.is_vararg);
		CHECK(operator_layout.word_count == 7 + GDScriptFunction::OPERATOR_INLINE_CACHE_PTR_WORDS);
		CHECK(operator_layout.operands[4] == GDScriptBytecodeFormat::OPERAND_OPERATOR_CACHE_ZERO);
		CHECK(operator_layout.operands[5] == GDScriptBytecodeFormat::OPERAND_OPERATOR_CACHE_ZERO);
		CHECK(operator_layout.operands[6] == GDScriptBytecodeFormat::OPERAND_OPERATOR_CACHE_ZERO);
		CHECK(operator_layout.operands[7] == GDScriptBytecodeFormat::OPERAND_OPERATOR_CACHE_ZERO);

		int vararg_opcode_count = 0;
		for (int opcode_index = 0; opcode_index <= GDScriptFunction::OPCODE_END; opcode_index++) {
			const GDScriptBytecodeFormat::OpcodeLayout &layout = GDScriptFunction::get_opcode_layout(static_cast<GDScriptFunction::Opcode>(opcode_index));
			if (layout.is_vararg) {
				vararg_opcode_count++;
			}
		}
		CHECK(vararg_opcode_count == 24);
	}

#ifdef GDSCRIPT_BYTECODE_EXPORT_ENABLED
	TEST_CASE("ExportFixups parity, symbolic lookup resolution, global stores, and OPCODE_OPERATOR cache") {
		GDScriptLanguage::get_singleton()->init();

		const StringName test_autoload_name("TestBytecodeAbiAutoload");
		const StringName test_named_global_name("TestBytecodeAbiNamedGlobal");
		const String autoload_setting_path = "autoload/" + String(test_autoload_name);
		const String named_global_autoload_setting_path = "autoload/" + String(test_named_global_name);
		const String autoload_script_path = TestUtils::get_temp_path("test_bytecode_abi_autoload.gd");

		{
			Ref<FileAccess> autoload_file = FileAccess::open(autoload_script_path, FileAccess::WRITE);
			REQUIRE(autoload_file.is_valid());
			autoload_file->store_string("extends RefCounted\n");
			autoload_file->close();
		}

		ProjectSettings::get_singleton()->set_setting(autoload_setting_path, "*" + autoload_script_path);
		if (!GDScriptLanguage::get_singleton()->get_global_map().has(test_autoload_name)) {
			GDScriptLanguage::get_singleton()->add_global_constant(test_autoload_name, Variant(777));
		}
#ifdef TOOLS_ENABLED
		ProjectSettings::get_singleton()->set_setting(named_global_autoload_setting_path, "*" + autoload_script_path);
		GDScriptLanguage::get_singleton()->add_named_global_constant(test_named_global_name, Variant(888));
#endif

		struct GlobalRegistrationCleanup {
			String autoload_setting;
			String named_global_autoload_setting;
			String autoload_file_path;
			StringName named_global_name;
			~GlobalRegistrationCleanup() {
				ProjectSettings::get_singleton()->set_setting(autoload_setting, Variant());
#ifdef TOOLS_ENABLED
				ProjectSettings::get_singleton()->set_setting(named_global_autoload_setting, Variant());
				GDScriptLanguage::get_singleton()->remove_named_global_constant(named_global_name);
#endif
				DirAccess::remove_absolute(autoload_file_path);
			}
		} cleanup{ autoload_setting_path, named_global_autoload_setting_path, autoload_script_path, test_named_global_name };

		String source_code = R"(
extends RefCounted

func exercise_all_fixups(untyped_left, untyped_right, runtime_float: float) -> Dictionary:
	# Validated unary & binary operators (plus specialized inline binary op).
	var typed_left: int = 10
	var typed_right: int = 5
	var validated_sum: int = typed_left + typed_right
	var validated_shift: int = typed_left ** 2
	var validated_neg: int = ~typed_left

	# Unvalidated dynamic OPCODE_OPERATOR (binary + unary).
	var dynamic_sum = untyped_left + untyped_right
	var dynamic_neg = -untyped_left

	# Validated setters and getters on builtin member.
	var vector_value: Vector3 = Vector3(3.0, 4.0, 5.0)
	vector_value.x = 6.0
	var vector_x: float = vector_value.x

	# Validated indexed setter and getter.
	var packed_ints: PackedInt32Array = PackedInt32Array([1, 2, 3])
	packed_ints[1] = 42
	var indexed_value: int = packed_ints[1]

	# Validated keyed setter and getter.
	var dictionary_value: Dictionary = {"alpha": 1}
	dictionary_value["beta"] = 99
	var keyed_value = dictionary_value["beta"]

	# Validated builtin method.
	var text_value: String = "godot"
	var upper_text: String = text_value.to_upper()

	# Validated utility and GDScript utility (non-constant inputs to avoid constant folding).
	var clamped_value: float = sin(runtime_float)
	var text_length: int = len(upper_text)

	# Validated and non-validated MethodBinds (instance and native static).
	var helper_object: RefCounted = RefCounted.new()
	var object_class: String = helper_object.get_class()
	var missing_file: bool = FileAccess.file_exists("res://nonexistent_bytecode_abi_test_path.tmp")

	# Global store via autoload singleton.
	var autoload_value = TestBytecodeAbiAutoload
)";
#ifdef TOOLS_ENABLED
		source_code += "\tvar named_global_value = TestBytecodeAbiNamedGlobal\n";
#else
		source_code += "\tvar named_global_value = 0\n";
#endif
		source_code += R"(
	return {
		"validated_sum": validated_sum,
		"validated_shift": validated_shift,
		"validated_neg": validated_neg,
		"dynamic_sum": dynamic_sum,
		"dynamic_neg": dynamic_neg,
		"vector_x": vector_x,
		"indexed_value": indexed_value,
		"keyed_value": keyed_value,
		"upper_text": upper_text,
		"clamped_value": clamped_value,
		"text_length": text_length,
		"object_class": object_class,
		"missing_file": missing_file,
		"autoload_value": autoload_value,
		"named_global_value": named_global_value,
	}
)";

		Ref<GDScript> script = compile_inline_script(source_code);
		REQUIRE(script->is_valid());
		const HashMap<StringName, GDScriptFunction *> &member_functions = script->get_member_functions();
		REQUIRE(member_functions.has(StringName("exercise_all_fixups")));
		GDScriptFunction *function = member_functions[StringName("exercise_all_fixups")];
		REQUIRE(function != nullptr);

		verify_bytecode_layout_walk(function);

		const GDScriptFunction::ExportFixups &fixups = function->get_export_fixups();

		// Verify 1:1 size parity across all 12 fixup tables and that each table was exercised.
		const auto &operator_funcs = GDScriptBytecodeAbiTestAccessor::get_operator_funcs(function);
		const auto &setters = GDScriptBytecodeAbiTestAccessor::get_setters(function);
		const auto &getters = GDScriptBytecodeAbiTestAccessor::get_getters(function);
		const auto &keyed_setters = GDScriptBytecodeAbiTestAccessor::get_keyed_setters(function);
		const auto &keyed_getters = GDScriptBytecodeAbiTestAccessor::get_keyed_getters(function);
		const auto &indexed_setters = GDScriptBytecodeAbiTestAccessor::get_indexed_setters(function);
		const auto &indexed_getters = GDScriptBytecodeAbiTestAccessor::get_indexed_getters(function);
		const auto &builtin_methods = GDScriptBytecodeAbiTestAccessor::get_builtin_methods(function);
		const auto &constructors = GDScriptBytecodeAbiTestAccessor::get_constructors(function);
		const auto &utilities = GDScriptBytecodeAbiTestAccessor::get_utilities(function);
		const auto &gds_utilities = GDScriptBytecodeAbiTestAccessor::get_gds_utilities(function);
		const auto &methods = GDScriptBytecodeAbiTestAccessor::get_methods(function);

		CHECK(fixups.operators.size() == operator_funcs.size());
		CHECK(fixups.operators.size() >= 2);
		for (int index = 0; index < fixups.operators.size(); index++) {
			const auto &entry = fixups.operators[index];
			CHECK(Variant::get_validated_operator_evaluator(entry.operation, entry.left_type, entry.right_type) == operator_funcs[index]);
		}

		CHECK(fixups.setters.size() == setters.size());
		CHECK(fixups.setters.size() >= 1);
		for (int index = 0; index < fixups.setters.size(); index++) {
			const auto &entry = fixups.setters[index];
			CHECK(Variant::get_member_validated_setter(entry.builtin_type, entry.member_name) == setters[index]);
		}

		CHECK(fixups.getters.size() == getters.size());
		CHECK(fixups.getters.size() >= 1);
		for (int index = 0; index < fixups.getters.size(); index++) {
			const auto &entry = fixups.getters[index];
			CHECK(Variant::get_member_validated_getter(entry.builtin_type, entry.member_name) == getters[index]);
		}

		CHECK(fixups.keyed_setters.size() == keyed_setters.size());
		CHECK(fixups.keyed_setters.size() >= 1);
		for (int index = 0; index < fixups.keyed_setters.size(); index++) {
			CHECK(Variant::get_member_validated_keyed_setter(fixups.keyed_setters[index]) == keyed_setters[index]);
		}

		CHECK(fixups.keyed_getters.size() == keyed_getters.size());
		CHECK(fixups.keyed_getters.size() >= 1);
		for (int index = 0; index < fixups.keyed_getters.size(); index++) {
			CHECK(Variant::get_member_validated_keyed_getter(fixups.keyed_getters[index]) == keyed_getters[index]);
		}

		CHECK(fixups.indexed_setters.size() == indexed_setters.size());
		CHECK(fixups.indexed_setters.size() >= 1);
		for (int index = 0; index < fixups.indexed_setters.size(); index++) {
			CHECK(Variant::get_member_validated_indexed_setter(fixups.indexed_setters[index]) == indexed_setters[index]);
		}

		CHECK(fixups.indexed_getters.size() == indexed_getters.size());
		CHECK(fixups.indexed_getters.size() >= 1);
		for (int index = 0; index < fixups.indexed_getters.size(); index++) {
			CHECK(Variant::get_member_validated_indexed_getter(fixups.indexed_getters[index]) == indexed_getters[index]);
		}

		CHECK(fixups.builtin_methods.size() == builtin_methods.size());
		CHECK(fixups.builtin_methods.size() >= 1);
		for (int index = 0; index < fixups.builtin_methods.size(); index++) {
			const auto &entry = fixups.builtin_methods[index];
			CHECK(Variant::get_validated_builtin_method(entry.builtin_type, entry.method_name) == builtin_methods[index]);
		}

		CHECK(fixups.constructors.size() == constructors.size());
		CHECK(fixups.constructors.size() >= 1);
		for (int index = 0; index < fixups.constructors.size(); index++) {
			const auto &entry = fixups.constructors[index];
			CHECK(Variant::get_validated_constructor(entry.builtin_type, entry.constructor_index) == constructors[index]);
		}

		CHECK(fixups.utilities.size() == utilities.size());
		CHECK(fixups.utilities.size() >= 1);
		for (int index = 0; index < fixups.utilities.size(); index++) {
			CHECK(Variant::get_validated_utility_function(fixups.utilities[index]) == utilities[index]);
		}

		CHECK(fixups.gds_utilities.size() == gds_utilities.size());
		CHECK(fixups.gds_utilities.size() >= 1);
		for (int index = 0; index < fixups.gds_utilities.size(); index++) {
			CHECK(GDScriptUtilityFunctions::get_function(fixups.gds_utilities[index]) == gds_utilities[index]);
		}

		CHECK(fixups.method_binds.size() == methods.size());
		CHECK(fixups.method_binds.size() >= 2);
		for (int index = 0; index < fixups.method_binds.size(); index++) {
			const auto &entry = fixups.method_binds[index];
			CHECK(ClassDB::get_method(entry.class_name, entry.method_name) == methods[index]);
		}

		// Verify global_stores metadata points to the global-index operand of OPCODE_STORE_GLOBAL.
		const Vector<int> &code = GDScriptBytecodeAbiTestAccessor::get_code(function);
		REQUIRE(fixups.global_stores.size() == 1);
		const auto &global_store = fixups.global_stores[0];
		CHECK(global_store.global_name == test_autoload_name);
		REQUIRE(global_store.code_offset >= 2);
		REQUIRE(global_store.code_offset < code.size());
		CHECK(code[global_store.code_offset - 2] == GDScriptFunction::OPCODE_STORE_GLOBAL);
		CHECK(code[global_store.code_offset] == GDScriptLanguage::get_singleton()->get_global_map()[test_autoload_name]);

#ifdef TOOLS_ENABLED
		REQUIRE(fixups.named_globals.size() == 1);
		CHECK(fixups.named_globals[0] == test_named_global_name);
#endif

		// Verify OPCODE_OPERATOR inline cache offsets point to zeroed 4-word inline caches before first execution.
		REQUIRE(fixups.operator_cache_offsets.size() >= 2);
		for (int offset : fixups.operator_cache_offsets) {
			REQUIRE(offset + 7 + GDScriptFunction::OPERATOR_INLINE_CACHE_PTR_WORDS <= code.size());
			CHECK(code[offset] == GDScriptFunction::OPCODE_OPERATOR);
			for (int cache_word = 5; cache_word < 7 + GDScriptFunction::OPERATOR_INLINE_CACHE_PTR_WORDS; cache_word++) {
				CHECK(code[offset + cache_word] == 0);
			}
		}

		// Execute twice to verify both cold-path inline cache initialization and warm-path memcpy evaluator dispatch.
		Ref<RefCounted> instance = memnew(RefCounted);
		instance->set_script(script);

		for (int call_iteration = 0; call_iteration < 2; call_iteration++) {
			Variant result_variant = instance->call(StringName("exercise_all_fixups"), 20, 22, 1.5);
			REQUIRE(result_variant.get_type() == Variant::DICTIONARY);
			Dictionary result = result_variant;
			CHECK(int(result["validated_sum"]) == 15);
			CHECK(int(result["validated_neg"]) == -11);
			CHECK(int(result["dynamic_sum"]) == 42);
			CHECK(int(result["dynamic_neg"]) == -20);
			CHECK(double(result["vector_x"]) == doctest::Approx(6.0));
			CHECK(int(result["indexed_value"]) == 42);
			CHECK(int(result["keyed_value"]) == 99);
			CHECK(String(result["upper_text"]) == "GODOT");
			CHECK(double(result["clamped_value"]) == doctest::Approx(Math::sin(1.5)));
			CHECK(int(result["text_length"]) == 5);
			CHECK(String(result["object_class"]) == "RefCounted");
			CHECK(bool(result["missing_file"]) == false);
			CHECK(int(result["autoload_value"]) == 777);
#ifdef TOOLS_ENABLED
			CHECK(int(result["named_global_value"]) == 888);
#endif
		}

		// Execute with different dynamic types to verify the cache-mismatch fallback path too.
		Variant float_result_variant = instance->call(StringName("exercise_all_fixups"), 1.5, 2.5, 0.25);
		REQUIRE(float_result_variant.get_type() == Variant::DICTIONARY);
		Dictionary float_result = float_result_variant;
		CHECK(double(float_result["dynamic_sum"]) == doctest::Approx(4.0));
		CHECK(double(float_result["dynamic_neg"]) == doctest::Approx(-1.5));
		CHECK(double(float_result["clamped_value"]) == doctest::Approx(Math::sin(0.25)));

		// Verify detached export compilation records placeholder index 0 with the exact autoload name in global_stores.
		const String detached_script_path = TestUtils::get_temp_path("test_bytecode_abi_detached_autoload.gd");
		{
			Ref<FileAccess> detached_file = FileAccess::open(detached_script_path, FileAccess::WRITE);
			REQUIRE(detached_file.is_valid());
			detached_file->store_string("extends RefCounted\nfunc read_autoload():\n\treturn TestBytecodeAbiAutoload\n");
			detached_file->close();
		}
		struct DetachedFileCleanup {
			String file_path;
			~DetachedFileCleanup() { DirAccess::remove_absolute(file_path); }
		} detached_file_cleanup{ detached_script_path };

		GDScriptExportCompileSession export_session(true);
		Error export_compile_error = OK;
		Ref<GDScript> detached_script = export_session.get_or_compile(detached_script_path, export_compile_error);
		REQUIRE(export_compile_error == OK);
		REQUIRE(detached_script.is_valid());
		REQUIRE(detached_script->get_member_functions().has(StringName("read_autoload")));
		GDScriptFunction *detached_function = detached_script->get_member_functions()[StringName("read_autoload")];
		REQUIRE(detached_function != nullptr);
		const GDScriptFunction::ExportFixups &detached_fixups = detached_function->get_export_fixups();
		const Vector<int> &detached_code = GDScriptBytecodeAbiTestAccessor::get_code(detached_function);
		REQUIRE(detached_fixups.global_stores.size() == 1);
		CHECK(detached_fixups.global_stores[0].global_name == test_autoload_name);
		REQUIRE(detached_fixups.global_stores[0].code_offset >= 2);
		REQUIRE(detached_fixups.global_stores[0].code_offset < detached_code.size());
		CHECK(detached_code[detached_fixups.global_stores[0].code_offset - 2] == GDScriptFunction::OPCODE_STORE_GLOBAL);
		CHECK(detached_code[detached_fixups.global_stores[0].code_offset] == 0);
	}
#endif // GDSCRIPT_BYTECODE_EXPORT_ENABLED

	TEST_CASE("Recursive large and medium VM frames spill cleanly to heap without stack overflow") {
		auto build_recursive_script = [](int p_local_count) -> String {
			StringBuilder builder;
			builder.append("extends RefCounted\n\n");
			builder.append("func recurse_sum(depth: int) -> int:\n");
			for (int local_index = 0; local_index < p_local_count; local_index++) {
				builder.append(vformat("\tvar local_%d: int = depth + %d\n", local_index, local_index));
			}
			builder.append("\tif depth <= 0:\n");
			builder.append(vformat("\t\treturn local_0 + local_%d\n", p_local_count - 1));
			builder.append(vformat("\treturn (local_%d - local_0) + recurse_sum(depth - 1)\n", p_local_count - 1));
			return builder.as_string();
		};

		// Unoptimized (-O0) builds allocate a much larger C++ stack frame for GDScriptFunction::call()
		// itself (~180 KiB/frame), whereas optimized builds reach MAX_CALL_DEPTH directly on an 8 MiB stack.
#if defined(__OPTIMIZE__)
		constexpr int large_frame_depth = GDScriptFunction::MAX_CALL_DEPTH - 20;
		constexpr int medium_frame_depth = GDScriptFunction::MAX_CALL_DEPTH - 20;
#else
		constexpr int large_frame_depth = 34;
		constexpr int medium_frame_depth = 20;
#endif

		// Case 1: Large single frame (> VM_SINGLE_FRAME_ALLOCA_LIMIT = 4096 bytes) spilling every frame to heap.
		// With 3500 locals (~84 KiB/frame), even 35 recursive frames would consume ~2.95 MiB of extra alloca on top
		// of the C++ call frames (and ~170 MiB at MAX_CALL_DEPTH in optimized builds).
		{
			constexpr int large_local_count = 3500;
			Ref<GDScript> large_script = compile_inline_script(build_recursive_script(large_local_count));
			GDScriptFunction *function = large_script->get_member_functions()[StringName("recurse_sum")];
			REQUIRE(function != nullptr);
			CHECK(GDScriptBytecodeAbiTestAccessor::get_stack_size(function) >= large_local_count + GDScriptFunction::FIXED_ADDRESSES_MAX);

			Ref<RefCounted> instance = memnew(RefCounted);
			instance->set_script(large_script);

			GDScriptFunction::debug_reset_vm_alloca_stats();
			CHECK(GDScriptFunction::debug_get_vm_alloca_stats().bytes_in_use == 0);

			Variant sum_result = instance->call(StringName("recurse_sum"), large_frame_depth);
			// Each recursive step adds (large_local_count - 1), base case returns 0 + (large_local_count - 1).
			const int64_t expected_sum = int64_t(large_frame_depth + 1) * int64_t(large_local_count - 1);
			CHECK(int64_t(sum_result) == expected_sum);

			const GDScriptFunction::DebugVMAllocaStats stats = GDScriptFunction::debug_get_vm_alloca_stats();
			CHECK(stats.bytes_in_use == 0);
			CHECK(stats.peak_bytes == 0);
			CHECK(stats.heap_spill_count == static_cast<uint32_t>(large_frame_depth + 1));

			// Invalid argument type on large heap-spilled frame must clean up without leaking memory or alloca budget.
			Callable::CallError call_error;
			Variant invalid_arg = "not_an_int";
			const Variant *arg_ptrs[1] = { &invalid_arg };
			instance->callp(StringName("recurse_sum"), arg_ptrs, 1, call_error);
			CHECK(call_error.error == Callable::CallError::CALL_ERROR_INVALID_ARGUMENT);
			CHECK(GDScriptFunction::debug_get_vm_alloca_stats().bytes_in_use == 0);
		}

		// Case 2: Medium frame (<= 4096 bytes) staying on alloca fast path initially, then crossing
		// VM_TOTAL_ALLOCA_BUDGET (256 KiB) and spilling all deeper frames to heap cleanly.
		{
			constexpr int medium_local_count = 120;
			Ref<GDScript> medium_script = compile_inline_script(build_recursive_script(medium_local_count));
			GDScriptFunction *function = medium_script->get_member_functions()[StringName("recurse_sum")];
			REQUIRE(function != nullptr);
			const size_t estimated_frame_bytes = sizeof(Variant) * GDScriptBytecodeAbiTestAccessor::get_stack_size(function);
			CHECK(estimated_frame_bytes < 4096);

			Ref<RefCounted> instance = memnew(RefCounted);
			instance->set_script(medium_script);

#if !defined(__OPTIMIZE__)
			// Pre-seed the cumulative alloca counter near the 256 KiB ceiling so unoptimized builds
			// deterministically exercise the exact fast-path -> heap-spill transition within 20 frames.
			constexpr uint32_t preseeded_alloca_bytes = (256 * 1024) - (3 * 4096);
			GDScriptFunction::debug_set_vm_alloca_bytes_in_use_for_test(preseeded_alloca_bytes);
			struct AllocaResetGuard {
				~AllocaResetGuard() { GDScriptFunction::debug_set_vm_alloca_bytes_in_use_for_test(0); }
			} alloca_reset_guard;
#else
			constexpr uint32_t preseeded_alloca_bytes = 0;
#endif

			GDScriptFunction::debug_reset_vm_alloca_stats();
			Variant sum_result = instance->call(StringName("recurse_sum"), medium_frame_depth);
			const int64_t expected_sum = int64_t(medium_frame_depth + 1) * int64_t(medium_local_count - 1);
			CHECK(int64_t(sum_result) == expected_sum);

			const GDScriptFunction::DebugVMAllocaStats stats = GDScriptFunction::debug_get_vm_alloca_stats();
			CHECK(stats.bytes_in_use == preseeded_alloca_bytes);
			CHECK(stats.peak_bytes > preseeded_alloca_bytes);
			CHECK(stats.peak_bytes <= 256 * 1024);
			CHECK(stats.heap_spill_count > 0);
			CHECK(stats.heap_spill_count < static_cast<uint32_t>(medium_frame_depth + 1));
		}

#if defined(__OPTIMIZE__)
		// Case 3 (optimized builds): Exceed MAX_CALL_DEPTH with a large frame to confirm clean recursion guard.
		{
			Ref<GDScript> large_script = compile_inline_script(build_recursive_script(500));
			Ref<RefCounted> instance = memnew(RefCounted);
			instance->set_script(large_script);

			ERR_PRINT_OFF;
			instance->call(StringName("recurse_sum"), GDScriptFunction::MAX_CALL_DEPTH + 10);
			ERR_PRINT_ON;
			CHECK(GDScriptFunction::debug_get_vm_alloca_stats().bytes_in_use == 0);
		}
#endif
	}
}

} // namespace TestGDScriptBytecodeABI
