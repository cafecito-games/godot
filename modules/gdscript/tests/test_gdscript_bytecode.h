/**************************************************************************/
/*  test_gdscript_bytecode.h                                              */
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
#include "../gdscript_bytecode_export.h"
#include "../gdscript_bytecode_format.h"
#include "../gdscript_bytecode_loader.h"
#include "../gdscript_bytecode_verifier.h"
#include "../gdscript_export_compile_session.h"
#include "../gdscript_function.h"
#include "../gdscript_utility_callable.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/marshalls.h"
#include "core/io/resource.h"
#include "core/object/script_language_extension.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/version.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

#ifdef GDSCRIPT_BYTECODE_EXPORT_ENABLED

class GDScriptBytecodeTestAccessor {
public:
	static Vector<int> &mutable_code(GDScriptFunction *p_function) { return p_function->code; }
	static int &mutable_code_size(GDScriptFunction *p_function) { return p_function->_code_size; }
	static int *&mutable_code_ptr(GDScriptFunction *p_function) { return p_function->_code_ptr; }
	static int &mutable_stack_size(GDScriptFunction *p_function) { return p_function->_stack_size; }
	static int &mutable_instruction_args_size(GDScriptFunction *p_function) { return p_function->_instruction_args_size; }
	static int &mutable_argument_count(GDScriptFunction *p_function) { return p_function->_argument_count; }
	static int &mutable_vararg_index(GDScriptFunction *p_function) { return p_function->_vararg_index; }
	static int &mutable_default_arg_count(GDScriptFunction *p_function) { return p_function->_default_arg_count; }
	static Vector<int> &mutable_default_arguments(GDScriptFunction *p_function) { return p_function->default_arguments; }
	static const int *&mutable_default_arg_ptr(GDScriptFunction *p_function) { return p_function->_default_arg_ptr; }
	static MethodInfo &mutable_method_info(GDScriptFunction *p_function) { return p_function->method_info; }
	static TightLocalVector<Pair<int, Variant::Type>> &mutable_temporary_slots(GDScriptFunction *p_function) { return p_function->temporary_slots; }
	static Vector<Variant> &mutable_constants(GDScriptFunction *p_function) { return p_function->constants; }
	static int &mutable_constant_count(GDScriptFunction *p_function) { return p_function->_constant_count; }
	static Variant *&mutable_constants_ptr(GDScriptFunction *p_function) { return p_function->_constants_ptr; }
	static Vector<Variant::ValidatedOperatorEvaluator> &mutable_operator_funcs(GDScriptFunction *p_function) { return p_function->operator_funcs; }
	static int &mutable_operator_funcs_count(GDScriptFunction *p_function) { return p_function->_operator_funcs_count; }
	static const Variant::ValidatedOperatorEvaluator *&mutable_operator_funcs_ptr(GDScriptFunction *p_function) { return p_function->_operator_funcs_ptr; }
	static Vector<GDScriptFunction *> &mutable_lambdas(GDScriptFunction *p_function) { return p_function->lambdas; }
	static const List<GDScriptFunction::StackDebug> &get_stack_debug(const GDScriptFunction *p_function) { return p_function->stack_debug; }
	static HashMap<StringName, Variant> &mutable_script_constants(GDScript *p_script) { return p_script->constants; }
	static HashMap<StringName, GDScript::MemberInfo> &mutable_member_indices(GDScript *p_script) { return p_script->member_indices; }
	static HashSet<StringName> &mutable_members(GDScript *p_script) { return p_script->members; }
	static HashMap<StringName, GDScript::MemberInfo> &mutable_static_indices(GDScript *p_script) { return p_script->static_variables_indices; }
	static HashMap<StringName, MethodInfo> &mutable_signals(GDScript *p_script) { return p_script->_signals; }
	static HashMap<StringName, GDScriptFunction *> &mutable_member_functions(GDScript *p_script) { return p_script->member_functions; }
	static HashMap<StringName, Ref<GDScript>> &mutable_subclasses(GDScript *p_script) { return p_script->subclasses; }
	static Dictionary &mutable_rpc_config(GDScript *p_script) { return p_script->rpc_config; }
	static Ref<GDScript> &mutable_base(GDScript *p_script) { return p_script->base; }
	static GDScript *get_owner(const GDScript *p_script) { return p_script->_owner; }
	static const String &get_path_field(const GDScript *p_script) { return p_script->path; }
	static bool get_path_valid(const GDScript *p_script) { return p_script->path_valid; }
	static GDScriptFunction *get_initializer(const GDScript *p_script) { return p_script->initializer; }

#ifdef DEBUG_ENABLED
	static const Vector<String> &get_operator_names(const GDScriptFunction *p_function) { return p_function->operator_names; }
	static const Vector<String> &get_setter_names(const GDScriptFunction *p_function) { return p_function->setter_names; }
	static const Vector<String> &get_getter_names(const GDScriptFunction *p_function) { return p_function->getter_names; }
	static const Vector<String> &get_builtin_methods_names(const GDScriptFunction *p_function) { return p_function->builtin_methods_names; }
	static const Vector<String> &get_constructors_names(const GDScriptFunction *p_function) { return p_function->constructors_names; }
	static const Vector<String> &get_utilities_names(const GDScriptFunction *p_function) { return p_function->utilities_names; }
	static const Vector<String> &get_gds_utilities_names(const GDScriptFunction *p_function) { return p_function->gds_utilities_names; }
	static const char *get_func_cname(const GDScriptFunction *p_function) { return p_function->_func_cname; }
	static StringName get_profile_signature(const GDScriptFunction *p_function) { return p_function->profile.signature; }
#endif
};

namespace TestGDScriptBytecode {

class StubExternalResolver : public GDScriptBytecodeExternalResolver {
public:
	HashMap<String, Ref<Resource>> resources;
	HashMap<String, Ref<GDScript>> scripts;
	int resolve_resource_calls = 0;
	int resolve_script_calls = 0;

	Ref<Resource> resolve_resource(const String &p_path, const String &p_type_hint, Error &r_error) override {
		resolve_resource_calls++;
		HashMap<String, Ref<Resource>>::ConstIterator entry = resources.find(p_path);
		if (!entry || entry->value.is_null()) {
			r_error = ERR_FILE_NOT_FOUND;
			return Ref<Resource>();
		}
		r_error = OK;
		return entry->value;
	}

