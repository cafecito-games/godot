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
#include "core/io/compression.h"
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
	static void reverse_member_indices(GDScript *p_script) {
		HashMap<StringName, GDScript::MemberInfo> original(p_script->member_indices);
		Vector<StringName> keys;
		for (const KeyValue<StringName, GDScript::MemberInfo> &entry : original) {
			keys.push_back(entry.key);
		}
		p_script->member_indices.clear();
		for (int i = keys.size() - 1; i >= 0; i--) {
			p_script->member_indices.insert(keys[i], original[keys[i]]);
		}
	}
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
	static GDScriptFunction *get_static_initializer(const GDScript *p_script) { return p_script->static_initializer; }

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
	void write_valid_header(
			uint32_t p_script_flags = 0,
			uint8_t p_compression_mode = GDScriptBytecodeFormat::COMPRESSION_NONE,
			uint32_t p_uncompressed_payload_size = 0) {
		raw(GDScriptBytecodeFormat::MAGIC, 4);
		u32(GDScriptBytecodeFormat::FORMAT_VERSION);
		raw(GDSCRIPT_BYTECODE_ABI_ID, 16);
		u8(static_cast<uint8_t>(sizeof(real_t)));
		u32(static_cast<uint32_t>(GDScriptFunction::OPCODE_END));
		utf8(String(GODOT_VERSION_FULL_CONFIG));
		utf8(String(GODOT_VERSION_HASH));
		u32(p_script_flags);
		u8(p_compression_mode);
		u32(p_uncompressed_payload_size);
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
	// Walk Header + ScriptFlags + CompressionEnvelope to Section 0.
	size_t offset = 4 + 4 + 16 + 1 + 4; // magic + version + abi + real_t + opcode_end
	REQUIRE(offset + 4 <= static_cast<size_t>(p_buffer.size()));
	uint32_t diag_config_len = decode_uint32(p_buffer.ptr() + offset);
	offset += 4 + diag_config_len;
	REQUIRE(offset + 4 <= static_cast<size_t>(p_buffer.size()));
	uint32_t diag_hash_len = decode_uint32(p_buffer.ptr() + offset);
	offset += 4 + diag_hash_len;
	offset += 4; // ScriptFlags
	offset += 1 + 4; // compression_mode + uncompressed_payload_size

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
			encode_uint32(1u << 2, mutated.ptrw() + section0_offset - 9);
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
		CHECK(!loaded_constants[StringName("C_DEFAULT_RID")].operator ::RID().is_valid());

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
		var util_val: float = sin(typed_float)
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
		CHECK(double(res["util_val"]) == doctest::Approx(Math::sin(0.5)));
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

			GDScriptBytecodeTestAccessor::reverse_member_indices(script_reordered.ptr());

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
		GDScriptLanguage::get_singleton()->init();
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
	var neg_a: int = ~a
	var v: Vector3 = Vector3(x, x, x)
	v.x = 9.0
	var vx: float = v.x
	var s: String = "hi"
	var u: String = s.to_upper()
	var c: float = sin(x)
	var l: int = len(u)
	var pair = [func(n): return n + 1, func(n): return n + 2, func(n): return (func(m): return m * 2).call(n)]
	return { "sum": sum + neg_a - neg_a, "vx": vx, "c": c, "l": l, "p0": pair[0].call(1), "p1": pair[1].call(1), "p2": pair[2].call(3) }

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
		var shifted: int = ~p1
		var dyn = p1 + p2
		return tmp + shifted + dyn
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

		// 10. OPCODE_SET_STATIC_VARIABLE / OPCODE_GET_STATIC_VARIABLE target & bounds checks.
		{
			Ref<GDScript> static_script = compile_source(R"(
extends RefCounted
class Other:
	static var other_val: int = 10
static var own_val: int = 5
static func read_both() -> int:
	return own_val + Other.other_val
)",
					"res://static_var_verify.gd");
			const Vector<uint8_t> static_bytes = export_script_bytes(static_script);
			Ref<GDScript> static_loaded = load_script_bytes(static_bytes, "res://static_var_verify.gd");
			GDScriptFunction *static_init = GDScriptBytecodeTestAccessor::get_static_initializer(static_loaded.ptr());
			REQUIRE(static_init != nullptr);
			REQUIRE(GDScriptBytecodeVerifier::verify_function(static_init, static_loaded.ptr()) == OK);

			Vector<int> &init_code = GDScriptBytecodeTestAccessor::mutable_code(static_init);
			int own_set_ip = -1;
			for (int ip = 0; ip < init_code.size();) {
				const int raw_op = init_code[ip];
				if (raw_op == GDScriptFunction::OPCODE_SET_STATIC_VARIABLE && init_code[ip + 2] == GDScriptFunction::ADDR_CLASS) {
					own_set_ip = ip;
					break;
				}
				const auto &layout = GDScriptFunction::get_opcode_layout(static_cast<GDScriptFunction::Opcode>(raw_op));
				ip += layout.is_vararg ? (2 + init_code[ip + 1] + layout.word_count) : layout.word_count;
			}
			REQUIRE(own_set_ip >= 0);

			// Out-of-bounds static index on ADDR_CLASS rejected.
			if (own_set_ip >= 0) {
				init_code.write[own_set_ip + 3] = 1;
				CHECK(GDScriptBytecodeVerifier::verify_function(static_init, static_loaded.ptr()) != OK);
				init_code.write[own_set_ip + 3] = 0;
				REQUIRE(GDScriptBytecodeVerifier::verify_function(static_init, static_loaded.ptr()) == OK);
			}

			GDScriptFunction *read_both = static_loaded->get_member_functions()[StringName("read_both")];
			REQUIRE(read_both != nullptr);
			REQUIRE(GDScriptBytecodeVerifier::verify_function(read_both, static_loaded.ptr()) == OK);

			Vector<int> &code = GDScriptBytecodeTestAccessor::mutable_code(read_both);
			int other_get_ip = -1;
			for (int ip = 0; ip < code.size();) {
				const int raw_op = code[ip];
				if (raw_op == GDScriptFunction::OPCODE_GET_STATIC_VARIABLE) {
					other_get_ip = ip;
					break;
				}
				const auto &layout = GDScriptFunction::get_opcode_layout(static_cast<GDScriptFunction::Opcode>(raw_op));
				ip += layout.is_vararg ? (2 + code[ip + 1] + layout.word_count) : layout.word_count;
			}
			REQUIRE(other_get_ip >= 0);

			if (other_get_ip >= 0) {
				// Out-of-bounds static index on constant GDScript rejected.
				code.write[other_get_ip + 3] = 1;
				CHECK(GDScriptBytecodeVerifier::verify_function(read_both, static_loaded.ptr()) != OK);
				code.write[other_get_ip + 3] = 0;

				// Non-GDScript target address (ADDR_NIL) rejected.
				const int orig_class_addr = code[other_get_ip + 2];
				code.write[other_get_ip + 2] = GDScriptFunction::ADDR_NIL;
				CHECK(GDScriptBytecodeVerifier::verify_function(read_both, static_loaded.ptr()) != OK);
				code.write[other_get_ip + 2] = orig_class_addr;

				REQUIRE(GDScriptBytecodeVerifier::verify_function(read_both, static_loaded.ptr()) == OK);
			}
		}

		// 11. Instance-only opcodes and ADDR_TYPE_MEMBER rejected in static functions and non-self lambdas.
		{
			Ref<GDScript> instanceless_script = compile_source(R"(
extends RefCounted
var inst_member: int = 1
static func static_fn(x: int) -> int:
	var lam = func(y: int) -> int:
		return y + 1
	return lam.call(x)
)",
					"res://instanceless_verify.gd");
			const Vector<uint8_t> instanceless_bytes = export_script_bytes(instanceless_script);
			Ref<GDScript> instanceless_loaded = load_script_bytes(instanceless_bytes, "res://instanceless_verify.gd");
			GDScriptFunction *static_fn = instanceless_loaded->get_member_functions()[StringName("static_fn")];
			REQUIRE(static_fn != nullptr);
			REQUIRE(GDScriptBytecodeVerifier::verify_function(static_fn, instanceless_loaded.ptr()) == OK);

			// Inject ADDR_TYPE_MEMBER into static_fn -> rejected.
			{
				Vector<int> &code = GDScriptBytecodeTestAccessor::mutable_code(static_fn);
				Vector<int> saved_code = code;
				Vector<int> bad_code;
				bad_code.push_back(GDScriptFunction::OPCODE_ASSIGN_NULL);
				bad_code.push_back(0 | (GDScriptFunction::ADDR_TYPE_MEMBER << GDScriptFunction::ADDR_BITS));
				bad_code.push_back(GDScriptFunction::OPCODE_END);
				code = bad_code;
				GDScriptBytecodeTestAccessor::mutable_code_size(static_fn) = code.size();
				GDScriptBytecodeTestAccessor::mutable_code_ptr(static_fn) = code.ptrw();
				CHECK(GDScriptBytecodeVerifier::verify_function(static_fn, instanceless_loaded.ptr()) != OK);

				// Inject OPCODE_GET_MEMBER into static_fn -> rejected.
				Vector<int> bad_member_op;
				bad_member_op.push_back(GDScriptFunction::OPCODE_GET_MEMBER);
				bad_member_op.push_back(GDScriptFunction::ADDR_NIL);
				bad_member_op.push_back(0);
				bad_member_op.push_back(GDScriptFunction::OPCODE_END);
				code = bad_member_op;
				GDScriptBytecodeTestAccessor::mutable_code_size(static_fn) = code.size();
				GDScriptBytecodeTestAccessor::mutable_code_ptr(static_fn) = code.ptrw();
				CHECK(GDScriptBytecodeVerifier::verify_function(static_fn, instanceless_loaded.ptr()) != OK);

				code = saved_code;
				GDScriptBytecodeTestAccessor::mutable_code_size(static_fn) = code.size();
				GDScriptBytecodeTestAccessor::mutable_code_ptr(static_fn) = code.ptrw();
			}

			// Inject ADDR_TYPE_MEMBER into non-self lambda -> rejected.
			{
				const Vector<GDScriptFunction *> &lambdas = GDScriptBytecodeTestAccessor::mutable_lambdas(static_fn);
				REQUIRE(lambdas.size() == 1);
				GDScriptFunction *non_self_lambda = lambdas[0];
				Vector<int> &lambda_code = GDScriptBytecodeTestAccessor::mutable_code(non_self_lambda);
				Vector<int> saved_lambda_code = lambda_code;
				Vector<int> bad_lambda_code;
				bad_lambda_code.push_back(GDScriptFunction::OPCODE_ASSIGN_NULL);
				bad_lambda_code.push_back(0 | (GDScriptFunction::ADDR_TYPE_MEMBER << GDScriptFunction::ADDR_BITS));
				bad_lambda_code.push_back(GDScriptFunction::OPCODE_END);
				lambda_code = bad_lambda_code;
				GDScriptBytecodeTestAccessor::mutable_code_size(non_self_lambda) = lambda_code.size();
				GDScriptBytecodeTestAccessor::mutable_code_ptr(non_self_lambda) = lambda_code.ptrw();
				CHECK(GDScriptBytecodeVerifier::verify_function(static_fn, instanceless_loaded.ptr()) != OK);

				lambda_code = saved_lambda_code;
				GDScriptBytecodeTestAccessor::mutable_code_size(non_self_lambda) = lambda_code.size();
				GDScriptBytecodeTestAccessor::mutable_code_ptr(non_self_lambda) = lambda_code.ptrw();
			}
		}

		// 12. *_NATIVE and *_SCRIPT opcodes reject non-GDScriptNativeClass / non-Script type operands.
		{
			Ref<GDScript> cast_script = compile_source(R"(
extends RefCounted
class CustomType:
	pass
func cast_both(value) -> Array:
	var as_native = value as RefCounted
	var as_script = value as CustomType
	var is_script: bool = value is CustomType
	return [as_native, as_script, is_script]
)",
					"res://cast_verify.gd");
			const Vector<uint8_t> cast_bytes = export_script_bytes(cast_script);
			Ref<GDScript> cast_loaded = load_script_bytes(cast_bytes, "res://cast_verify.gd");
			GDScriptFunction *cast_fn = cast_loaded->get_member_functions()[StringName("cast_both")];
			REQUIRE(cast_fn != nullptr);
			REQUIRE(GDScriptBytecodeVerifier::verify_function(cast_fn, cast_loaded.ptr()) == OK);

			Vector<int> &code = GDScriptBytecodeTestAccessor::mutable_code(cast_fn);
			int cast_native_ip = -1;
			int cast_script_ip = -1;
			int test_script_ip = -1;
			for (int ip = 0; ip < code.size();) {
				const int raw_op = code[ip];
				if (raw_op == GDScriptFunction::OPCODE_CAST_TO_NATIVE) {
					cast_native_ip = ip;
				} else if (raw_op == GDScriptFunction::OPCODE_CAST_TO_SCRIPT) {
					cast_script_ip = ip;
				} else if (raw_op == GDScriptFunction::OPCODE_TYPE_TEST_SCRIPT) {
					test_script_ip = ip;
				}
				const auto &layout = GDScriptFunction::get_opcode_layout(static_cast<GDScriptFunction::Opcode>(raw_op));
				ip += layout.is_vararg ? (2 + code[ip + 1] + layout.word_count) : layout.word_count;
			}
			REQUIRE(cast_native_ip >= 0);
			REQUIRE(cast_script_ip >= 0);
			REQUIRE(test_script_ip >= 0);

			// Replace OPCODE_CAST_TO_NATIVE type operand with ADDR_NIL and with a Script constant -> both rejected.
			const int orig_native_operand = code[cast_native_ip + 3];
			const int orig_script_operand = code[cast_script_ip + 3];
			code.write[cast_native_ip + 3] = GDScriptFunction::ADDR_NIL;
			CHECK(GDScriptBytecodeVerifier::verify_function(cast_fn, cast_loaded.ptr()) != OK);
			code.write[cast_native_ip + 3] = orig_script_operand;
			CHECK(GDScriptBytecodeVerifier::verify_function(cast_fn, cast_loaded.ptr()) != OK);
			code.write[cast_native_ip + 3] = orig_native_operand;

			// Replace OPCODE_CAST_TO_SCRIPT and OPCODE_TYPE_TEST_SCRIPT type operands with a GDScriptNativeClass constant -> rejected.
			code.write[cast_script_ip + 3] = orig_native_operand;
			CHECK(GDScriptBytecodeVerifier::verify_function(cast_fn, cast_loaded.ptr()) != OK);
			code.write[cast_script_ip + 3] = orig_script_operand;

			const int orig_test_operand = code[test_script_ip + 3];
			code.write[test_script_ip + 3] = orig_native_operand;
			CHECK(GDScriptBytecodeVerifier::verify_function(cast_fn, cast_loaded.ptr()) != OK);
			code.write[test_script_ip + 3] = orig_test_operand;

			REQUIRE(GDScriptBytecodeVerifier::verify_function(cast_fn, cast_loaded.ptr()) == OK);
		}

		// 13. Cyclic local base classes fail cleanly and clear base references without leaking Ref<GDScript> cycles.
		{
			RawBufferBuilder builder;
			builder.write_valid_header(0);
			// String table: 0="res://cycle.gd", 1="", 2="RefCounted", 3="SubA", 4="SubB"
			builder.u32(GDScriptBytecodeFormat::SECTION_STRING_TABLE);
			builder.u32(5);
			builder.utf8("res://cycle.gd");
			builder.utf8("");
			builder.utf8("RefCounted");
			builder.utf8("SubA");
			builder.utf8("SubB");
			builder.u32(GDScriptBytecodeFormat::SECTION_DEPENDENCIES);
			builder.u32(0);
			// Skeleton: 3 classes (0=root, 1=SubA extends 2, 2=SubB extends 1).
			builder.u32(GDScriptBytecodeFormat::SECTION_SKELETON);
			builder.u32(3);
			// Root (index 0)
			builder.u32(0);
			builder.u32(1);
			builder.u32(1);
			builder.u32(1);
			builder.u32(2);
			builder.u8(0);
			builder.u8(GDScriptBytecodeFormat::BASE_CLASS_NATIVE_ONLY);
			builder.u32(2); // 2 subclasses
			// SubA (index 1, base = local 2)
			builder.u32(3); // key "SubA"
			builder.u32(3); // fqcn
			builder.u32(3); // local_name
			builder.u32(1); // global_name
			builder.u32(1); // icon
			builder.u32(2); // native
			builder.u8(0);
			builder.u8(GDScriptBytecodeFormat::BASE_CLASS_LOCAL);
			builder.u32(2);
			builder.u32(0); // 0 subclasses
			// SubB (index 2, base = local 1)
			builder.u32(4); // key "SubB"
			builder.u32(4); // fqcn
			builder.u32(4); // local_name
			builder.u32(1); // global_name
			builder.u32(1); // icon
			builder.u32(2); // native
			builder.u8(0);
			builder.u8(GDScriptBytecodeFormat::BASE_CLASS_LOCAL);
			builder.u32(1);
			builder.u32(0); // 0 subclasses

			Ref<GDScript> cyclic_target = memnew(GDScript);
			String cycle_error;
			CHECK(GDScriptBytecodeLoader::load_skeleton(builder.get_bytes(), cyclic_target, "res://cycle.gd", nullptr, &cycle_error) == ERR_INVALID_DATA);
			CHECK(cycle_error.contains("Cyclic base class"));
			CHECK(cyclic_target->get_base().is_null());
			for (const KeyValue<StringName, Ref<GDScript>> &subclass_entry : cyclic_target->get_subclasses()) {
				CHECK(subclass_entry.value->get_base().is_null());
			}
		}
	}

	TEST_CASE("Zstd compression round-trip, determinism, zero-source-leak, and hostile envelope bounds") {
		Ref<GDScript> external_dep = compile_source("extends RefCounted\nconst EXT_VALUE: int = 19\n", "res://zstd_ext_dep.gd");
		Ref<GDScript> script = compile_source(R"(
extends RefCounted

# SECRET_COMMENT_ZSTD_NEVER_LEAK
const TAG_LABEL: String = "zstd_roundtrip_payload"
const NUMBERS = [10, 20, 30, 40, 50, 60, 70, 80]

class Helper extends RefCounted:
	var scale: int = 3
	func compute(input_value: int) -> int:
		var secret_local_zstd_var: int = input_value * scale
		return secret_local_zstd_var + 7

func evaluate(base_value: int) -> Dictionary:
	var helper := Helper.new()
	var total: int = helper.compute(base_value)
	for entry in NUMBERS:
		total += entry
	return {
		"label": TAG_LABEL,
		"total": total,
	}
)",
				"res://zstd_roundtrip.gd");
		GDScriptBytecodeTestAccessor::mutable_script_constants(script.ptr())[StringName("EXT_DEP")] = external_dep;

		GDScriptBytecodeExporter::Options uncompressed_options;
		uncompressed_options.release_profile = true;
		uncompressed_options.emit_line_opcodes = false;
		uncompressed_options.compression_mode = GDScriptBytecodeFormat::COMPRESSION_NONE;
		const Vector<uint8_t> uncompressed_bytes = export_script_bytes(script, uncompressed_options);

		GDScriptBytecodeExporter::Options compressed_options = uncompressed_options;
		compressed_options.compression_mode = GDScriptBytecodeFormat::COMPRESSION_ZSTD;
		const Vector<uint8_t> compressed_bytes_a = export_script_bytes(script, compressed_options);
		const Vector<uint8_t> compressed_bytes_b = export_script_bytes(script, compressed_options);

		// Deterministic output and meaningful compression savings.
		CHECK(compressed_bytes_a == compressed_bytes_b);
		CHECK(compressed_bytes_a.size() < uncompressed_bytes.size());

		// Header check succeeds without decompressing and reports compression metadata.
		GDScriptBytecodeLoader::HeaderInfo header;
		String error_message;
		REQUIRE(GDScriptBytecodeLoader::check_header(compressed_bytes_a, &header, &error_message) == OK);
		CHECK(header.compression_mode == GDScriptBytecodeFormat::COMPRESSION_ZSTD);
		const size_t payload_offset = find_section_offset(uncompressed_bytes, GDScriptBytecodeFormat::SECTION_STRING_TABLE);
		CHECK(header.uncompressed_payload_size == static_cast<uint32_t>(uncompressed_bytes.size() - payload_offset));

		// Decompressed payload preserves release-profile zero-source-leak guarantees.
		Vector<uint8_t> decompressed_payload;
		decompressed_payload.resize(static_cast<int>(header.uncompressed_payload_size));
		const int64_t raw_decompressed_size = Compression::decompress(
				decompressed_payload.ptrw(),
				static_cast<int64_t>(header.uncompressed_payload_size),
				compressed_bytes_a.ptr() + payload_offset,
				static_cast<int64_t>(compressed_bytes_a.size() - payload_offset),
				Compression::MODE_ZSTD);
		REQUIRE(raw_decompressed_size == static_cast<int64_t>(header.uncompressed_payload_size));
		CHECK_FALSE(buffer_contains_ascii(decompressed_payload, "SECRET_COMMENT_ZSTD_NEVER_LEAK"));
		CHECK_FALSE(buffer_contains_ascii(decompressed_payload, "secret_local_zstd_var"));

		// Dependency scan, skeleton load, and full load + execution all work on compressed bytecode.
		Vector<String> dependencies;
		REQUIRE(GDScriptBytecodeLoader::read_dependencies(compressed_bytes_a, dependencies, &error_message) == OK);
		REQUIRE(dependencies.size() == 1);
		CHECK(dependencies[0] == "res://zstd_ext_dep.gd");

		Ref<GDScript> skeleton_script = memnew(GDScript);
		REQUIRE(GDScriptBytecodeLoader::load_skeleton(compressed_bytes_a, skeleton_script, "res://zstd_roundtrip.gd", nullptr, &error_message) == OK);
		CHECK(skeleton_script->get_subclasses().has(StringName("Helper")));

		StubExternalResolver resolver;
		resolver.scripts["res://zstd_ext_dep.gd"] = external_dep;
		Ref<GDScript> loaded = load_script_bytes(compressed_bytes_a, "res://zstd_roundtrip.gd", &resolver);
		Ref<RefCounted> instance = memnew(RefCounted);
		instance->set_script(loaded);
		Variant result_variant = instance->call(StringName("evaluate"), 10);
		REQUIRE(result_variant.get_type() == Variant::DICTIONARY);
		Dictionary result = result_variant;
		CHECK(String(result["label"]) == "zstd_roundtrip_payload");
		CHECK(int(result["total"]) == (10 * 3 + 7 + 360));

		// Exporter rejects unknown compression mode.
		{
			GDScriptBytecodeExporter::Options invalid_options;
			invalid_options.compression_mode = GDScriptBytecodeFormat::COMPRESSION_MAX;
			Vector<uint8_t> ignored_output;
			String export_error;
			CHECK(GDScriptBytecodeExporter::serialize(script, ignored_output, invalid_options, &export_error) == ERR_INVALID_PARAMETER);
			CHECK(ignored_output.is_empty());
		}

		// Hostile compression envelope tests.
		{
			// Unknown compression_mode byte.
			RawBufferBuilder builder;
			builder.write_valid_header(0, GDScriptBytecodeFormat::COMPRESSION_MAX, 16);
			builder.u32(0);
			CHECK(GDScriptBytecodeLoader::check_header(builder.get_bytes()) == ERR_INVALID_DATA);
		}
		{
			// Non-zero uncompressed_payload_size with COMPRESSION_NONE.
			RawBufferBuilder builder;
			builder.write_valid_header(0, GDScriptBytecodeFormat::COMPRESSION_NONE, 16);
			builder.u32(0);
			CHECK(GDScriptBytecodeLoader::check_header(builder.get_bytes()) == ERR_INVALID_DATA);
		}
		{
			// Zero uncompressed_payload_size with COMPRESSION_ZSTD.
			RawBufferBuilder builder;
			builder.write_valid_header(0, GDScriptBytecodeFormat::COMPRESSION_ZSTD, 0);
			builder.u32(0x12345678u);
			CHECK(GDScriptBytecodeLoader::check_header(builder.get_bytes()) == ERR_INVALID_DATA);
		}
		{
			// Empty compressed payload with COMPRESSION_ZSTD.
			RawBufferBuilder builder;
			builder.write_valid_header(0, GDScriptBytecodeFormat::COMPRESSION_ZSTD, 64);
			CHECK(GDScriptBytecodeLoader::check_header(builder.get_bytes()) == ERR_FILE_EOF);
		}
		{
			// Declared uncompressed_payload_size exceeding MAX_UNCOMPRESSED_PAYLOAD_BYTES.
			RawBufferBuilder builder;
			builder.write_valid_header(0, GDScriptBytecodeFormat::COMPRESSION_ZSTD, GDScriptBytecodeFormat::MAX_UNCOMPRESSED_PAYLOAD_BYTES + 1u);
			builder.u32(0x12345678u);
			CHECK(GDScriptBytecodeLoader::check_header(builder.get_bytes()) == ERR_INVALID_DATA);
		}
		{
			// Zip-bomb ratio rejection (declared uncompressed size > MAX(MIN_DECOMPRESSION_ALLOWANCE, compressed_size * MAX_DECOMPRESSION_RATIO)).
			RawBufferBuilder builder;
			builder.write_valid_header(0, GDScriptBytecodeFormat::COMPRESSION_ZSTD, static_cast<uint32_t>(GDScriptBytecodeFormat::MIN_DECOMPRESSION_ALLOWANCE + 1u));
			builder.u32(0x12345678u);
			CHECK(GDScriptBytecodeLoader::check_header(builder.get_bytes()) == ERR_INVALID_DATA);
		}
		{
			// Corrupted zstd payload bytes.
			Vector<uint8_t> corrupted = compressed_bytes_a;
			corrupted.write[payload_offset] ^= 0xFF;
			corrupted.write[payload_offset + 1] ^= 0xAA;
			Ref<GDScript> target = memnew(GDScript);
			CHECK(GDScriptBytecodeLoader::load_full(corrupted, target, "res://zstd_corrupt.gd", &resolver) == ERR_FILE_CORRUPT);
		}
		{
			// Truncated zstd payload.
			Vector<uint8_t> truncated = compressed_bytes_a.slice(0, compressed_bytes_a.size() - 4);
			Ref<GDScript> target = memnew(GDScript);
			CHECK(GDScriptBytecodeLoader::load_full(truncated, target, "res://zstd_trunc.gd", &resolver) == ERR_FILE_CORRUPT);
		}
		{
			// Declared uncompressed_payload_size mismatch (larger than actual decompressed output).
			Vector<uint8_t> mismatched = compressed_bytes_a;
			encode_uint32(header.uncompressed_payload_size + 16u, mismatched.ptrw() + payload_offset - 4);
			Ref<GDScript> target = memnew(GDScript);
			CHECK(GDScriptBytecodeLoader::load_full(mismatched, target, "res://zstd_size_mismatch.gd", &resolver) == ERR_FILE_CORRUPT);
		}
		{
			// Trailing garbage bytes appended after a valid zstd frame.
			Vector<uint8_t> with_trailing = compressed_bytes_a;
			with_trailing.push_back(0xDE);
			with_trailing.push_back(0xAD);
			Ref<GDScript> target = memnew(GDScript);
			CHECK(GDScriptBytecodeLoader::load_full(with_trailing, target, "res://zstd_trailing.gd", &resolver) == ERR_FILE_CORRUPT);
		}
	}

	TEST_CASE("Inline scalar/Vector2 opcodes, fused compare-jumps, peephole assign folding, and call/member inline caches round-trip cleanly") {
		Ref<GDScript> script = compile_source(R"(
extends RefCounted

class ChainNode extends RefCounted:
	var value: int = 0
	var next_node: ChainNode = null
	var _backed: int = 0
	var prop: int:
		get:
			return _backed
		set(new_val):
			_backed = new_val * 2

	func eval(x: int) -> int:
		return x + value

class ChainDouble extends ChainNode:
	func eval(x: int) -> int:
		return x * 2 + value

class ChainTriple extends ChainNode:
	func eval(x: int) -> int:
		return x * 3 + value

class NativeHolder extends Node2D:
	func update_self_props() -> Vector2:
		position = Vector2(3.0, 4.0)
		rotation = 0.5
		position += Vector2(1.0, 2.0)
		return Vector2(position.x + rotation, position.y)

class FreeOverrideNode extends Node:
	static var intercepted_count: int = 0

	@warning_ignore("native_method_override")
	func free() -> void:
		intercepted_count += 1

func destroy_untyped(target) -> bool:
	target.free()
	return is_instance_valid(target)

func run_all(limit: int, bonus: int = 2 + 3) -> Dictionary:
	# Exercises inline int/float/Vector2 ops, fused compare-and-jump, and redundant-assign folding.
	var total: int = 0
	var index: int = 0
	while index < limit:
		var masked: int = (index * 3 + 1) & 15
		if (masked ^ 1) != 0 and masked <= 12:
			total = total + masked
		index += 1

	var flt: float = 1.5
	var step: float = 0.25
	var f_acc: float = 0.0
	for i: int in range(6):
		flt = (flt + step) * 1.1 - 0.05
		if flt > 2.0:
			f_acc += flt / 2.0
		else:
			f_acc += flt

	var vec_a := Vector2(1.0, 2.0)
	var vec_b := Vector2(0.5, -0.25)
	vec_a = (vec_a + vec_b) * 2.0 - vec_b * Vector2(2.0, 4.0)

	# Exercises polymorphic call cache, direct member cache, accessor cache, and src == dst aliasing.
	var n1 := ChainNode.new()
	n1.value = 10
	n1.prop = 7
	var n2 := ChainDouble.new()
	n2.value = 20
	n1.next_node = n2
	var n3 := ChainTriple.new()
	n3.value = 30
	n2.next_node = n3

	var poly: Array[ChainNode] = [n1, n2, n3]
	var poly_sum: int = 0
	for item: ChainNode in poly:
		poly_sum += item.eval(4)

	var cursor: ChainNode = n1
	var chain_sum: int = 0
	while cursor != null:
		chain_sum += cursor.value
		cursor = cursor.next_node

	# Exercises self native property validated MethodBind + cross-object native property member cache.
	var holder := NativeHolder.new()
	var self_vec: Vector2 = holder.update_self_props()
	var plain := Node2D.new()
	plain.position = Vector2(7.0, 8.0)
	plain.rotation = 1.25
	var plain_sum: float = plain.position.x + plain.position.y + plain.rotation
	holder.free()
	plain.free()

	# Verify call inline cache never lets a script-defined free() bypass Object::callp deletion.
	FreeOverrideNode.intercepted_count = 0
	var victim := FreeOverrideNode.new()
	var victim_still_valid: bool = destroy_untyped(victim)
	var free_intercepted: int = FreeOverrideNode.intercepted_count

	# Exercises shifts, negate, Vector2/Vector2i properties, Array/Vector2 builtins, utilities,
	# integer JUMP_IF fusion (or), native MethodBind PIC, and type-test/cast caches.
	var shift_val: int = ((limit << 2) >> 1) + (-limit)
	var tile := Vector2i(3, 4)
	tile.x = tile.x + 2
	tile.y = tile.y - 1
	var tile_sum: Vector2i = tile + Vector2i(1, 2) - Vector2i(0, 1)
	var tile_eq_ok: bool = (tile_sum == Vector2i(6, 4)) and (tile_sum != Vector2i(0, 0))
	var or_branch_hits: int = 0
	for k: int in range(6):
		if k < 2 or k >= 5:
			or_branch_hits += 1

	var util_arr: Array[int] = []
	var arr_was_empty: bool = util_arr.is_empty()
	util_arr.append(absi(-7))
	util_arr.append(mini(9, 4))
	util_arr.append(maxi(3, 8))
	util_arr.append(clampi(15, 0, 10))
	var arr_len: int = util_arr.size()
	var f_util: float = absf(-2.5) + minf(3.0, 1.5) + maxf(0.5, 2.0) + clampf(5.0, 0.0, 3.0) + lerpf(1.0, 5.0, 0.25)
	var v_dot_len: float = vec_a.length_squared() + vec_a.dot(vec_b)
	vec_a.x = -vec_a.x
	vec_a.y = vec_a.y + 1.0
	var f_eq_ok: bool = (f_util == 11.0) and (f_util != 0.0)

	var dyn_node: Object = Node2D.new()
	dyn_node.set_position(Vector2(2.0, 3.0))
	var dyn_pos: Vector2 = dyn_node.get_position()
	var is_node2d: bool = dyn_node is Node2D
	var is_node: bool = dyn_node is Node
	var is_ctrl: bool = dyn_node is Control
	var cast_ok: bool = (dyn_node as Node2D) != null and (dyn_node as Control) == null
	dyn_node.free()

	return {
		"total": total + bonus,
		"f_acc": int(round(f_acc * 100.0)),
		"vec_x": int(round(vec_a.x * 100.0)),
		"vec_y": int(round(vec_a.y * 100.0)),
		"prop": n1.prop,
		"poly_sum": poly_sum,
		"chain_sum": chain_sum,
		"self_x": int(round(self_vec.x * 100.0)),
		"self_y": int(round(self_vec.y * 100.0)),
		"plain_sum": int(round(plain_sum * 100.0)),
		"victim_still_valid": victim_still_valid,
		"free_intercepted": free_intercepted,
		"shift_val": shift_val,
		"tile_eq_ok": tile_eq_ok,
		"or_branch_hits": or_branch_hits,
		"arr_was_empty": arr_was_empty,
		"arr_len": arr_len,
		"f_eq_ok": f_eq_ok,
		"v_dot_len": int(round(v_dot_len * 100.0)),
		"dyn_pos_sum": int(round((dyn_pos.x + dyn_pos.y) * 100.0)),
		"type_flags": (1 if is_node2d else 0) + (2 if is_node else 0) + (4 if not is_ctrl else 0) + (8 if cast_ok else 0),
	}
)",
				"res://perf_opts_roundtrip.gd");

		// Verify fused jump opcodes were actually emitted in run_all.
		GDScriptFunction *orig_fn = script->get_member_functions()[StringName("run_all")];
		REQUIRE(orig_fn != nullptr);
		bool saw_fused_jump = false;
		bool saw_inline_int = false;
		const Vector<int> &orig_code = GDScriptBytecodeTestAccessor::mutable_code(orig_fn);
		for (int ip = 0; ip < orig_code.size();) {
			const auto opcode = static_cast<GDScriptFunction::Opcode>(orig_code[ip]);
			if (opcode == GDScriptFunction::OPCODE_JUMP_IF_NOT_INT_LT || opcode == GDScriptFunction::OPCODE_JUMP_IF_NOT_FLOAT_GT) {
				saw_fused_jump = true;
			}
			if (opcode == GDScriptFunction::OPCODE_INT_ADD || opcode == GDScriptFunction::OPCODE_INT_MUL) {
				saw_inline_int = true;
			}
			const auto &layout = GDScriptFunction::get_opcode_layout(opcode);
			ip += layout.is_vararg ? (2 + orig_code[ip + 1] + layout.word_count) : layout.word_count;
		}
		CHECK(saw_fused_jump);
		CHECK(saw_inline_int);

		const Vector<uint8_t> bytes = export_script_bytes(script);
		Ref<GDScript> loaded = load_script_bytes(bytes, "res://perf_opts_roundtrip.gd");
		REQUIRE(GDScriptBytecodeVerifier::verify_class(loaded.ptr()) == OK);

		Ref<RefCounted> orig_inst = memnew(RefCounted);
		orig_inst->set_script(script);
		Ref<RefCounted> loaded_inst = memnew(RefCounted);
		loaded_inst->set_script(loaded);
		REQUIRE(orig_inst->get_script_instance() != nullptr);
		REQUIRE(loaded_inst->get_script_instance() != nullptr);

		// Execute multiple times to exercise cold + warm inline caches and default arguments.
		for (int repeat = 0; repeat < 3; repeat++) {
			Dictionary orig_res = orig_inst->call("run_all", 10);
			Dictionary loaded_res = loaded_inst->call("run_all", 10);
			CHECK(orig_res == loaded_res);
			CHECK(int(loaded_res["prop"]) == 14);
			CHECK(int(loaded_res["poly_sum"]) == (14 + 28 + 42));
			CHECK(int(loaded_res["chain_sum"]) == 60);
			CHECK(int(loaded_res["self_x"]) == 450);
			CHECK(int(loaded_res["self_y"]) == 600);
			CHECK(int(loaded_res["plain_sum"]) == 1625);
			CHECK(bool(loaded_res["victim_still_valid"]) == false);
			CHECK(int(loaded_res["free_intercepted"]) == 0);
			CHECK(int(loaded_res["shift_val"]) == 10);
			CHECK(bool(loaded_res["tile_eq_ok"]) == true);
			CHECK(int(loaded_res["or_branch_hits"]) == 3);
			CHECK(bool(loaded_res["arr_was_empty"]) == true);
			CHECK(int(loaded_res["arr_len"]) == 4);
			CHECK(bool(loaded_res["f_eq_ok"]) == true);
			CHECK(int(loaded_res["dyn_pos_sum"]) == 500);
			CHECK(int(loaded_res["type_flags"]) == 15);
		}