	Ref<GDScript> resolve_script(const String &p_path, const String &p_fully_qualified_name, bool &r_is_local_class, Error &r_error) override {
		resolve_script_calls++;
		r_is_local_class = false;
		HashMap<String, Ref<GDScript>>::ConstIterator entry = scripts.find(p_path);
		if (!entry || entry->value.is_null()) {
			r_error = ERR_FILE_NOT_FOUND;
			return Ref<GDScript>();
		}
		Ref<GDScript> found = entry->value;
		if (!p_fully_qualified_name.is_empty()) {
			GDScript *inner = found->find_class(p_fully_qualified_name);
			if (inner != nullptr) {
				found = Ref<GDScript>(inner);
			}
		}
		r_error = OK;
		return found;
	}
};

class RawBufferBuilder {
	Vector<uint8_t> bytes;

public:
	void u8(uint8_t p_value) { bytes.push_back(p_value); }
	void u32(uint32_t p_value) {
		const int old_size = bytes.size();
		bytes.resize(old_size + 4);
		encode_uint32(p_value, bytes.ptrw() + old_size);
	}
	void i32(int32_t p_value) { u32(static_cast<uint32_t>(p_value)); }
	void raw(const uint8_t *p_data, int p_length) {
		if (p_length <= 0) {
			return;
		}
		const int old_size = bytes.size();
		bytes.resize(old_size + p_length);
		memcpy(bytes.ptrw() + old_size, p_data, static_cast<size_t>(p_length));
	}
	void utf8(const String &p_text) {
		const CharString utf8_bytes = p_text.utf8();
		u32(static_cast<uint32_t>(utf8_bytes.length()));
		raw(reinterpret_cast<const uint8_t *>(utf8_bytes.get_data()), utf8_bytes.length());
	}
	void write_valid_header(uint32_t p_script_flags = 0) {
		raw(GDScriptBytecodeFormat::MAGIC, 4);
		u32(GDScriptBytecodeFormat::FORMAT_VERSION);
		raw(GDSCRIPT_BYTECODE_ABI_ID, 16);
		u8(static_cast<uint8_t>(sizeof(real_t)));
		u32(static_cast<uint32_t>(GDScriptFunction::OPCODE_END));
		utf8(String(GODOT_VERSION_FULL_CONFIG));
		utf8(String(GODOT_VERSION_HASH));
		u32(p_script_flags);
	}
	const Vector<uint8_t> &get_bytes() const { return bytes; }
};

static Ref<GDScript> compile_source(const String &p_source, const String &p_path = "res://test_bytecode_inline.gd") {
	GDScriptLanguage::get_singleton()->init();
	Ref<GDScript> script = memnew(GDScript);
	script->set_path_cache(p_path);
	script->set_source_code(p_source);
	const Error reload_error = script->reload();
	REQUIRE_MESSAGE(reload_error == OK, "Source script must compile cleanly.");
	REQUIRE(script->is_valid());
	return script;
}

static Vector<uint8_t> export_script_bytes(const Ref<GDScript> &p_script, const GDScriptBytecodeExporter::Options &p_options = GDScriptBytecodeExporter::Options()) {
	Vector<uint8_t> bytes;
	String error_message;
	const Error export_error = GDScriptBytecodeExporter::serialize(p_script, bytes, p_options, &error_message);
	REQUIRE_MESSAGE(export_error == OK, error_message);
	REQUIRE(!bytes.is_empty());
	return bytes;
}

static Ref<GDScript> load_script_bytes(const Vector<uint8_t> &p_bytes, const String &p_path = "res://test_bytecode_loaded.gd", GDScriptBytecodeExternalResolver *p_resolver = nullptr) {
	Ref<GDScript> loaded = memnew(GDScript);
	String error_message;
	const Error load_error = GDScriptBytecodeLoader::load_full(p_bytes, loaded, p_path, p_resolver, nullptr, &error_message);
	REQUIRE_MESSAGE(load_error == OK, error_message);
	REQUIRE(loaded->is_valid());
	REQUIRE(loaded->is_bytecode_backed());
	return loaded;
}

static bool buffer_contains_ascii(const Vector<uint8_t> &p_buffer, const char *p_needle) {
	const size_t needle_length = strlen(p_needle);
	if (needle_length == 0 || static_cast<size_t>(p_buffer.size()) < needle_length) {
		return false;
	}
	const uint8_t *haystack = p_buffer.ptr();
	const size_t limit = static_cast<size_t>(p_buffer.size()) - needle_length;
	for (size_t index = 0; index <= limit; index++) {
		if (memcmp(haystack + index, p_needle, needle_length) == 0) {
			return true;
		}
	}
	return false;
}

static void collect_opcodes_recursive(const GDScriptFunction *p_function, HashSet<int> &r_opcodes, HashMap<int, Pair<GDScriptFunction *, int>> &r_vararg_locations) {
	if (p_function == nullptr) {
		return;
	}
	const Vector<int> &code = GDScriptBytecodeTestAccessor::mutable_code(const_cast<GDScriptFunction *>(p_function));
	int ip = 0;
	while (ip < code.size()) {
		const int raw_opcode = code[ip];
		r_opcodes.insert(raw_opcode);
		const GDScriptBytecodeFormat::OpcodeLayout &layout = GDScriptFunction::get_opcode_layout(static_cast<GDScriptFunction::Opcode>(raw_opcode));
		if (raw_opcode == GDScriptFunction::OPCODE_END) {
			break;
		}
		if (!layout.is_vararg) {
			ip += layout.word_count;
		} else {
			if (!r_vararg_locations.has(raw_opcode)) {
				r_vararg_locations.insert(raw_opcode, Pair<GDScriptFunction *, int>(const_cast<GDScriptFunction *>(p_function), ip));
			}
			const int instr_arg_count = code[ip + 1];
			ip = ip + 2 + instr_arg_count + layout.word_count;
		}
	}
	const Vector<GDScriptFunction *> &lambdas = GDScriptBytecodeTestAccessor::mutable_lambdas(const_cast<GDScriptFunction *>(p_function));
	for (int index = 0; index < lambdas.size(); index++) {
		collect_opcodes_recursive(lambdas[index], r_opcodes, r_vararg_locations);
	}
}

static size_t find_section_offset(const Vector<uint8_t> &p_buffer, GDScriptBytecodeFormat::SectionId p_target_section) {
	// Walk Header + ScriptFlags to Section 0.
	size_t offset = 4 + 4 + 16 + 1 + 4; // magic + version + abi + real_t + opcode_end
	REQUIRE(offset + 4 <= static_cast<size_t>(p_buffer.size()));
	uint32_t diag_config_len = decode_uint32(p_buffer.ptr() + offset);
	offset += 4 + diag_config_len;
	REQUIRE(offset + 4 <= static_cast<size_t>(p_buffer.size()));
	uint32_t diag_hash_len = decode_uint32(p_buffer.ptr() + offset);
	offset += 4 + diag_hash_len;
	offset += 4; // ScriptFlags

	if (p_target_section == GDScriptBytecodeFormat::SECTION_STRING_TABLE) {
		return offset;
	}
	// Skip Section 0 (STRING_TABLE).
	REQUIRE(decode_uint32(p_buffer.ptr() + offset) == GDScriptBytecodeFormat::SECTION_STRING_TABLE);
	offset += 4;
	uint32_t string_count = decode_uint32(p_buffer.ptr() + offset);
	offset += 4;
	for (uint32_t i = 0; i < string_count; i++) {
		uint32_t len = decode_uint32(p_buffer.ptr() + offset);
		offset += 4 + len;
	}
	if (p_target_section == GDScriptBytecodeFormat::SECTION_DEPENDENCIES) {
		return offset;
	}
	// Skip Section 1 (DEPENDENCIES).
	REQUIRE(decode_uint32(p_buffer.ptr() + offset) == GDScriptBytecodeFormat::SECTION_DEPENDENCIES);
	offset += 4;
	uint32_t dep_count = decode_uint32(p_buffer.ptr() + offset);
	offset += 4 + dep_count * 4;
	return offset;
}

TEST_SUITE("[Modules][GDScript][Bytecode]") {
	TEST_CASE("Header and ABI compatibility guard rejects all mismatches") {
		Ref<GDScript> script = compile_source("extends RefCounted\nfunc ping() -> int:\n\treturn 42\n");
		const Vector<uint8_t> valid_bytes = export_script_bytes(script);

		GDScriptBytecodeLoader::HeaderInfo header;
		String error_message;
		CHECK(GDScriptBytecodeLoader::check_header(valid_bytes, &header, &error_message) == OK);
		CHECK(header.format_version == GDScriptBytecodeFormat::FORMAT_VERSION);
		CHECK(header.real_t_size == sizeof(real_t));
		CHECK(header.opcode_end == static_cast<uint32_t>(GDScriptFunction::OPCODE_END));

		// Bad magic.
		{
			Vector<uint8_t> mutated = valid_bytes;
			mutated.write[0] = 'X';
			CHECK(GDScriptBytecodeLoader::check_header(mutated) != OK);
		}
		// Wrong FORMAT_VERSION.
		{
			Vector<uint8_t> mutated = valid_bytes;
			encode_uint32(GDScriptBytecodeFormat::FORMAT_VERSION + 1, mutated.ptrw() + 4);
			CHECK(GDScriptBytecodeLoader::check_header(mutated) != OK);
		}
		// Flipped byte in 16-byte GDSCRIPT_BYTECODE_ABI_ID.
		{
			Vector<uint8_t> mutated = valid_bytes;
			mutated.write[8 + 7] ^= 0x01;
			CHECK(GDScriptBytecodeLoader::check_header(mutated) != OK);
		}
		// Wrong real_t_size.
		{
			Vector<uint8_t> mutated = valid_bytes;
			mutated.write[24] ^= 0x04;
			CHECK(GDScriptBytecodeLoader::check_header(mutated) != OK);
		}
		// Wrong opcode_end.
		{
			Vector<uint8_t> mutated = valid_bytes;
			encode_uint32(static_cast<uint32_t>(GDScriptFunction::OPCODE_END) + 1u, mutated.ptrw() + 25);
			CHECK(GDScriptBytecodeLoader::check_header(mutated) != OK);
		}
		// Unknown ScriptFlags bit.
		{
			Vector<uint8_t> mutated = valid_bytes;
			const size_t section0_offset = find_section_offset(mutated, GDScriptBytecodeFormat::SECTION_STRING_TABLE);
			encode_uint32(1u << 2, mutated.ptrw() + section0_offset - 4);
			CHECK(GDScriptBytecodeLoader::check_header(mutated) != OK);
		}
	}

	TEST_CASE("All 11 TaggedVariant tags round-trip and disallowed inline/live variants are rejected") {
		Ref<GDScript> external_gdscript = compile_source("extends RefCounted\nclass Sub:\n\tpass\n", "res://external_dep.gd");
		Ref<Resource> external_resource = memnew(Resource);
		external_resource->set_path_cache("res://external_res.tres");
		Ref<ScriptExtension> external_non_gd_script = memnew(ScriptExtension);
		external_non_gd_script->set_path_cache("res://external_custom.cs");

		Ref<GDScript> script = compile_source(R"(
extends RefCounted

class LocalInner:
	pass

const C_NIL = null
const C_BOOL: bool = true
const C_INT: int = -123456
const C_FLOAT: float = 3.25
const C_STR: String = "hello"
const C_VEC2: Vector2 = Vector2(1.5, -2.5)
const C_COLOR: Color = Color(0.25, 0.5, 0.75, 1.0)
const C_SNAME: StringName = &"my_name"
const C_NPATH: NodePath = ^"Node/Child:prop"
const C_ARRAY_RO = [1, "two", Vector2(3, 4)]
const C_DICT_RO = {"k1": 10, "k2": [20, 30]}
const C_UTIL_CALLABLE: Callable = sin
const C_GDS_UTIL_CALLABLE: Callable = len

func get_engine_singleton():
	return Engine

func get_native_class():
	return RefCounted
)",
				"res://tagged_variants_test.gd");

		// Inject remaining tags (typed arrays/dicts, external GDScript, external non-GD Script, external Resource,
		// TAG_NULL_OBJECT, TAG_DEFAULT_VALUE for Callable/Signal/RID) into script constants for full 11-tag coverage.
		HashMap<StringName, Variant> &constants = GDScriptBytecodeTestAccessor::mutable_script_constants(script.ptr());

		PackedInt32Array packed_ints;
		packed_ints.push_back(7);
		packed_ints.push_back(8);
		packed_ints.push_back(9);
		constants[StringName("C_PACKED")] = packed_ints;

		Array typed_array;
		typed_array.set_typed(Variant::INT, StringName(), Variant());
		typed_array.push_back(11);
		typed_array.push_back(22);
		typed_array.make_read_only();
		constants[StringName("C_TYPED_ARRAY")] = typed_array;

		Dictionary typed_dict;
		typed_dict.set_typed(Variant::STRING, StringName(), Variant(), Variant::INT, StringName(), Variant());
		typed_dict["alpha"] = 100;
		typed_dict.make_read_only();
		constants[StringName("C_TYPED_DICT")] = typed_dict;

		Object *engine_singleton_obj = Engine::get_singleton()->get_singleton_object(StringName("Engine"));
		REQUIRE(engine_singleton_obj != nullptr);
		constants[StringName("C_EXTERNAL_GDSCRIPT")] = external_gdscript;
		constants[StringName("C_EXTERNAL_SCRIPT")] = external_non_gd_script;
		constants[StringName("C_EXTERNAL_RESOURCE")] = external_resource;
		constants[StringName("C_ENGINE_SINGLETON")] = engine_singleton_obj;
		constants[StringName("C_NULL_OBJECT")] = Variant(static_cast<Object *>(nullptr));
		constants[StringName("C_DEFAULT_CALLABLE")] = Callable();
		constants[StringName("C_DEFAULT_SIGNAL")] = Signal();
		constants[StringName("C_DEFAULT_RID")] = RID();

		const Vector<uint8_t> bytes = export_script_bytes(script);

		// Check SECTION_DEPENDENCIES contains the 3 external paths in sorted order.
		Vector<String> dependencies;
		REQUIRE(GDScriptBytecodeLoader::read_dependencies(bytes, dependencies) == OK);
		REQUIRE(dependencies.size() == 3);
		CHECK(dependencies[0] == "res://external_custom.cs");
		CHECK(dependencies[1] == "res://external_dep.gd");
		CHECK(dependencies[2] == "res://external_res.tres");

		// Load skeleton without resolver (no I/O).
		Ref<GDScript> loaded = memnew(GDScript);
		REQUIRE(GDScriptBytecodeLoader::load_skeleton(bytes, loaded, "res://tagged_variants_test.gd") == OK);
		REQUIRE(loaded->get_subclasses().has(StringName("LocalInner")));
		Ref<GDScript> skeleton_inner = loaded->get_subclasses()[StringName("LocalInner")];

		// Load full with stub resolver; verify skeleton subclass object was reused in place.
		StubExternalResolver resolver;
		resolver.scripts["res://external_dep.gd"] = external_gdscript;
		resolver.resources["res://external_custom.cs"] = external_non_gd_script;
		resolver.resources["res://external_res.tres"] = external_resource;

		String load_error_message;
		REQUIRE_MESSAGE(GDScriptBytecodeLoader::load_full(bytes, loaded, "res://tagged_variants_test.gd", &resolver, nullptr, &load_error_message) == OK, load_error_message);
		CHECK(loaded->get_subclasses()[StringName("LocalInner")].ptr() == skeleton_inner.ptr());
		CHECK(GDScriptBytecodeTestAccessor::get_owner(skeleton_inner.ptr()) == loaded.ptr());
		CHECK(GDScriptBytecodeTestAccessor::get_path_field(skeleton_inner.ptr()) == "res://tagged_variants_test.gd");
		CHECK(GDScriptBytecodeTestAccessor::get_path_valid(skeleton_inner.ptr()));

		const HashMap<StringName, Variant> &loaded_constants = loaded->get_constants();
		CHECK(loaded_constants[StringName("C_NIL")].get_type() == Variant::NIL);
		CHECK(bool(loaded_constants[StringName("C_BOOL")]) == true);
		CHECK(int64_t(loaded_constants[StringName("C_INT")]) == -123456);
		CHECK(double(loaded_constants[StringName("C_FLOAT")]) == doctest::Approx(3.25));
		CHECK(String(loaded_constants[StringName("C_STR")]) == "hello");
		CHECK(Vector2(loaded_constants[StringName("C_VEC2")]) == Vector2(1.5, -2.5));
		CHECK(Color(loaded_constants[StringName("C_COLOR")]) == Color(0.25, 0.5, 0.75, 1.0));
		CHECK(StringName(loaded_constants[StringName("C_SNAME")]) == StringName("my_name"));
		CHECK(NodePath(loaded_constants[StringName("C_NPATH")]) == NodePath("Node/Child:prop"));
		CHECK(PackedInt32Array(loaded_constants[StringName("C_PACKED")]).size() == 3);

		Array loaded_typed_array = loaded_constants[StringName("C_TYPED_ARRAY")];
		CHECK(loaded_typed_array.is_read_only());
		CHECK(loaded_typed_array.get_typed_builtin() == Variant::INT);
		REQUIRE(loaded_typed_array.size() == 2);
		CHECK(int(loaded_typed_array[0]) == 11);
		CHECK(int(loaded_typed_array[1]) == 22);

		Dictionary loaded_typed_dict = loaded_constants[StringName("C_TYPED_DICT")];
		CHECK(loaded_typed_dict.is_read_only());
		CHECK(loaded_typed_dict.get_typed_key_builtin() == Variant::STRING);
		CHECK(loaded_typed_dict.get_typed_value_builtin() == Variant::INT);
		CHECK(int(loaded_typed_dict["alpha"]) == 100);

		CHECK(Object::cast_to<GDScript>(loaded_constants[StringName("LocalInner")].operator Object *()) == skeleton_inner.ptr());
		CHECK(Object::cast_to<GDScript>(loaded_constants[StringName("C_EXTERNAL_GDSCRIPT")].operator Object *()) == external_gdscript.ptr());
		CHECK(Object::cast_to<Script>(loaded_constants[StringName("C_EXTERNAL_SCRIPT")].operator Object *()) == external_non_gd_script.ptr());
		CHECK(Object::cast_to<Resource>(loaded_constants[StringName("C_EXTERNAL_RESOURCE")].operator Object *()) == external_resource.ptr());
		CHECK(loaded_constants[StringName("C_ENGINE_SINGLETON")].operator Object *() == engine_singleton_obj);

		Callable loaded_util = loaded_constants[StringName("C_UTIL_CALLABLE")];
		REQUIRE(loaded_util.is_valid());
		CHECK(double(loaded_util.call(0.0)) == doctest::Approx(0.0));
		Callable loaded_gds_util = loaded_constants[StringName("C_GDS_UTIL_CALLABLE")];
		REQUIRE(loaded_gds_util.is_valid());
		CHECK(int(loaded_gds_util.call("abcd")) == 4);

		CHECK(loaded_constants[StringName("C_NULL_OBJECT")].get_type() == Variant::OBJECT);
		CHECK(loaded_constants[StringName("C_NULL_OBJECT")].operator Object *() == nullptr);
		CHECK(loaded_constants[StringName("C_DEFAULT_CALLABLE")].get_type() == Variant::CALLABLE);
		CHECK(loaded_constants[StringName("C_DEFAULT_CALLABLE")].operator Callable().is_null());
		CHECK(loaded_constants[StringName("C_DEFAULT_SIGNAL")].get_type() == Variant::SIGNAL);
		CHECK(loaded_constants[StringName("C_DEFAULT_SIGNAL")].operator Signal().is_null());
		CHECK(loaded_constants[StringName("C_DEFAULT_RID")].get_type() == Variant::RID);
		CHECK(!loaded_constants[StringName("C_DEFAULT_RID")].operator RID().is_valid());

		// Verify exporter rejects freed object, unknown live Object, unnamed Resource, live Callable, live Signal, valid RID.
		{
			Object *ephemeral = memnew(Object);
			Variant freed_variant(ephemeral);
			memdelete(ephemeral);

			Ref<GDScript> bad_script = compile_source("extends RefCounted\n");
			GDScriptBytecodeTestAccessor::mutable_script_constants(bad_script.ptr())[StringName("BAD")] = freed_variant;
			Vector<uint8_t> out;
			CHECK(GDScriptBytecodeExporter::serialize(bad_script, out) != OK);
		}
		{
			Ref<RefCounted> live_obj = memnew(RefCounted);
			Ref<GDScript> bad_script = compile_source("extends RefCounted\n");
			GDScriptBytecodeTestAccessor::mutable_script_constants(bad_script.ptr())[StringName("BAD")] = live_obj;
			Vector<uint8_t> out;
			CHECK(GDScriptBytecodeExporter::serialize(bad_script, out) != OK);
		}
		{
			Ref<Resource> unnamed_res = memnew(Resource);
			Ref<GDScript> bad_script = compile_source("extends RefCounted\n");
			GDScriptBytecodeTestAccessor::mutable_script_constants(bad_script.ptr())[StringName("BAD")] = unnamed_res;
			Vector<uint8_t> out;
			CHECK(GDScriptBytecodeExporter::serialize(bad_script, out) != OK);
		}
		{
			Ref<RefCounted> target = memnew(RefCounted);
			Ref<GDScript> bad_script = compile_source("extends RefCounted\n");
			GDScriptBytecodeTestAccessor::mutable_script_constants(bad_script.ptr())[StringName("BAD")] = Callable(target.ptr(), "get_class");
			Vector<uint8_t> out;
			CHECK(GDScriptBytecodeExporter::serialize(bad_script, out) != OK);
		}
		{
			Ref<RefCounted> target = memnew(RefCounted);
			Ref<GDScript> bad_script = compile_source("extends RefCounted\n");
			GDScriptBytecodeTestAccessor::mutable_script_constants(bad_script.ptr())[StringName("BAD")] = Signal(target.ptr(), "script_changed");
			Vector<uint8_t> out;
			CHECK(GDScriptBytecodeExporter::serialize(bad_script, out) != OK);
		}
		{
			Ref<GDScript> bad_script = compile_source("extends RefCounted\n");
			GDScriptBytecodeTestAccessor::mutable_script_constants(bad_script.ptr())[StringName("BAD")] = RID::from_uint64(12345);
			Vector<uint8_t> out;
			CHECK(GDScriptBytecodeExporter::serialize(bad_script, out) != OK);
		}

		// Verify loader rejects ARRAY, DICTIONARY, OBJECT, CALLABLE, SIGNAL, RID smuggled inside TAG_INLINE_VARIANT.
		Ref<RefCounted> inline_obj = memnew(RefCounted);
		const Variant disallowed_inline_variants[] = {
			Variant(Array()),
			Variant(Dictionary()),
			Variant(inline_obj),
			Variant(Callable()),
			Variant(Signal()),
			Variant(RID()),
		};
		for (const Variant &disallowed_value : disallowed_inline_variants) {
			int encoded_len = 0;
			REQUIRE(encode_variant(disallowed_value, nullptr, encoded_len, false) == OK);
			Vector<uint8_t> encoded_payload;
			encoded_payload.resize(encoded_len);
			int written_len = 0;
			REQUIRE(encode_variant(disallowed_value, encoded_payload.ptrw(), written_len, false) == OK);

			// Build minimal valid container whose single constant uses TAG_INLINE_VARIANT with the disallowed payload.
			RawBufferBuilder builder;
			builder.write_valid_header(0);
			// Section 0: string table ("res://x.gd", "", "RefCounted", "BAD")
			builder.u32(GDScriptBytecodeFormat::SECTION_STRING_TABLE);
			builder.u32(4);
			builder.utf8("res://x.gd");
			builder.utf8("");
			builder.utf8("RefCounted");
			builder.utf8("BAD");
			// Section 1: dependencies (0)
			builder.u32(GDScriptBytecodeFormat::SECTION_DEPENDENCIES);
			builder.u32(0);
			// Section 2: skeleton (1 class)
			builder.u32(GDScriptBytecodeFormat::SECTION_SKELETON);
			builder.u32(1);
			builder.u32(0); // fqcn
			builder.u32(1); // local_name
			builder.u32(1); // global_name
			builder.u32(1); // icon
			builder.u32(2); // native "RefCounted"
			builder.u8(0); // class_flags
			builder.u8(GDScriptBytecodeFormat::BASE_CLASS_NATIVE_ONLY);
			builder.u32(0); // subclass_count
			// Section 3: class body with 1 constant encoded as TAG_INLINE_VARIANT
			builder.u32(GDScriptBytecodeFormat::SECTION_CLASS_BODIES);
			builder.u32(0); // members
			builder.u32(0); // member_indices
			builder.u32(0); // static_vars
			builder.u32(1); // constants
			builder.u32(3); // "BAD"
			builder.u8(GDScriptBytecodeFormat::TAG_INLINE_VARIANT);
			builder.u32(static_cast<uint32_t>(written_len));
			builder.raw(encoded_payload.ptr(), written_len);
			// Minimal remainder (won't be reached because TAG_INLINE_VARIANT rejects immediately)
			builder.u32(0);

			Ref<GDScript> target = memnew(GDScript);
			CHECK(GDScriptBytecodeLoader::load_full(builder.get_bytes(), target, "res://x.gd", nullptr) == ERR_INVALID_DATA);
		}
	}

	TEST_CASE("Default-argument jump-table sentinel and vararg round-trip") {
		Ref<GDScript> script = compile_source(R"(
extends RefCounted

func f0(a: int, b: int) -> int:
	return a * 10 + b

func f1(a: int, b: int = 7) -> int:
	return a * 10 + b

func f3(a: int, b: int = 2, c: int = 3, d: int = 4) -> Array:
	return [a, b, c, d]

func fall(a: int = 10, b: int = 20, c: int = 30) -> Array:
	return [a, b, c]

func _compute_default() -> int:
	return 99

func f_expr(a: int = 5, b: int = _compute_default()) -> Array:
	return [a, b]

func f_vararg(a: int, b: int = 5, ...rest: Array) -> Array:
	return [a, b, rest]
)",
				"res://default_args_test.gd");

		const Vector<uint8_t> bytes = export_script_bytes(script);
		Ref<GDScript> loaded = load_script_bytes(bytes, "res://default_args_test.gd");

		Ref<RefCounted> instance = memnew(RefCounted);
		instance->set_script(loaded);

		// 0 optional args.
		CHECK(int(instance->call("f0", 3, 4)) == 34);

		// 1 optional arg: defarg 1 and sentinel defarg 0.
		CHECK(int(instance->call("f1", 3)) == 37);
		CHECK(int(instance->call("f1", 3, 9)) == 39);