#ifdef DEBUG_ENABLED
		// Verify DEBUG_ENABLED native calls still acquire OBJ_DEBUG_LOCK so callbacks cannot free the receiver mid-call.
		{
			Ref<GDScript> lock_script = compile_source(R"(
extends RefCounted

class SelfFreeObject extends Object:
	var notification_count: int = 0

	func _notification(what: int) -> void:
		if what == 12345:
			notification_count += 1
			free()

func exercise_locked_native_call() -> Dictionary:
	var target: Variant = SelfFreeObject.new()
	target.notification(12345, false)
	target.notification(12345, false)
	var still_valid: bool = is_instance_valid(target)
	var notification_count: int = target.notification_count if still_valid else 0
	if still_valid:
		target.free()
	return { "still_valid": still_valid, "notification_count": notification_count }
)",
					"res://debug_lock_native_call.gd");
			Ref<RefCounted> lock_inst = memnew(RefCounted);
			lock_inst->set_script(lock_script);
			ERR_PRINT_OFF;
			Dictionary lock_res = lock_inst->call("exercise_locked_native_call");
			ERR_PRINT_ON;
			CHECK(bool(lock_res["still_valid"]) == true);
			CHECK(int(lock_res["notification_count"]) == 2);
		}
#endif
	}

	TEST_CASE("Inline VM call frames preserve recursion, refcounting, getters/setters, void returns, re-entrancy, and call stacks") {
		Ref<GDScript> script = compile_source(R"(
extends RefCounted

class Box extends RefCounted:
	var _val: int = 0
	var scaled: int:
		get:
			return _val * 3
		set(new_value):
			_val = new_value + 1
	var next_box: Box = null

	func _init(initial: int = 0) -> void:
		_val = initial

	func add_and_clear_owner(owner_holder: Array, delta: int) -> int:
		# Drops the caller's only external reference to `self` mid-call; self must stay alive until return.
		owner_holder.clear()
		var local_str: String = "v:" + str(_val + delta)
		return local_str.length() + _val + delta

	func get_self_via_getter() -> Box:
		return next_box

	func get_scaled_plus(offset: int = 5) -> int:
		return scaled + offset

class NodeTarget extends Node:
	var count: int = 0

	func bump(amount: int) -> int:
		count += amount
		return count

func fib(n: int) -> int:
	if n <= 1:
		return n
	return fib(n - 1) + fib(n - 2)

func implicit_nil_return(flag: bool):
	if flag:
		return 1

func strict_float_return(x: int) -> float:
	return x

func reentrant_inner(v: int) -> int:
	return fib(v) + 1

func reentrant_outer(v: int) -> int:
	# Exercises GDScript -> Object::call -> GDScript -> inline VM frames.
	return int(call("reentrant_inner", v))

func stack_depth_leaf() -> int:
	return get_stack().size()

func stack_depth_mid() -> int:
	return stack_depth_leaf()

func run_frame_tests() -> Dictionary:
	var fib_12: int = fib(12)

	# Implicit OPCODE_END return must overwrite a non-null destination slot cleanly.
	var slot_val: Variant = "non_null_before"
	slot_val = implicit_nil_return(false)
	var void_became_nil: bool = (slot_val == null)

	# Strict int -> float return conversion across inline frame.
	var flt_ret: float = strict_float_return(7)

	# Property setter + getter inline frames, including default args and src == dst aliasing.
	var b1 := Box.new(2)
	b1.scaled = 4
	var b1_scaled: int = b1.scaled
	var b1_default: int = b1.get_scaled_plus()
	var b2 := Box.new(9)
	b1.next_box = b2
	var alias_ref: Variant = b1
	alias_ref = alias_ref.get_self_via_getter()
	var alias_val: int = alias_ref.scaled

	# Cross-instance RefCounted call where caller's sole reference is cleared mid-call.
	var holder: Array = [Box.new(10)]
	var survivor_res: int = holder[0].add_and_clear_owner(holder, 5)
	var holder_empty: bool = holder.is_empty()

	# Non-RefCounted Node receiver inline call.
	var node := NodeTarget.new()
	var node_sum: int = node.bump(3) + node.bump(4)
	node.free()

	# Re-entrant C++ -> GDScript -> inline GDScript call.
	var reentrant_val: int = reentrant_outer(8)

	# Call stack depth increases across inline frames when stack tracking is active.
	var depth_root: int = get_stack().size()
	var depth_nested: int = stack_depth_mid()

	return {
		"fib_12": fib_12,
		"void_became_nil": void_became_nil,
		"flt_ret": int(flt_ret),
		"b1_scaled": b1_scaled,
		"b1_default": b1_default,
		"alias_val": alias_val,
		"survivor_res": survivor_res,
		"holder_empty": holder_empty,
		"node_sum": node_sum,
		"reentrant_val": reentrant_val,
		"depth_delta": depth_nested - depth_root,
	}
)",
				"res://tier2a_inline_frames.gd");

		const Vector<uint8_t> bytes = export_script_bytes(script);
		Ref<GDScript> loaded = load_script_bytes(bytes, "res://tier2a_inline_frames.gd");
		REQUIRE(GDScriptBytecodeVerifier::verify_class(loaded.ptr()) == OK);

		Ref<RefCounted> orig_inst = memnew(RefCounted);
		orig_inst->set_script(script);
		Ref<RefCounted> loaded_inst = memnew(RefCounted);
		loaded_inst->set_script(loaded);

		for (int repeat = 0; repeat < 3; repeat++) {
			Dictionary orig_res = orig_inst->call("run_frame_tests");
			Dictionary loaded_res = loaded_inst->call("run_frame_tests");
			CHECK(orig_res == loaded_res);
			CHECK(int(loaded_res["fib_12"]) == 144);
			CHECK(bool(loaded_res["void_became_nil"]) == true);
			CHECK(int(loaded_res["flt_ret"]) == 7);
			CHECK(int(loaded_res["b1_scaled"]) == 15);
			CHECK(int(loaded_res["b1_default"]) == 20);
			CHECK(int(loaded_res["alias_val"]) == 27);
			CHECK(int(loaded_res["survivor_res"]) == (4 + 10 + 5));
			CHECK(bool(loaded_res["holder_empty"]) == true);
			CHECK(int(loaded_res["node_sum"]) == (3 + 7));
			CHECK(int(loaded_res["reentrant_val"]) == (21 + 1));
#ifdef DEBUG_ENABLED
			CHECK(int(loaded_res["depth_delta"]) == 2);
#endif
		}
	}

	TEST_CASE("Container fast paths: keyed/indexed ops, literal construction, aliasing, String/StringName equivalence, and iteration survive .gdb round-trip") {
		Ref<GDScript> script = compile_source(R"(
extends RefCounted

func run_container_tests() -> Dictionary:
	# Untyped dictionary: String vs StringName equivalence, runtime duplicate key overwrite, Vector2i, bool, and null keys.
	var duplicate_key: String = "alpha"
	var untyped_dict: Variant = { "alpha": 10, "beta": 20, duplicate_key: 30, Vector2i(2, -3): 44, true: 55, null: 66 }
	var string_name_hit: int = untyped_dict[&"alpha"]
	untyped_dict[&"alpha"] = 35
	var string_after_update: int = untyped_dict["alpha"]
	var has_string_name: bool = untyped_dict.has(&"beta")
	var get_default_hit: int = untyped_dict.get(&"beta", -1)
	var get_default_miss: int = untyped_dict.get("missing_key", -7)
	var vec_key_value: int = untyped_dict[Vector2i(2, -3)]
	var bool_key_value: int = untyped_dict[true]
	var null_key_value: int = untyped_dict[null]
	var erased_beta: bool = untyped_dict.erase(&"beta")
	var dict_key_count: int = 0
	for key in untyped_dict:
		dict_key_count += 1

	# Typed dictionary with > 3 entries and int -> float value conversion.
	var typed_dict: Dictionary[int, float] = { 1: 10, 2: 20.5, 3: 30, 4: 40.5, 5: 50 }
	typed_dict[3] = 33
	var typed_dict_sum: float = 0.0
	for key: int in typed_dict:
		typed_dict_sum += typed_dict[key]

	# Self-aliasing on GET_KEYED, GET_KEYED_VALIDATED, and literal construction.
	var alias_untyped: Variant = { "next": 99 }
	alias_untyped = alias_untyped["next"]
	var alias_typed_dict: Dictionary = { "inner": { "leaf": 77 } }
	alias_typed_dict = alias_typed_dict["inner"]
	var alias_typed_leaf: int = alias_typed_dict["leaf"]
	var wrapped_dict: Dictionary = { "seed": 5 }
	wrapped_dict = { "prev": wrapped_dict, "tag": 8 }
	var wrapped_dict_seed: int = wrapped_dict["prev"]["seed"]

	# Typed and untyped arrays: positive/negative indexing, conversion, aliasing, and wrapping.
	var typed_int_array: Array[int] = [10, 20, 30, 40]
	typed_int_array[-1] = 45
	typed_int_array[1] = 25
	var typed_array_last: int = typed_int_array[-1]
	var typed_float_array: Array[float] = [1, 2.5, 3]
	typed_float_array.push_back(4)
	var typed_float_sum: float = 0.0
	for value: float in typed_float_array:
		typed_float_sum += value

	var untyped_array: Variant = [100, 200, 300]
	untyped_array[-2] = 250
	var untyped_array_mid: int = untyped_array[1]
	var alias_array: Variant = [64, 128]
	alias_array = alias_array[-1]
	var wrapped_array: Array = [7]
	wrapped_array = [wrapped_array, 9]
	var wrapped_array_inner: int = wrapped_array[0][0]

	# Empty and non-empty Array + Packed*Array iteration.
	var empty_array: Array[int] = []
	var empty_iter_count: int = 0
	for item: int in empty_array:
		empty_iter_count += 1
	var int_iter_sum: int = 0
	for item: int in typed_int_array:
		int_iter_sum += item
	var packed_ints := PackedInt32Array([3, 5, 7, 11])
	var packed_int_sum: int = 0
	for item: int in packed_ints:
		packed_int_sum += item
	var packed_vectors := PackedVector2Array([Vector2(1.0, 2.0), Vector2(3.0, 4.0)])
	var packed_vector_sum: float = 0.0
	for point: Vector2 in packed_vectors:
		packed_vector_sum += point.x + point.y
	var empty_packed := PackedFloat32Array()
	var empty_packed_count: int = 0
	for item: float in empty_packed:
		empty_packed_count += 1

	return {
		"string_name_hit": string_name_hit,
		"string_after_update": string_after_update,
		"has_string_name": has_string_name,
		"get_default_hit": get_default_hit,
		"get_default_miss": get_default_miss,
		"vec_key_value": vec_key_value,
		"bool_key_value": bool_key_value,
		"null_key_value": null_key_value,
		"erased_beta": erased_beta,
		"dict_key_count": dict_key_count,
		"typed_dict_sum": int(typed_dict_sum),
		"alias_untyped": alias_untyped,
		"alias_typed_leaf": alias_typed_leaf,
		"wrapped_dict_seed": wrapped_dict_seed,
		"typed_array_last": typed_array_last,
		"typed_float_sum": int(typed_float_sum * 2.0),
		"untyped_array_mid": untyped_array_mid,
		"alias_array": alias_array,
		"wrapped_array_inner": wrapped_array_inner,
		"empty_iter_count": empty_iter_count,
		"int_iter_sum": int_iter_sum,
		"packed_int_sum": packed_int_sum,
		"packed_vector_sum": int(packed_vector_sum),
		"empty_packed_count": empty_packed_count,
	}
)",
				"res://container_fast_paths.gd");

		const Vector<uint8_t> bytes = export_script_bytes(script);
		Ref<GDScript> loaded = load_script_bytes(bytes, "res://container_fast_paths.gd");
		REQUIRE(GDScriptBytecodeVerifier::verify_class(loaded.ptr()) == OK);

		Ref<RefCounted> orig_inst = memnew(RefCounted);
		orig_inst->set_script(script);
		Ref<RefCounted> loaded_inst = memnew(RefCounted);
		loaded_inst->set_script(loaded);

		Dictionary orig_res = orig_inst->call("run_container_tests");
		Dictionary loaded_res = loaded_inst->call("run_container_tests");
		CHECK(orig_res == loaded_res);
		CHECK(int(loaded_res["string_name_hit"]) == 30);
		CHECK(int(loaded_res["string_after_update"]) == 35);
		CHECK(bool(loaded_res["has_string_name"]) == true);
		CHECK(int(loaded_res["get_default_hit"]) == 20);
		CHECK(int(loaded_res["get_default_miss"]) == -7);
		CHECK(int(loaded_res["vec_key_value"]) == 44);
		CHECK(int(loaded_res["bool_key_value"]) == 55);
		CHECK(int(loaded_res["null_key_value"]) == 66);
		CHECK(bool(loaded_res["erased_beta"]) == true);
		CHECK(int(loaded_res["dict_key_count"]) == 4);
		CHECK(int(loaded_res["typed_dict_sum"]) == 154);
		CHECK(int(loaded_res["alias_untyped"]) == 99);
		CHECK(int(loaded_res["alias_typed_leaf"]) == 77);
		CHECK(int(loaded_res["wrapped_dict_seed"]) == 5);
		CHECK(int(loaded_res["typed_array_last"]) == 45);
		CHECK(int(loaded_res["typed_float_sum"]) == 21);
		CHECK(int(loaded_res["untyped_array_mid"]) == 250);
		CHECK(int(loaded_res["alias_array"]) == 128);
		CHECK(int(loaded_res["wrapped_array_inner"]) == 7);
		CHECK(int(loaded_res["empty_iter_count"]) == 0);
		CHECK(int(loaded_res["int_iter_sum"]) == 110);
		CHECK(int(loaded_res["packed_int_sum"]) == 26);
		CHECK(int(loaded_res["packed_vector_sum"]) == 10);
		CHECK(int(loaded_res["empty_packed_count"]) == 0);
	}

	TEST_CASE("Object instantiation, member storage, notification cache, and lambda/callable dispatch round-trip through .gdb") {
		// Direct unit coverage for GDScriptMemberStorage inline/heap transitions.
		{
			GDScriptMemberStorage storage;
			CHECK(storage.size() == 0);
			CHECK(storage.ptr() == nullptr);

			storage.resize(4);
			REQUIRE(storage.size() == 4);
			storage[0] = 11;
			storage[1] = String("inline");
			storage[2] = Vector2(1.0, 2.0);
			storage[3] = true;

			storage.resize(8);
			REQUIRE(storage.size() == 8);
			CHECK(int(storage[0]) == 11);
			CHECK(String(storage[1]) == "inline");
			storage[7] = 77;

			storage.resize(3);
			REQUIRE(storage.size() == 3);
			CHECK(int(storage[0]) == 11);
			CHECK(String(storage[1]) == "inline");
			CHECK(Vector2(storage[2]) == Vector2(1.0, 2.0));

			storage.resize(12);
			REQUIRE(storage.size() == 12);
			CHECK(int(storage[0]) == 11);
			CHECK(String(storage[1]) == "inline");
			storage[11] = String("spilled");

			storage.resize(5);
			REQUIRE(storage.size() == 5);
			CHECK(int(storage[0]) == 11);
			CHECK(String(storage[1]) == "inline");
			CHECK(Vector2(storage[2]) == Vector2(1.0, 2.0));

			storage.resize(0);
			CHECK(storage.size() == 0);
			CHECK(storage.ptr() == nullptr);
		}

		Ref<GDScript> script = compile_source(R"(
extends RefCounted

class BaseTemplate extends RefCounted:
	var base_int: int = 10
	var base_float: float = 3
	var base_text: String = "base"
	var base_vec: Vector2 = Vector2(2.0, 4.0)
	var base_flag: bool = true

class DerivedTemplate extends BaseTemplate:
	var extra: int = 25

class SpillMembers extends RefCounted:
	var m0: int = 1
	var m1: int = 2
	var m2: int = 3
	var m3: int = 4
	var m4: int = 5
	var m5: int = 6
	var m6: int = 7
	var m7: int = 8
	var m8: int = 9
	var m9: int = 10
	var m10: String = "ten"
	var m11: int = 12

class DynamicFallback extends RefCounted:
	var items: Array = []
	var map: Dictionary = {}
	var computed: int = _make_default()
	var setter_hits: int = 0
	var watched: int = 5:
		set(value):
			setter_hits += 1
			watched = value * 2

	func _make_default() -> int:
		return 13

class InitArgs extends RefCounted:
	var a: int
	var b: float
	var label: String
	var early_stopped: bool = false

	func _init(p_a: int, p_b: float = 2.5, p_label: String = "ok") -> void:
		a = p_a
		b = p_b
		label = p_label
		if p_a < 0:
			early_stopped = true
			return
		a += 1

class DerivedInit extends InitArgs:
	var bonus: int = 7

	func _init(p_a: int) -> void:
		super(p_a + 10, 4.5, "derived")
		bonus += a

class NodeHelper extends Node:
	var tag: int = 0
	@onready var ready_value: int = 7
	var seen_in_ready: int = 0

	func _init(p_tag: int) -> void:
		tag = p_tag

	func _ready() -> void:
		seen_in_ready = ready_value

	func make_self_lambda() -> Callable:
		return func(delta: int) -> int:
			return tag + delta

class NotifyBase extends RefCounted:
	static var log: Array[String] = []

	func _notification(what: int) -> void:
		if what == 777 or what == NOTIFICATION_PREDELETE:
			log.append("base:" + str(what))

class NotifyDerived extends NotifyBase:
	func _notification(what: int) -> void:
		if what == 777 or what == NOTIFICATION_PREDELETE:
			log.append("derived:" + str(what))

class MethodTargetBase extends RefCounted:
	var offset: int = 100

	func add_offset(value: int) -> int:
		return value + offset

class MethodTargetDerived extends MethodTargetBase:
	var scale: int = 3

	func compute(value: int, bonus: int = 2) -> int:
		return value * scale + bonus

var held_instance: InitArgs = null
var self_value: int = 40

func _self_method(delta: int) -> int:
	return self_value + delta

func run_object_and_callable_tests() -> Dictionary:
	# Template initialization across inheritance chain.
	var derived := DerivedTemplate.new()
	var template_sum: int = derived.base_int + int(derived.base_float) + int(derived.base_vec.x + derived.base_vec.y) + derived.extra
	var template_text: String = derived.base_text
	var template_flag: bool = derived.base_flag

	# >8 members spilling out of inline GDScriptMemberStorage.
	var spilled := SpillMembers.new()
	var spill_sum: int = spilled.m0 + spilled.m1 + spilled.m2 + spilled.m3 + spilled.m4 + spilled.m5 + spilled.m6 + spilled.m7 + spilled.m8 + spilled.m9 + spilled.m11
	var spill_text: String = spilled.m10

	# Fallback to @implicit_new for container literals and method-call initializers (no shared container aliasing).
	var dyn_a := DynamicFallback.new()
	var dyn_b := DynamicFallback.new()
	dyn_a.items.append(42)
	dyn_a.map["k"] = 9
	var dyn_isolated: bool = dyn_b.items.is_empty() and dyn_b.map.is_empty()
	var dyn_initial_watched: int = dyn_a.watched
	var dyn_computed: int = dyn_a.computed
	dyn_a.watched = 7
	var dyn_setter_value: int = dyn_a.watched
	var dyn_setter_hits: int = dyn_a.setter_hits

	# Inline _init with defaults, early return, super(), self-aliasing, class-var aliasing, and member target.
	var init_one := InitArgs.new(5)
	var init_all := InitArgs.new(10, 1.5, "custom")
	var init_early := InitArgs.new(-3)
	var derived_init := DerivedInit.new(4)
	var aliased_arg: Variant = InitArgs.new(20)
	aliased_arg = InitArgs.new(aliased_arg.a + 5)
	var cls_alias: Variant = InitArgs
	cls_alias = cls_alias.new(30)
	held_instance = InitArgs.new(8)
	var member_init_a: int = held_instance.a
	held_instance = null

	# Non-RefCounted Node subclass instantiation, Callable("_ready") @onready dispatch, and self-capturing lambda.
	var node_obj := NodeHelper.new(50)
	var ready_callable := Callable(node_obj, "_ready")
	ready_callable.call()
	var node_ready_seen: int = node_obj.seen_in_ready
	var node_lambda: Callable = node_obj.make_self_lambda()
	var node_lambda_alive: int = node_lambda.call(7)
	node_obj.free()

	# _notification ordering (forward, reversed, and PREDELETE).
	NotifyBase.log.clear()
	var notify_obj := NotifyDerived.new()
	notify_obj.notification(777, false)
	notify_obj.notification(777, true)
	notify_obj = null
	var notify_log: String = ",".join(NotifyBase.log)

	# Non-capturing, capturing, default-arg, self-capturing, and self-aliased lambdas.
	var non_cap := func(x: int) -> int:
		return x * 3 + 1
	var mul: int = 4
	var prefix: String = "v="
	var cap_lambda := func(x: int, extra: int = 2) -> String:
		return prefix + str(x * mul + extra)
	var self_lambda := func(delta: int) -> int:
		return self._self_method(delta) + self_value
	var captured_obj := InitArgs.new(12)
	var obj_cap_lambda := func(delta: int) -> int:
		return captured_obj.a + delta
	var alias_lambda: Variant = 9
	alias_lambda = func() -> int:
		return alias_lambda

	# Standard GDScript Callable(obj, method) for direct and inherited methods.
	var target := MethodTargetDerived.new()
	var direct_callable := Callable(target, "compute")
	var inherited_callable := Callable(target, "add_offset")

	return {
		"template_sum": template_sum,
		"template_text": template_text,
		"template_flag": template_flag,
		"spill_sum": spill_sum,
		"spill_text": spill_text,
		"dyn_isolated": dyn_isolated,
		"dyn_initial_watched": dyn_initial_watched,
		"dyn_computed": dyn_computed,
		"dyn_setter_value": dyn_setter_value,
		"dyn_setter_hits": dyn_setter_hits,
		"init_one_a": init_one.a,
		"init_one_b": int(init_one.b * 2.0),
		"init_one_label": init_one.label,
		"init_all_a": init_all.a,
		"init_all_label": init_all.label,
		"init_early_a": init_early.a,
		"init_early_stopped": init_early.early_stopped,
		"derived_init_bonus": derived_init.bonus,
		"aliased_arg_a": aliased_arg.a,
		"cls_alias_a": cls_alias.a,
		"member_init_a": member_init_a,
		"node_ready_seen": node_ready_seen,
		"node_lambda_alive": node_lambda_alive,
		"notify_log": notify_log,
		"non_cap": non_cap.call(7),
		"cap_default": cap_lambda.call(5),
		"cap_explicit": cap_lambda.call(5, 9),
		"self_lambda": self_lambda.call(6),
		"obj_cap_lambda": obj_cap_lambda.call(3),
		"alias_lambda": alias_lambda.call(),
		"direct_callable_default": direct_callable.call(4),
		"direct_callable_explicit": direct_callable.call(4, 5),
		"inherited_callable": inherited_callable.call(23),
	}
)",
				"res://objects_and_callables_fast_paths.gd");

		const Vector<uint8_t> bytes = export_script_bytes(script);
		Ref<GDScript> loaded = load_script_bytes(bytes, "res://objects_and_callables_fast_paths.gd");
		REQUIRE(GDScriptBytecodeVerifier::verify_class(loaded.ptr()) == OK);

		Ref<RefCounted> orig_inst = memnew(RefCounted);
		orig_inst->set_script(script);
		Ref<RefCounted> loaded_inst = memnew(RefCounted);
		loaded_inst->set_script(loaded);

		Dictionary orig_res = orig_inst->call("run_object_and_callable_tests");
		Dictionary loaded_res = loaded_inst->call("run_object_and_callable_tests");
		CHECK(orig_res == loaded_res);
		CHECK(int(loaded_res["template_sum"]) == 44);
		CHECK(String(loaded_res["template_text"]) == "base");
		CHECK(bool(loaded_res["template_flag"]) == true);
		CHECK(int(loaded_res["spill_sum"]) == 67);
		CHECK(String(loaded_res["spill_text"]) == "ten");
		CHECK(bool(loaded_res["dyn_isolated"]) == true);
		CHECK(int(loaded_res["dyn_initial_watched"]) == 5);
		CHECK(int(loaded_res["dyn_computed"]) == 13);
		CHECK(int(loaded_res["dyn_setter_value"]) == 14);
		CHECK(int(loaded_res["dyn_setter_hits"]) == 1);
		CHECK(int(loaded_res["init_one_a"]) == 6);
		CHECK(int(loaded_res["init_one_b"]) == 5);
		CHECK(String(loaded_res["init_one_label"]) == "ok");
		CHECK(int(loaded_res["init_all_a"]) == 11);
		CHECK(String(loaded_res["init_all_label"]) == "custom");
		CHECK(int(loaded_res["init_early_a"]) == -3);
		CHECK(bool(loaded_res["init_early_stopped"]) == true);
		CHECK(int(loaded_res["derived_init_bonus"]) == 22);
		CHECK(int(loaded_res["aliased_arg_a"]) == 27);
		CHECK(int(loaded_res["cls_alias_a"]) == 31);
		CHECK(int(loaded_res["member_init_a"]) == 9);
		CHECK(int(loaded_res["node_ready_seen"]) == 7);
		CHECK(int(loaded_res["node_lambda_alive"]) == 57);
		CHECK(String(loaded_res["notify_log"]) == "base:777,derived:777,derived:777,base:777,derived:1,base:1");
		CHECK(int(loaded_res["non_cap"]) == 22);
		CHECK(String(loaded_res["cap_default"]) == "v=22");
		CHECK(String(loaded_res["cap_explicit"]) == "v=29");
		CHECK(int(loaded_res["self_lambda"]) == 86);
		CHECK(int(loaded_res["obj_cap_lambda"]) == 16);
		CHECK(int(loaded_res["alias_lambda"]) == 9);
		CHECK(int(loaded_res["direct_callable_default"]) == 14);
		CHECK(int(loaded_res["direct_callable_explicit"]) == 17);
		CHECK(int(loaded_res["inherited_callable"]) == 123);

		// Hot-reload invalidation of instantiation template, _init, and _notification caches.
		{
			Ref<GDScript> reloadable;
			reloadable.instantiate();
			reloadable->set_source_code(R"(
extends RefCounted
var value: int = 10
var notified: int = 0
func _init(delta: int = 1) -> void:
	value += delta
func _notification(what: int) -> void:
	if what == 555:
		notified = 100
)");
			REQUIRE(reloadable->reload() == OK);

			Callable::CallError err;
			Variant first_obj = reloadable->_new(nullptr, 0, err);
			REQUIRE(err.error == Callable::CallError::CALL_OK);
			Ref<RefCounted> first_ref = first_obj;
			REQUIRE(first_ref.is_valid());
			CHECK(int(first_ref->get("value")) == 11);
			first_ref->notification(555);
			CHECK(int(first_ref->get("notified")) == 100);
			first_ref.unref();
			first_obj = Variant();

			reloadable->set_source_code(R"(
extends RefCounted
var value: int = 90
var notified: int = 0
func _init(delta: int = 5) -> void:
	value += delta
func _notification(what: int) -> void:
	if what == 555:
		notified = 200
)");
			REQUIRE(reloadable->reload() == OK);

			Variant second_obj = reloadable->_new(nullptr, 0, err);
			REQUIRE(err.error == Callable::CallError::CALL_OK);
			Ref<RefCounted> second_ref = second_obj;
			REQUIRE(second_ref.is_valid());
			CHECK(int(second_ref->get("value")) == 95);
			second_ref->notification(555);
			CHECK(int(second_ref->get("notified")) == 200);
		}

		// Live reload(true) of a derived script during base _notification dispatch must resolve the replacement function safely.
		{
			const String base_path = TestUtils::get_temp_path("test_notify_reload_base.gd");
			const String derived_path = TestUtils::get_temp_path("test_notify_reload_derived.gd");
			const String reloaded_derived_source = "extends \"" + base_path + "\"\nfunc _notification(what: int) -> void:\n\tif what == 888:\n\t\ttrace.append(\"derived_reloaded\")\n";
			{
				Ref<FileAccess> base_file = FileAccess::open(base_path, FileAccess::WRITE);
				REQUIRE(base_file.is_valid());
				base_file->store_string(
						"extends RefCounted\n"
						"var trace: Array = []\n"
						"var reloaded_source: String = \"\"\n"
						"func _notification(what: int) -> void:\n"
						"\tif what == 888:\n"
						"\t\ttrace.append(\"base\")\n"
						"\t\tvar derived_script: GDScript = get_script()\n"
						"\t\tderived_script.set_source_code(reloaded_source)\n"
						"\t\tderived_script.reload(true)\n");
				base_file->close();

				Ref<FileAccess> derived_file = FileAccess::open(derived_path, FileAccess::WRITE);
				REQUIRE(derived_file.is_valid());
				derived_file->store_string("extends \"" + base_path + "\"\nfunc _notification(what: int) -> void:\n\tif what == 888:\n\t\ttrace.append(\"derived_old\")\n");
				derived_file->close();
			}

			Ref<GDScript> derived_script = ResourceLoader::load(derived_path);
			REQUIRE(derived_script.is_valid());

			Callable::CallError err;
			Variant obj = derived_script->_new(nullptr, 0, err);
			REQUIRE(err.error == Callable::CallError::CALL_OK);
			Ref<RefCounted> obj_ref = obj;
			REQUIRE(obj_ref.is_valid());
			obj_ref->set("reloaded_source", reloaded_derived_source);

			obj_ref->notification(888, false);
			Array trace = obj_ref->get("trace");
			REQUIRE(trace.size() == 2);
			CHECK(String(trace[0]) == "base");
			CHECK(String(trace[1]) == "derived_reloaded");

			obj_ref.unref();
			obj = Variant();
			Ref<GDScript> base_script = derived_script->get_base();
			derived_script->clear();
			if (base_script.is_valid()) {
				base_script->clear();
			}
			DirAccess::remove_absolute(derived_path);
			DirAccess::remove_absolute(base_path);
		}
	}
}

} // namespace TestGDScriptBytecode

#endif // GDSCRIPT_BYTECODE_EXPORT_ENABLED