		// 3 optional args: defarg 3, 2, 1, and sentinel 0.
		CHECK(Array(instance->call("f3", 1)) == Array({ 1, 2, 3, 4 }));
		CHECK(Array(instance->call("f3", 1, 20)) == Array({ 1, 20, 3, 4 }));
		CHECK(Array(instance->call("f3", 1, 20, 30)) == Array({ 1, 20, 30, 4 }));
		CHECK(Array(instance->call("f3", 1, 20, 30, 40)) == Array({ 1, 20, 30, 40 }));

		// All-optional: defarg 3, 2, 1, and sentinel 0.
		CHECK(Array(instance->call("fall")) == Array({ 10, 20, 30 }));
		CHECK(Array(instance->call("fall", 1)) == Array({ 1, 20, 30 }));
		CHECK(Array(instance->call("fall", 1, 2)) == Array({ 1, 2, 30 }));
		CHECK(Array(instance->call("fall", 1, 2, 3)) == Array({ 1, 2, 3 }));

		// Non-constant default expression.
		CHECK(Array(instance->call("f_expr")) == Array({ 5, 99 }));
		CHECK(Array(instance->call("f_expr", 6)) == Array({ 6, 99 }));
		CHECK(Array(instance->call("f_expr", 6, 7)) == Array({ 6, 7 }));

		// Vararg + optional arg.
		CHECK(Array(instance->call("f_vararg", 1)) == Array({ 1, 5, Array() }));
		CHECK(Array(instance->call("f_vararg", 1, 2)) == Array({ 1, 2, Array() }));
		CHECK(Array(instance->call("f_vararg", 1, 2, "x", "y")) == Array({ 1, 2, Array({ "x", "y" }) }));
	}

	TEST_CASE("Golden test covering all 24 variable-width opcodes and rejecting instr_arg_count mutations") {
		Ref<GDScript> script = compile_source(R"(
extends RefCounted

class BaseHelper:
	var member_val: int = 7

	func base_fn(x: int) -> int:
		return x + 100

class ChildHelper extends BaseHelper:
	func helper_method(a: int) -> int:
		return a * 2

	func exercise_sync(untyped_obj, untyped_a, untyped_b, untyped_path, typed_float: float, typed_str: String, typed_bool: bool) -> Dictionary:
		# 1. OPCODE_CONSTRUCT (untyped args)
		var v_dyn = Vector2(untyped_a, untyped_b)
		# 2. OPCODE_CONSTRUCT_VALIDATED (typed non-const args)
		var v_val: Vector2 = Vector2(typed_float, typed_float)
		# 3. OPCODE_CONSTRUCT_ARRAY
		var arr_plain = [untyped_a, untyped_b]
		# 4. OPCODE_CONSTRUCT_TYPED_ARRAY
		var arr_typed: Array[int] = [1, 2, 3]
		# 5. OPCODE_CONSTRUCT_DICTIONARY
		var dict_plain = {"a": untyped_a}
		# 6. OPCODE_CONSTRUCT_TYPED_DICTIONARY
		var dict_typed: Dictionary[String, int] = {"k": 5}
		# 7. OPCODE_CALL (discard return)
		untyped_obj.helper_method(3)
		# 8. OPCODE_CALL_RETURN
		var call_ret = untyped_obj.helper_method(4)
		# 9. OPCODE_CALL_UTILITY (unvalidated)
		var util_unval = str(untyped_a, ":", untyped_b)
		# 10. OPCODE_CALL_UTILITY_VALIDATED
		var util_val: float = clampf(typed_float, 0.0, 1.0)
		# 11. OPCODE_CALL_GDSCRIPT_UTILITY
		var gds_util: int = len(typed_str)
		# 12. OPCODE_CALL_BUILTIN_TYPE_VALIDATED
		var upper_s: String = typed_str.to_upper()
		# 13. OPCODE_CALL_SELF_BASE
		var super_res: int = super.base_fn(5)
		# 14. OPCODE_CALL_METHOD_BIND (non-validated, discard return via default/vararg/untyped arg)
		var obj: RefCounted = RefCounted.new()
		obj.set_meta("m", untyped_a)
		# 15. OPCODE_CALL_METHOD_BIND_RET (non-validated, capture return)
		var meta_val = obj.get_meta("m", untyped_b)
		# 16. OPCODE_CALL_BUILTIN_STATIC (untyped arg avoids validated builtin optimization)
		var static_builtin: String = String.num(untyped_a, 2)
		# 17. OPCODE_CALL_NATIVE_STATIC (non-validated via untyped arg)
		var native_static_dyn = FileAccess.file_exists(untyped_path)
		# 18. OPCODE_CALL_NATIVE_STATIC_VALIDATED_RETURN
		var native_static_val_ret: bool = FileAccess.file_exists(typed_str)
		# 20. OPCODE_CALL_METHOD_BIND_VALIDATED_RETURN
		var cls_name: String = obj.get_class()
		# 21. OPCODE_CALL_METHOD_BIND_VALIDATED_NO_RETURN
		obj.set_block_signals(typed_bool)
		# 22. OPCODE_CREATE_LAMBDA
		var captured: int = 10
		var lam = func(x: int) -> int:
			return x + captured
		# 23. OPCODE_CREATE_SELF_LAMBDA
		var self_lam = func(x: int) -> int:
			return self.member_val + x + captured
		return {
			"v_dyn": v_dyn,
			"v_val": v_val,
			"arr_plain": arr_plain,
			"arr_typed": arr_typed,
			"dict_plain": dict_plain,
			"dict_typed": dict_typed,
			"call_ret": call_ret,
			"util_unval": util_unval,
			"util_val": util_val,
			"gds_util": gds_util,
			"upper_s": upper_s,
			"super_res": super_res,
			"meta_val": meta_val,
			"static_builtin": static_builtin,
			"native_static_dyn": native_static_dyn,
			"native_static_val_ret": native_static_val_ret,
			"cls_name": cls_name,
			"lam": lam.call(5),
			"self_lam": self_lam.call(5),
		}

	# 19. OPCODE_CALL_NATIVE_STATIC_VALIDATED_NO_RETURN
	func exercise_native_static_void(typed_bool: bool) -> void:
		Thread.set_thread_safety_checks_enabled(typed_bool)

	# 24. OPCODE_CALL_ASYNC
	func exercise_async(untyped_obj) -> int:
		return await untyped_obj.helper_method(6)
)",
				"res://all_24_vararg_opcodes.gd");

		// Collect all variable-width opcodes present in ChildHelper.
		const Vector<uint8_t> bytes = export_script_bytes(script);
		Ref<GDScript> loaded = load_script_bytes(bytes, "res://all_24_vararg_opcodes.gd");
		REQUIRE(loaded->get_subclasses().has(StringName("ChildHelper")));
		Ref<GDScript> child_class = loaded->get_subclasses()[StringName("ChildHelper")];

		HashSet<int> observed_opcodes;
		HashMap<int, Pair<GDScriptFunction *, int>> vararg_locations;
		for (const KeyValue<StringName, GDScriptFunction *> &entry : child_class->get_member_functions()) {
			collect_opcodes_recursive(entry.value, observed_opcodes, vararg_locations);
		}

		// Verify EVERY ONE of the 24 variable-width opcodes in gdscript_opcode_layout.inc was compiled & loaded.
		int total_vararg_opcodes = 0;
		for (int op = 0; op <= GDScriptFunction::OPCODE_END; op++) {
			const GDScriptBytecodeFormat::OpcodeLayout &layout = GDScriptFunction::get_opcode_layout(static_cast<GDScriptFunction::Opcode>(op));
			if (layout.is_vararg) {
				total_vararg_opcodes++;
				CHECK_MESSAGE(vararg_locations.has(op), vformat("Missing variable-width opcode %d in golden fixture.", op));
			}
		}
		CHECK(total_vararg_opcodes == 24);
		REQUIRE(vararg_locations.size() == 24);

		// Execute loaded ChildHelper and verify runtime results.
		Ref<RefCounted> instance = memnew(RefCounted);
		instance->set_script(child_class);
		Variant res_var = instance->call("exercise_sync", instance, 1.0, 2.0, "res://nonexistent_test_path.tmp", 0.5, "abc", true);
		REQUIRE(res_var.get_type() == Variant::DICTIONARY);
		Dictionary res = res_var;
		CHECK(Vector2(res["v_dyn"]) == Vector2(1.0, 2.0));
		CHECK(Vector2(res["v_val"]) == Vector2(0.5, 0.5));
		CHECK(int(res["call_ret"]) == 8);
		CHECK(String(res["util_unval"]) == "1.0:2.0");
		CHECK(double(res["util_val"]) == doctest::Approx(0.5));
		CHECK(int(res["gds_util"]) == 3);
		CHECK(String(res["upper_s"]) == "ABC");
		CHECK(int(res["super_res"]) == 105);
		CHECK(double(res["meta_val"]) == doctest::Approx(1.0));
		CHECK(String(res["static_builtin"]) == "1.0");
		CHECK(String(res["cls_name"]) == "RefCounted");
		CHECK(int(res["lam"]) == 15);
		CHECK(int(res["self_lam"]) == 22);
		CHECK(int(instance->call("exercise_async", instance)) == 12);
		ERR_PRINT_OFF;
		instance->call("exercise_native_static_void", true);
		ERR_PRINT_ON;

		// For EACH of the 24 variable-width opcodes, mutate instr_arg_count (+/- 1) and trailing count (+1)
		// and assert GDScriptBytecodeVerifier rejects every single mutation!
		for (const KeyValue<int, Pair<GDScriptFunction *, int>> &loc : vararg_locations) {
			GDScriptFunction *func = loc.value.first;
			const int ip = loc.value.second;
			const GDScriptBytecodeFormat::OpcodeLayout &layout = GDScriptFunction::get_opcode_layout(static_cast<GDScriptFunction::Opcode>(loc.key));
			Vector<int> &code = GDScriptBytecodeTestAccessor::mutable_code(func);

			// Baseline verifies cleanly.
			REQUIRE(GDScriptBytecodeVerifier::verify_function(func, child_class.ptr()) == OK);

			const int original_arg_count = code[ip + 1];
			// Mutation A: instr_arg_count + 1
			code.write[ip + 1] = original_arg_count + 1;
			CHECK_MESSAGE(GDScriptBytecodeVerifier::verify_function(func, child_class.ptr()) != OK, vformat("Verifier accepted instr_arg_count+1 for opcode %d", loc.key));
			// Mutation B: instr_arg_count - 1
			code.write[ip + 1] = original_arg_count - 1;
			CHECK_MESSAGE(GDScriptBytecodeVerifier::verify_function(func, child_class.ptr()) != OK, vformat("Verifier accepted instr_arg_count-1 for opcode %d", loc.key));
			code.write[ip + 1] = original_arg_count;

			// Mutation C: trailing count word + 1
			const int count_offset = ip + 2 + original_arg_count + layout.count_tail_offset;
			const int original_count = code[count_offset];
			code.write[count_offset] = original_count + 1;
			CHECK_MESSAGE(GDScriptBytecodeVerifier::verify_function(func, child_class.ptr()) != OK, vformat("Verifier accepted count+1 for opcode %d", loc.key));
			code.write[count_offset] = original_count;
		}
	}

	TEST_CASE("Deterministic canonical serialization across repeated runs, insertion orders, and live operator cache") {
		String source_a = R"(
extends RefCounted
signal sig_z(x: int)
signal sig_a(y: String)
const CONST_Z = 9
const CONST_A = 1
static var stat_z: int = 2
static var stat_a: int = 1
var mem_z: int = 3
var mem_a: int = 4
class SubZ:
	var v: int = 1
class SubA:
	var v: int = 2
@rpc("any_peer")
func fn_z(u, v):
	return u + v
@rpc("authority")
func fn_a(x: int) -> int:
	return x + 1
)";
		Ref<GDScript> script = compile_source(source_a, "res://determinism_test.gd");
		const Vector<uint8_t> bytes_first = export_script_bytes(script);
		const Vector<uint8_t> bytes_second = export_script_bytes(script);
		CHECK(bytes_first == bytes_second);

		// Execute dynamic OPCODE_OPERATOR to populate live inline cache, then re-export and verify byte-identical output.
		Ref<RefCounted> instance = memnew(RefCounted);
		instance->set_script(script);
		CHECK(int(instance->call("fn_z", 10, 20)) == 30);
		const Vector<uint8_t> bytes_after_exec = export_script_bytes(script);
		CHECK(bytes_first == bytes_after_exec);

		// Reverse HashMap/HashSet/Dictionary insertion orders on a second loaded copy and verify byte-identical export.
		Ref<GDScript> script_reordered = compile_source(source_a, "res://determinism_test.gd");
		{
			Dictionary orig_rpc = GDScriptBytecodeTestAccessor::mutable_rpc_config(script_reordered.ptr());
			Dictionary reversed_rpc;
			LocalVector<Variant> keys = orig_rpc.get_key_list();
			for (int i = static_cast<int>(keys.size()) - 1; i >= 0; i--) {
				reversed_rpc[keys[i]] = orig_rpc[keys[i]];
			}
			GDScriptBytecodeTestAccessor::mutable_rpc_config(script_reordered.ptr()) = reversed_rpc;

			HashMap<StringName, Variant> orig_consts(GDScriptBytecodeTestAccessor::mutable_script_constants(script_reordered.ptr()));
			Vector<StringName> const_keys;
			for (const KeyValue<StringName, Variant> &e : orig_consts) {
				const_keys.push_back(e.key);
			}
			GDScriptBytecodeTestAccessor::mutable_script_constants(script_reordered.ptr()).clear();
			for (int i = const_keys.size() - 1; i >= 0; i--) {
				GDScriptBytecodeTestAccessor::mutable_script_constants(script_reordered.ptr()).insert(const_keys[i], orig_consts[const_keys[i]]);
			}

			HashMap<StringName, Ref<GDScript>> orig_subs(GDScriptBytecodeTestAccessor::mutable_subclasses(script_reordered.ptr()));
			Vector<StringName> sub_keys;
			for (const KeyValue<StringName, Ref<GDScript>> &e : orig_subs) {
				sub_keys.push_back(e.key);
			}
			GDScriptBytecodeTestAccessor::mutable_subclasses(script_reordered.ptr()).clear();
			for (int i = sub_keys.size() - 1; i >= 0; i--) {
				GDScriptBytecodeTestAccessor::mutable_subclasses(script_reordered.ptr()).insert(sub_keys[i], orig_subs[sub_keys[i]]);
			}
		}
		const Vector<uint8_t> bytes_reordered = export_script_bytes(script_reordered);
		CHECK(bytes_first == bytes_reordered);
	}

	TEST_CASE("Zero-source-leak check in release bytecode profile preserves MethodInfo parameter names") {
		const String script_path = TestUtils::get_temp_path("test_bytecode_zero_leak.gd");
		{
			Ref<FileAccess> file = FileAccess::open(script_path, FileAccess::WRITE);
			REQUIRE(file.is_valid());
			file->store_string(R"(extends RefCounted
# SECRET_COMMENT_TOKEN_9911
func compute(visible_param_name_9944: int) -> int:
	# ANOTHER_SECRET_COMMENT_8877
	assert(visible_param_name_9944 > 0, "SECRET_ASSERT_MSG_9933")
	var secret_local_var_name_9922: int = visible_param_name_9944 * 2
	return secret_local_var_name_9922
)");
			file->close();
		}
		struct TempFileCleanup {
			String path;
			~TempFileCleanup() { DirAccess::remove_absolute(path); }
		} cleanup{ script_path };

		GDScriptExportCompileSession session(/*p_release_profile=*/true);
		Error compile_error = OK;
		Ref<GDScript> compiled = session.get_or_compile(script_path, compile_error);
		REQUIRE(compile_error == OK);
		REQUIRE(compiled.is_valid());

		GDScriptBytecodeExporter::Options export_options;
		export_options.release_profile = true;
		export_options.emit_line_opcodes = false;
		const Vector<uint8_t> bytes = export_script_bytes(compiled, export_options);

		CHECK_FALSE(buffer_contains_ascii(bytes, "SECRET_COMMENT_TOKEN_9911"));
		CHECK_FALSE(buffer_contains_ascii(bytes, "ANOTHER_SECRET_COMMENT_8877"));
		CHECK_FALSE(buffer_contains_ascii(bytes, "secret_local_var_name_9922"));
		CHECK_FALSE(buffer_contains_ascii(bytes, "SECRET_ASSERT_MSG_9933"));
		CHECK(buffer_contains_ascii(bytes, "visible_param_name_9944"));

		Ref<GDScript> loaded = load_script_bytes(bytes, script_path);
		REQUIRE(loaded->get_member_functions().has(StringName("compute")));
		GDScriptFunction *compute_fn = loaded->get_member_functions()[StringName("compute")];
		CHECK(GDScriptBytecodeTestAccessor::get_stack_debug(compute_fn).is_empty());
		REQUIRE(compute_fn->get_method_info().arguments.size() == 1);
		CHECK(compute_fn->get_method_info().arguments[0].name == "visible_param_name_9944");

		Ref<RefCounted> instance = memnew(RefCounted);
		instance->set_script(loaded);
		// Assert was stripped in release profile, so even negative input computes cleanly without tripping assert.
		CHECK(int(instance->call("compute", -5)) == -10);
	}

	TEST_CASE("collect_unsupported_named_globals walks member functions, special functions, lambdas, and subclasses") {
#ifdef TOOLS_ENABLED
		GDScriptLanguage::get_singleton()->init();
		const StringName global_member("TestUnsupportedGlobalMember");
		const StringName global_lambda("TestUnsupportedGlobalLambda");
		const StringName global_sub("TestUnsupportedGlobalSub");
		const StringName global_init("TestUnsupportedGlobalInit");

		const String autoload_dummy_path = TestUtils::get_temp_path("test_unsupported_named_global_dummy.gd");
		{
			Ref<FileAccess> dummy_file = FileAccess::open(autoload_dummy_path, FileAccess::WRITE);
			REQUIRE(dummy_file.is_valid());
			dummy_file->store_string("extends RefCounted\n");
			dummy_file->close();
		}

		ProjectSettings::get_singleton()->set_setting("autoload/" + String(global_member), "*" + autoload_dummy_path);
		ProjectSettings::get_singleton()->set_setting("autoload/" + String(global_lambda), "*" + autoload_dummy_path);
		ProjectSettings::get_singleton()->set_setting("autoload/" + String(global_sub), "*" + autoload_dummy_path);
		ProjectSettings::get_singleton()->set_setting("autoload/" + String(global_init), "*" + autoload_dummy_path);

		GDScriptLanguage::get_singleton()->add_named_global_constant(global_member, Variant(1));
		GDScriptLanguage::get_singleton()->add_named_global_constant(global_lambda, Variant(2));
		GDScriptLanguage::get_singleton()->add_named_global_constant(global_sub, Variant(3));
		GDScriptLanguage::get_singleton()->add_named_global_constant(global_init, Variant(4));

		struct NamedGlobalCleanup {
			StringName a, b, c, d;
			String dummy_path;
			~NamedGlobalCleanup() {
				ProjectSettings::get_singleton()->set_setting("autoload/" + String(a), Variant());
				ProjectSettings::get_singleton()->set_setting("autoload/" + String(b), Variant());
				ProjectSettings::get_singleton()->set_setting("autoload/" + String(c), Variant());
				ProjectSettings::get_singleton()->set_setting("autoload/" + String(d), Variant());
				GDScriptLanguage::get_singleton()->remove_named_global_constant(a);
				GDScriptLanguage::get_singleton()->remove_named_global_constant(b);
				GDScriptLanguage::get_singleton()->remove_named_global_constant(c);
				GDScriptLanguage::get_singleton()->remove_named_global_constant(d);
				DirAccess::remove_absolute(dummy_path);
			}
		} cleanup{ global_member, global_lambda, global_sub, global_init, autoload_dummy_path };

		Ref<GDScript> script = compile_source(R"(
extends RefCounted
var field_val = TestUnsupportedGlobalInit
func use_globals():
	var lam = func(): return TestUnsupportedGlobalLambda
	return [TestUnsupportedGlobalMember, lam.call()]
class Inner:
	func use_sub():
		return TestUnsupportedGlobalSub
)",
				"res://unsupported_named_globals.gd");

		Vector<StringName> found = GDScriptBytecodeExporter::collect_unsupported_named_globals(script);
		REQUIRE(found.size() == 4);
		CHECK(found[0] == global_init);
		CHECK(found[1] == global_lambda);
		CHECK(found[2] == global_member);
		CHECK(found[3] == global_sub);
#endif
	}

#ifdef DEBUG_ENABLED
	TEST_CASE("DEBUG_ENABLED debug names, disassemble(), runtime errors, and multi-lambda same-line profiler signatures") {
		Ref<GDScript> script = compile_source(R"(
extends RefCounted

class InnerA:
	func dup_name() -> int:
		return 1

class InnerB:
	func dup_name() -> int:
		return 2

func exercise_debug(x: float, bad_len_arg) -> Dictionary:
	var a: int = 3
	var b: int = 4
	var sum: int = a + b
	var v: Vector2 = Vector2(x, x)
	v.x = 9.0
	var vx: float = v.x
	var s: String = "hi"
	var u: String = s.to_upper()
	var c: float = clampf(x, 0.0, 1.0)
	var l: int = len(u)
	var pair = [func(n): return n + 1, func(n): return n + 2, func(n): return (func(m): return m * 2).call(n)]
	return { "sum": sum, "vx": vx, "c": c, "l": l, "p0": pair[0].call(1), "p1": pair[1].call(1), "p2": pair[2].call(3) }

func trigger_gds_utility_error(bad_arg):
	return len(bad_arg)
)",
				"res://debug_runtime_test.gd");

		const Vector<uint8_t> bytes = export_script_bytes(script);
		Ref<GDScript> loaded = load_script_bytes(bytes, "res://debug_runtime_test.gd");

		GDScriptFunction *fn = loaded->get_member_functions()[StringName("exercise_debug")];
		REQUIRE(fn != nullptr);

		// Verify all 7 reconstructed debug name vectors + func_cname + disassemble().
		CHECK(!GDScriptBytecodeTestAccessor::get_operator_names(fn).is_empty());
		CHECK(!GDScriptBytecodeTestAccessor::get_setter_names(fn).is_empty());
		CHECK(!GDScriptBytecodeTestAccessor::get_getter_names(fn).is_empty());
		CHECK(!GDScriptBytecodeTestAccessor::get_builtin_methods_names(fn).is_empty());
		CHECK(!GDScriptBytecodeTestAccessor::get_constructors_names(fn).is_empty());
		CHECK(!GDScriptBytecodeTestAccessor::get_utilities_names(fn).is_empty());
		CHECK(!GDScriptBytecodeTestAccessor::get_gds_utilities_names(fn).is_empty());
		REQUIRE(GDScriptBytecodeTestAccessor::get_func_cname(fn) != nullptr);
		CHECK(String(GDScriptBytecodeTestAccessor::get_func_cname(fn)) == "res://debug_runtime_test.gd - exercise_debug");

		// Disassemble must succeed without OOB across every reconstructed name vector.
		fn->disassemble(Vector<String>());

		// Verify multi-lambda same-line and inner-class profiler signatures are collision-free.
		HashSet<StringName> signatures;
		signatures.insert(GDScriptBytecodeTestAccessor::get_profile_signature(fn));
		const Vector<GDScriptFunction *> &lambdas = GDScriptBytecodeTestAccessor::mutable_lambdas(fn);
		REQUIRE(lambdas.size() == 3);
		for (int i = 0; i < lambdas.size(); i++) {
			StringName sig = GDScriptBytecodeTestAccessor::get_profile_signature(lambdas[i]);
			CHECK(sig != StringName());
			CHECK_FALSE(signatures.has(sig));
			signatures.insert(sig);
			const Vector<GDScriptFunction *> &nested = GDScriptBytecodeTestAccessor::mutable_lambdas(lambdas[i]);
			for (int j = 0; j < nested.size(); j++) {
				StringName nested_sig = GDScriptBytecodeTestAccessor::get_profile_signature(nested[j]);
				CHECK(nested_sig != StringName());
				CHECK_FALSE(signatures.has(nested_sig));
				signatures.insert(nested_sig);
			}
		}

		GDScriptFunction *inner_a_dup = loaded->get_subclasses()[StringName("InnerA")]->get_member_functions()[StringName("dup_name")];
		GDScriptFunction *inner_b_dup = loaded->get_subclasses()[StringName("InnerB")]->get_member_functions()[StringName("dup_name")];
		CHECK(GDScriptBytecodeTestAccessor::get_profile_signature(inner_a_dup) != GDScriptBytecodeTestAccessor::get_profile_signature(inner_b_dup));

		// Trigger GDScript utility runtime error (indexes gds_utilities_names) without OOB.
		Ref<RefCounted> instance = memnew(RefCounted);
		instance->set_script(loaded);
		ERR_PRINT_OFF;
		instance->call("trigger_gds_utility_error", 12345);
		ERR_PRINT_ON;
	}
#endif

	TEST_CASE("OPCODE_STORE_GLOBAL rebaking fails loudly on missing global and succeeds when registered") {
		GDScriptLanguage::get_singleton()->init();
		const StringName runtime_global("TestBytecodeRuntimeAutoload");
		const String autoload_setting = "autoload/" + String(runtime_global);
		const String autoload_path = TestUtils::get_temp_path("test_bc_autoload_target.gd");
		const String consumer_path = TestUtils::get_temp_path("test_bc_autoload_consumer.gd");

		{
			Ref<FileAccess> f = FileAccess::open(autoload_path, FileAccess::WRITE);
			REQUIRE(f.is_valid());
			f->store_string("extends RefCounted\n");
			f->close();
		}
		{
			Ref<FileAccess> f = FileAccess::open(consumer_path, FileAccess::WRITE);
			REQUIRE(f.is_valid());
			f->store_string("extends RefCounted\nfunc read_global():\n\treturn TestBytecodeRuntimeAutoload\n");
			f->close();
		}
		ProjectSettings::get_singleton()->set_setting(autoload_setting, "*" + autoload_path);
		if (!GDScriptLanguage::get_singleton()->get_global_map().has(runtime_global)) {
			GDScriptLanguage::get_singleton()->add_global_constant(runtime_global, Variant(4242));
		}

		struct Cleanup {
			String setting, p1, p2;
			~Cleanup() {
				ProjectSettings::get_singleton()->set_setting(setting, Variant());
				DirAccess::remove_absolute(p1);
				DirAccess::remove_absolute(p2);
			}
		} cleanup{ autoload_setting, autoload_path, consumer_path };

		GDScriptExportCompileSession session(true);
		Error compile_error = OK;
		Ref<GDScript> compiled = session.get_or_compile(consumer_path, compile_error);
		REQUIRE(compile_error == OK);
		const Vector<uint8_t> bytes = export_script_bytes(compiled);

		// Round-trip succeeds and rebakes to live global slot.
		Ref<GDScript> loaded = load_script_bytes(bytes, consumer_path);
		Ref<RefCounted> instance = memnew(RefCounted);
		instance->set_script(loaded);
		CHECK(int(instance->call("read_global")) == 4242);

		// Mutate the global name in string table to a nonexistent global name -> loader must fail loudly naming it.
		Vector<uint8_t> mutated = bytes;
		const char *needle = "TestBytecodeRuntimeAutoload";
		const char *replacement = "MissingBytecodeRuntimeGlobX";
		REQUIRE(strlen(needle) == strlen(replacement));
		bool patched = false;
		for (int i = 0; i <= mutated.size() - static_cast<int>(strlen(needle)); i++) {
			if (memcmp(mutated.ptr() + i, needle, strlen(needle)) == 0) {
				memcpy(mutated.ptrw() + i, replacement, strlen(replacement));
				patched = true;
				break;
			}
		}
		REQUIRE(patched);
		Ref<GDScript> missing_target = memnew(GDScript);
		String error_message;
		CHECK(GDScriptBytecodeLoader::load_full(mutated, missing_target, consumer_path, nullptr, nullptr, &error_message) == ERR_CANT_RESOLVE);
		CHECK(error_message.contains("MissingBytecodeRuntimeGlobX"));
		CHECK_FALSE(missing_target->is_valid());
	}

	TEST_CASE("Hostile container inputs: truncation, trailing bytes, section order, booleans, enums, budgets, and deep recursion") {
		Ref<GDScript> script = compile_source("extends RefCounted\nfunc f(x: int) -> int:\n\treturn x + 1\n", "res://hostile_base.gd");
		const Vector<uint8_t> valid_bytes = export_script_bytes(script);

		// Truncation at header, section, and body boundaries.
		const size_t section0 = find_section_offset(valid_bytes, GDScriptBytecodeFormat::SECTION_STRING_TABLE);
		const size_t section1 = find_section_offset(valid_bytes, GDScriptBytecodeFormat::SECTION_DEPENDENCIES);
		const size_t section2 = find_section_offset(valid_bytes, GDScriptBytecodeFormat::SECTION_SKELETON);
		const size_t truncate_points[] = { 0, 1, 3, 4, 8, 23, 24, 28, section0, section0 + 2, section1, section2, static_cast<size_t>(valid_bytes.size() - 1) };
		for (size_t cut : truncate_points) {
			Vector<uint8_t> truncated = valid_bytes.slice(0, static_cast<int>(cut));
			Ref<GDScript> target = memnew(GDScript);
			CHECK_MESSAGE(GDScriptBytecodeLoader::load_full(truncated, target, "res://t.gd", nullptr) != OK, vformat("Truncated buffer at %d should fail", static_cast<int>(cut)));
		}

		// Trailing bytes rejected.
		{
			Vector<uint8_t> with_trailing = valid_bytes;
			with_trailing.push_back(0x00);
			Ref<GDScript> target = memnew(GDScript);
			CHECK(GDScriptBytecodeLoader::load_full(with_trailing, target, "res://t.gd", nullptr) == ERR_INVALID_DATA);
		}

		// Reordered / duplicate section IDs rejected.
		{
			Vector<uint8_t> reordered = valid_bytes;
			encode_uint32(GDScriptBytecodeFormat::SECTION_SKELETON, reordered.ptrw() + section1);
			Ref<GDScript> target = memnew(GDScript);
			CHECK(GDScriptBytecodeLoader::load_full(reordered, target, "res://t.gd", nullptr) == ERR_INVALID_DATA);
		}
		{
			Vector<uint8_t> duplicated = valid_bytes;
			encode_uint32(GDScriptBytecodeFormat::SECTION_STRING_TABLE, duplicated.ptrw() + section1);
			Ref<GDScript> target = memnew(GDScript);
			CHECK(GDScriptBytecodeLoader::load_full(duplicated, target, "res://t.gd", nullptr) == ERR_INVALID_DATA);
		}

		// Oversized string_count / budget exhaustion rejected cleanly.
		{
			RawBufferBuilder builder;
			builder.write_valid_header(0);
			builder.u32(GDScriptBytecodeFormat::SECTION_STRING_TABLE);
			builder.u32(0x7FFFFFFFu);
			Ref<GDScript> target = memnew(GDScript);
			CHECK(GDScriptBytecodeLoader::load_full(builder.get_bytes(), target, "res://t.gd", nullptr) == ERR_INVALID_DATA);
		}

		// Invalid boolean byte (2) in SkeletonClass::tool? Wait: tool is bitflag, test bool in TAG_ARRAY read_only = 2!
		{
			RawBufferBuilder builder;
			builder.write_valid_header(0);
			builder.u32(GDScriptBytecodeFormat::SECTION_STRING_TABLE);
			builder.u32(4);
			builder.utf8("res://b.gd");
			builder.utf8("");
			builder.utf8("RefCounted");
			builder.utf8("C");
			builder.u32(GDScriptBytecodeFormat::SECTION_DEPENDENCIES);
			builder.u32(0);
			builder.u32(GDScriptBytecodeFormat::SECTION_SKELETON);
			builder.u32(1);
			builder.u32(0);
			builder.u32(1);
			builder.u32(1);
			builder.u32(1);
			builder.u32(2);
			builder.u8(0);
			builder.u8(GDScriptBytecodeFormat::BASE_CLASS_NATIVE_ONLY);
			builder.u32(0);
			builder.u32(GDScriptBytecodeFormat::SECTION_CLASS_BODIES);
			builder.u32(0); // members
			builder.u32(0); // member_indices
			builder.u32(0); // static_vars
			builder.u32(1); // constants
			builder.u32(3); // "C"
			builder.u8(GDScriptBytecodeFormat::TAG_ARRAY);
			builder.u8(2); // Invalid bool!
			Ref<GDScript> target = memnew(GDScript);
			CHECK(GDScriptBytecodeLoader::load_full(builder.get_bytes(), target, "res://b.gd", nullptr) == ERR_INVALID_DATA);
		}

		// Out-of-range VariantTag (99) & BaseClassKind (9).
		{
			RawBufferBuilder builder;
			builder.write_valid_header(0);
			builder.u32(GDScriptBytecodeFormat::SECTION_STRING_TABLE);
			builder.u32(3);
			builder.utf8("res://e.gd");
			builder.utf8("");
			builder.utf8("RefCounted");
			builder.u32(GDScriptBytecodeFormat::SECTION_DEPENDENCIES);
			builder.u32(0);
			builder.u32(GDScriptBytecodeFormat::SECTION_SKELETON);
			builder.u32(1);
			builder.u32(0);
			builder.u32(1);
			builder.u32(1);
			builder.u32(1);
			builder.u32(2);
			builder.u8(0);
			builder.u8(9); // Out-of-range base_kind!
			builder.u32(0);
			Ref<GDScript> target = memnew(GDScript);
			CHECK(GDScriptBytecodeLoader::load_full(builder.get_bytes(), target, "res://e.gd", nullptr) == ERR_INVALID_DATA);
		}

		// Deep recursion: class depth > MAX_CLASS_DEPTH (64).
		{
			RawBufferBuilder builder;
			builder.write_valid_header(0);
			builder.u32(GDScriptBytecodeFormat::SECTION_STRING_TABLE);
			builder.u32(4);
			builder.utf8("res://deep.gd");
			builder.utf8("");
			builder.utf8("RefCounted");
			builder.utf8("Sub");
			builder.u32(GDScriptBytecodeFormat::SECTION_DEPENDENCIES);
			builder.u32(0);
			builder.u32(GDScriptBytecodeFormat::SECTION_SKELETON);
			const uint32_t depth = GDScriptBytecodeFormat::MAX_CLASS_DEPTH + 3;
			builder.u32(depth);
			for (uint32_t i = 0; i < depth; i++) {
				if (i > 0) {
					builder.u32(3); // subclass key "Sub"
				}
				builder.u32(0);
				builder.u32(1);
				builder.u32(1);
				builder.u32(1);
				builder.u32(2);
				builder.u8(0);
				builder.u8(GDScriptBytecodeFormat::BASE_CLASS_NATIVE_ONLY);
				builder.u32(i + 1 < depth ? 1u : 0u);
			}
			Ref<GDScript> target = memnew(GDScript);
			CHECK(GDScriptBytecodeLoader::load_skeleton(builder.get_bytes(), target, "res://deep.gd") == ERR_INVALID_DATA);
		}

		// Deep recursion: Variant depth > Variant::MAX_RECURSION_DEPTH.
		{
			RawBufferBuilder builder;
			builder.write_valid_header(0);
			builder.u32(GDScriptBytecodeFormat::SECTION_STRING_TABLE);
			builder.u32(4);
			builder.utf8("res://deepv.gd");
			builder.utf8("");
			builder.utf8("RefCounted");
			builder.utf8("C");
			builder.u32(GDScriptBytecodeFormat::SECTION_DEPENDENCIES);
			builder.u32(0);
			builder.u32(GDScriptBytecodeFormat::SECTION_SKELETON);
			builder.u32(1);
			builder.u32(0);
			builder.u32(1);
			builder.u32(1);
			builder.u32(1);
			builder.u32(2);
			builder.u8(0);
			builder.u8(GDScriptBytecodeFormat::BASE_CLASS_NATIVE_ONLY);
			builder.u32(0);
			builder.u32(GDScriptBytecodeFormat::SECTION_CLASS_BODIES);
			builder.u32(0);
			builder.u32(0);
			builder.u32(0);
			builder.u32(1);
			builder.u32(3);
			for (int d = 0; d < Variant::MAX_RECURSION_DEPTH + 4; d++) {
				builder.u8(GDScriptBytecodeFormat::TAG_ARRAY);
				builder.u8(0); // read_only
				builder.u32(Variant::NIL); // elem builtin
				builder.u32(1); // elem class_name ""
				builder.u8(0); // has_script
				builder.u32(1); // size 1
			}
			builder.u8(GDScriptBytecodeFormat::TAG_NULL_OBJECT);
			Ref<GDScript> target = memnew(GDScript);
			CHECK(GDScriptBytecodeLoader::load_full(builder.get_bytes(), target, "res://deepv.gd", nullptr) == ERR_INVALID_DATA);
		}

		// Illegal GDScriptDataType shapes on member_indices:
		// (a) VARIANT with non-NIL builtin_type
		// (b) BUILTIN with Variant::OBJECT
		// (c) BUILTIN with Variant::NIL on a member (only return_type allows void)
		// (d) ARRAY with elem_count == 2
		// (e) DICTIONARY with elem_count == 1
		// (f) NATIVE with builtin_type == INT
		auto build_illegal_type_buffer = [](uint8_t p_kind, uint32_t p_builtin, uint32_t p_native_str_idx, uint32_t p_elem_count) {
			RawBufferBuilder builder;
			builder.write_valid_header(0);
			builder.u32(GDScriptBytecodeFormat::SECTION_STRING_TABLE);
			builder.u32(4);
			builder.utf8("res://dt.gd");
			builder.utf8("");
			builder.utf8("RefCounted");
			builder.utf8("m");
			builder.u32(GDScriptBytecodeFormat::SECTION_DEPENDENCIES);
			builder.u32(0);
			builder.u32(GDScriptBytecodeFormat::SECTION_SKELETON);
			builder.u32(1);
			builder.u32(0);
			builder.u32(1);
			builder.u32(1);
			builder.u32(1);
			builder.u32(2);
			builder.u8(0);
			builder.u8(GDScriptBytecodeFormat::BASE_CLASS_NATIVE_ONLY);
			builder.u32(0);
			builder.u32(GDScriptBytecodeFormat::SECTION_CLASS_BODIES);
			builder.u32(1); // own_members
			builder.u32(3); // "m"
			builder.u32(1); // member_indices
			builder.u32(3); // name "m"
			builder.i32(0); // index
			builder.u32(1); // setter ""
			builder.u32(1); // getter ""
			// GDScriptDataType
			builder.u8(p_kind);
			builder.u32(p_builtin);
			builder.u32(p_native_str_idx);
			builder.u32(p_elem_count);
			return builder.get_bytes();
		};
		{
			Ref<GDScript> t = memnew(GDScript);
			CHECK(GDScriptBytecodeLoader::load_full(build_illegal_type_buffer(GDScriptDataType::VARIANT, Variant::INT, 1, 0), t, "res://dt.gd", nullptr) == ERR_INVALID_DATA);
			CHECK(GDScriptBytecodeLoader::load_full(build_illegal_type_buffer(GDScriptDataType::BUILTIN, Variant::OBJECT, 1, 0), t, "res://dt.gd", nullptr) == ERR_INVALID_DATA);
			CHECK(GDScriptBytecodeLoader::load_full(build_illegal_type_buffer(GDScriptDataType::BUILTIN, Variant::NIL, 1, 0), t, "res://dt.gd", nullptr) == ERR_INVALID_DATA);
			CHECK(GDScriptBytecodeLoader::load_full(build_illegal_type_buffer(GDScriptDataType::BUILTIN, Variant::ARRAY, 1, 2), t, "res://dt.gd", nullptr) == ERR_INVALID_DATA);
			CHECK(GDScriptBytecodeLoader::load_full(build_illegal_type_buffer(GDScriptDataType::BUILTIN, Variant::DICTIONARY, 1, 1), t, "res://dt.gd", nullptr) == ERR_INVALID_DATA);
			CHECK(GDScriptBytecodeLoader::load_full(build_illegal_type_buffer(GDScriptDataType::NATIVE, Variant::INT, 2, 0), t, "res://dt.gd", nullptr) == ERR_INVALID_DATA);
		}
	}

	TEST_CASE("Link-time verifier hardening: scalar ceilings, temp slots, vararg, default args, OPCODE_END, unused fixups, inherited MemberInfo, store global, operator cache") {
		Ref<GDScript> script = compile_source(R"(
extends RefCounted

class Base:
	var a: int = 1
	var b: String = "x"

class Derived extends Base:
	var c: float = 2.0

	func test_fn(p1: int, p2: int = 5) -> int:
		var tmp: int = p1 + p2
		var dyn = p1 + p2
		return tmp + dyn
)",
				"res://verifier_hardening.gd");

		const Vector<uint8_t> bytes = export_script_bytes(script);
		Ref<GDScript> loaded = load_script_bytes(bytes, "res://verifier_hardening.gd");
		Ref<GDScript> derived = loaded->get_subclasses()[StringName("Derived")];
		GDScriptFunction *fn = derived->get_member_functions()[StringName("test_fn")];
		REQUIRE(fn != nullptr);
		REQUIRE(GDScriptBytecodeVerifier::verify_class(derived.ptr()) == OK);

		// 1. INT32_MAX stack_size and instruction_args_size.
		{
			const int orig_stack = GDScriptBytecodeTestAccessor::mutable_stack_size(fn);
			GDScriptBytecodeTestAccessor::mutable_stack_size(fn) = INT32_MAX;
			CHECK(GDScriptBytecodeVerifier::verify_function(fn, derived.ptr()) != OK);
			GDScriptBytecodeTestAccessor::mutable_stack_size(fn) = orig_stack;

			const int orig_args = GDScriptBytecodeTestAccessor::mutable_instruction_args_size(fn);
			GDScriptBytecodeTestAccessor::mutable_instruction_args_size(fn) = INT32_MAX;
			CHECK(GDScriptBytecodeVerifier::verify_function(fn, derived.ptr()) != OK);
			GDScriptBytecodeTestAccessor::mutable_instruction_args_size(fn) = orig_args;
		}

		// 2. Duplicate, parameter-overlapping, out-of-range, and Variant::NIL temporary_slots.
		{
			TightLocalVector<Pair<int, Variant::Type>> orig_temps(GDScriptBytecodeTestAccessor::mutable_temporary_slots(fn));
			const int stack_size = GDScriptBytecodeTestAccessor::mutable_stack_size(fn);
			const int valid_slot = stack_size - 1;

			// Overlapping parameter slot (FIXED_ADDRESSES_MAX + 0).
			GDScriptBytecodeTestAccessor::mutable_temporary_slots(fn).clear();
			GDScriptBytecodeTestAccessor::mutable_temporary_slots(fn).push_back(Pair<int, Variant::Type>(GDScriptFunction::FIXED_ADDRESSES_MAX, Variant::INT));
			CHECK(GDScriptBytecodeVerifier::verify_function(fn, derived.ptr()) != OK);

			// Duplicate slot.
			GDScriptBytecodeTestAccessor::mutable_temporary_slots(fn).clear();
			GDScriptBytecodeTestAccessor::mutable_temporary_slots(fn).push_back(Pair<int, Variant::Type>(valid_slot, Variant::INT));
			GDScriptBytecodeTestAccessor::mutable_temporary_slots(fn).push_back(Pair<int, Variant::Type>(valid_slot, Variant::FLOAT));
			CHECK(GDScriptBytecodeVerifier::verify_function(fn, derived.ptr()) != OK);

			// Out-of-range slot.
			GDScriptBytecodeTestAccessor::mutable_temporary_slots(fn).clear();
			GDScriptBytecodeTestAccessor::mutable_temporary_slots(fn).push_back(Pair<int, Variant::Type>(stack_size, Variant::INT));
			CHECK(GDScriptBytecodeVerifier::verify_function(fn, derived.ptr()) != OK);

			// Variant::NIL (uninitializable).
			GDScriptBytecodeTestAccessor::mutable_temporary_slots(fn).clear();
			GDScriptBytecodeTestAccessor::mutable_temporary_slots(fn).push_back(Pair<int, Variant::Type>(valid_slot, Variant::NIL));
			CHECK(GDScriptBytecodeVerifier::verify_function(fn, derived.ptr()) != OK);

			GDScriptBytecodeTestAccessor::mutable_temporary_slots(fn) = orig_temps;
			REQUIRE(GDScriptBytecodeVerifier::verify_function(fn, derived.ptr()) == OK);
		}

		// 3. Invalid _vararg_index and METHOD_FLAG_VARARG mismatch.
		{
			const int orig_vararg = GDScriptBytecodeTestAccessor::mutable_vararg_index(fn);
			const uint32_t orig_flags = GDScriptBytecodeTestAccessor::mutable_method_info(fn).flags;

			GDScriptBytecodeTestAccessor::mutable_vararg_index(fn) = GDScriptFunction::FIXED_ADDRESSES_MAX; // wrong slot + flag mismatch
			CHECK(GDScriptBytecodeVerifier::verify_function(fn, derived.ptr()) != OK);

			GDScriptBytecodeTestAccessor::mutable_method_info(fn).flags |= METHOD_FLAG_VARARG;
			GDScriptBytecodeTestAccessor::mutable_vararg_index(fn) = GDScriptFunction::FIXED_ADDRESSES_MAX + fn->get_argument_count() + 1; // off-by-one
			CHECK(GDScriptBytecodeVerifier::verify_function(fn, derived.ptr()) != OK);

			GDScriptBytecodeTestAccessor::mutable_vararg_index(fn) = orig_vararg;
			GDScriptBytecodeTestAccessor::mutable_method_info(fn).flags = orig_flags;
			REQUIRE(GDScriptBytecodeVerifier::verify_function(fn, derived.ptr()) == OK);
		}

		// 4. Mismatched default_arguments / optional_arg_count / MethodInfo.default_arguments.
		{
			const int orig_def_count = GDScriptBytecodeTestAccessor::mutable_default_arg_count(fn);
			GDScriptBytecodeTestAccessor::mutable_default_arg_count(fn) = orig_def_count + 1;
			CHECK(GDScriptBytecodeVerifier::verify_function(fn, derived.ptr()) != OK);
			GDScriptBytecodeTestAccessor::mutable_default_arg_count(fn) = orig_def_count;

			// Default jump target landing mid-instruction.
			Vector<int> &defs = GDScriptBytecodeTestAccessor::mutable_default_arguments(fn);
			REQUIRE(defs.size() == 2);
			const int orig_target = defs[0];
			defs.write[0] = -1;
			CHECK(GDScriptBytecodeVerifier::verify_function(fn, derived.ptr()) != OK);
			defs.write[0] = orig_target;
			REQUIRE(GDScriptBytecodeVerifier::verify_function(fn, derived.ptr()) == OK);
		}

		// 5. Mid-stream OPCODE_END and missing terminal OPCODE_END.
		{
			Vector<int> &code = GDScriptBytecodeTestAccessor::mutable_code(fn);
			const int orig_last = code[code.size() - 1];
			REQUIRE(orig_last == GDScriptFunction::OPCODE_END);

			// Missing terminal OPCODE_END.
			code.write[code.size() - 1] = GDScriptFunction::OPCODE_BREAKPOINT;
			CHECK(GDScriptBytecodeVerifier::verify_function(fn, derived.ptr()) != OK);
			code.write[code.size() - 1] = orig_last;

			// Mid-stream OPCODE_END (append extra OPCODE_END after existing terminal OPCODE_END).
			code.push_back(GDScriptFunction::OPCODE_END);
			GDScriptBytecodeTestAccessor::mutable_code_size(fn) = code.size();
			GDScriptBytecodeTestAccessor::mutable_code_ptr(fn) = code.ptrw();
			CHECK(GDScriptBytecodeVerifier::verify_function(fn, derived.ptr()) != OK);
			code.resize(code.size() - 1);
			GDScriptBytecodeTestAccessor::mutable_code_size(fn) = code.size();
			GDScriptBytecodeTestAccessor::mutable_code_ptr(fn) = code.ptrw();
			REQUIRE(GDScriptBytecodeVerifier::verify_function(fn, derived.ptr()) == OK);
		}

		// 6. Unused fixup table entry rejected.
		{
			auto &ops = GDScriptBytecodeTestAccessor::mutable_operator_funcs(fn);
			const int orig_size = ops.size();
			REQUIRE(orig_size >= 1);
			ops.push_back(ops[0]);
			GDScriptBytecodeTestAccessor::mutable_operator_funcs_count(fn) = ops.size();
			GDScriptBytecodeTestAccessor::mutable_operator_funcs_ptr(fn) = ops.ptr();
			CHECK(GDScriptBytecodeVerifier::verify_function(fn, derived.ptr()) != OK);
			ops.resize(orig_size);
			GDScriptBytecodeTestAccessor::mutable_operator_funcs_count(fn) = orig_size;
			GDScriptBytecodeTestAccessor::mutable_operator_funcs_ptr(fn) = ops.ptr();
			REQUIRE(GDScriptBytecodeVerifier::verify_function(fn, derived.ptr()) == OK);
		}

		// 7. Non-zeroed OPCODE_OPERATOR inline-cache words rejected.
		{
			Ref<GDScript> dyn_script = compile_source("extends RefCounted\nfunc add(a, b):\n\treturn a + b\n", "res://dyn_op.gd");
			const Vector<uint8_t> dyn_bytes = export_script_bytes(dyn_script);
			Ref<GDScript> dyn_loaded = load_script_bytes(dyn_bytes, "res://dyn_op.gd");
			GDScriptFunction *add_fn = dyn_loaded->get_member_functions()[StringName("add")];
			Vector<int> &code = GDScriptBytecodeTestAccessor::mutable_code(add_fn);
			int op_ip = -1;
			for (int i = 0; i < code.size();) {
				if (code[i] == GDScriptFunction::OPCODE_OPERATOR) {
					op_ip = i;
					break;
				}
				const auto &layout = GDScriptFunction::get_opcode_layout(static_cast<GDScriptFunction::Opcode>(code[i]));
				i += layout.is_vararg ? (2 + code[i + 1] + layout.word_count) : layout.word_count;
			}
			REQUIRE(op_ip >= 0);
			for (int word = 5; word < 7 + GDScriptFunction::OPERATOR_INLINE_CACHE_PTR_WORDS; word++) {
				code.write[op_ip + word] = 1;
				CHECK(GDScriptBytecodeVerifier::verify_function(add_fn, dyn_loaded.ptr()) != OK);
				code.write[op_ip + word] = 0;
			}
			CHECK(GDScriptBytecodeVerifier::verify_function(add_fn, dyn_loaded.ptr()) == OK);
		}

		// 8. Unpatched / unrecorded OPCODE_STORE_GLOBAL rejected.
		{
			Ref<GDScript> store_script = compile_source("extends RefCounted\nfunc get_rc():\n\treturn RefCounted\n", "res://store_global_check.gd");
			const Vector<uint8_t> store_bytes = export_script_bytes(store_script);
			Ref<GDScript> store_loaded = load_script_bytes(store_bytes, "res://store_global_check.gd");
			GDScriptFunction *get_rc = store_loaded->get_member_functions()[StringName("get_rc")];
			// Passing an empty GlobalStoreOffsetsMap when OPCODE_STORE_GLOBAL is present must fail verification.
			// (Note: RefCounted may be stored via constant or OPCODE_STORE_GLOBAL; explicitly inject an OPCODE_STORE_GLOBAL check.)
			Vector<int> &code = GDScriptBytecodeTestAccessor::mutable_code(get_rc);
			Vector<int> patched_code;
			patched_code.push_back(GDScriptFunction::OPCODE_STORE_GLOBAL);
			patched_code.push_back(GDScriptFunction::ADDR_NIL);
			patched_code.push_back(0);
			patched_code.push_back(GDScriptFunction::OPCODE_END);
			Vector<int> saved_code = code;
			code = patched_code;
			GDScriptBytecodeTestAccessor::mutable_code_size(get_rc) = code.size();
			GDScriptBytecodeTestAccessor::mutable_code_ptr(get_rc) = code.ptrw();

			GDScriptBytecodeVerifier::GlobalStoreOffsetsMap empty_offsets;
			CHECK(GDScriptBytecodeVerifier::verify_function(get_rc, store_loaded.ptr(), &empty_offsets) != OK);

			GDScriptBytecodeVerifier::GlobalStoreOffsetsMap valid_offsets;
			valid_offsets[get_rc].push_back(2);
			CHECK(GDScriptBytecodeVerifier::verify_function(get_rc, store_loaded.ptr(), &valid_offsets) == OK);

			code = saved_code;
			GDScriptBytecodeTestAccessor::mutable_code_size(get_rc) = code.size();
			GDScriptBytecodeTestAccessor::mutable_code_ptr(get_rc) = code.ptrw();
		}

		// 9. Mismatched inherited MemberInfo rejected.
		{
			auto &derived_members = GDScriptBytecodeTestAccessor::mutable_member_indices(derived.ptr());
			REQUIRE(derived_members.has(StringName("a")));
			const StringName orig_setter = derived_members[StringName("a")].setter;
			derived_members[StringName("a")].setter = StringName("tampered_setter");
			CHECK(GDScriptBytecodeVerifier::verify_class(derived.ptr()) != OK);
			derived_members[StringName("a")].setter = orig_setter;

			const Variant::Type orig_prop_type = derived_members[StringName("a")].property_info.type;
			derived_members[StringName("a")].property_info.type = Variant::STRING;
			CHECK(GDScriptBytecodeVerifier::verify_class(derived.ptr()) != OK);
			derived_members[StringName("a")].property_info.type = orig_prop_type;

			REQUIRE(GDScriptBytecodeVerifier::verify_class(derived.ptr()) == OK);
		}
	}
}

} // namespace TestGDScriptBytecode

#endif // GDSCRIPT_BYTECODE_EXPORT_ENABLED
