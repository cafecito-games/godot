/**************************************************************************/
/*  gdscript_bytecode_loader.cpp                                          */
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

#include "gdscript_bytecode_loader.h"

#include "gdscript_bytecode_abi.gen.h"
#include "gdscript_bytecode_verifier.h"
#include "gdscript_cache.h"
#include "gdscript_utility_callable.h"
#include "gdscript_utility_functions.h"

#include "core/config/engine.h"
#include "core/io/marshalls.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/object/method_bind.h"
#include "core/templates/hash_set.h"
#include "core/variant/container_type_validate.h"
#include "core/version.h"

namespace {

using namespace GDScriptBytecodeFormat;

bool is_allowed_inline_variant_type(Variant::Type p_type) {
	switch (p_type) {
		case Variant::NIL:
		case Variant::BOOL:
		case Variant::INT:
		case Variant::FLOAT:
		case Variant::STRING:
		case Variant::VECTOR2:
		case Variant::VECTOR2I:
		case Variant::RECT2:
		case Variant::RECT2I:
		case Variant::VECTOR3:
		case Variant::VECTOR3I:
		case Variant::TRANSFORM2D:
		case Variant::VECTOR4:
		case Variant::VECTOR4I:
		case Variant::PLANE:
		case Variant::QUATERNION:
		case Variant::AABB:
		case Variant::BASIS:
		case Variant::TRANSFORM3D:
		case Variant::PROJECTION:
		case Variant::COLOR:
		case Variant::STRING_NAME:
		case Variant::NODE_PATH:
		case Variant::PACKED_BYTE_ARRAY:
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY:
		case Variant::PACKED_FLOAT32_ARRAY:
		case Variant::PACKED_FLOAT64_ARRAY:
		case Variant::PACKED_STRING_ARRAY:
		case Variant::PACKED_VECTOR2_ARRAY:
		case Variant::PACKED_VECTOR3_ARRAY:
		case Variant::PACKED_COLOR_ARRAY:
		case Variant::PACKED_VECTOR4_ARRAY:
			return true;
		default:
			return false;
	}
}

uint64_t measure_decoded_inline_variant_memory(const Variant &p_variant) {
	switch (p_variant.get_type()) {
		case Variant::STRING: {
			const String value = p_variant;
			return static_cast<uint64_t>(value.length()) * sizeof(char32_t);
		}
		case Variant::STRING_NAME: {
			const String value = String(p_variant.operator StringName());
			return static_cast<uint64_t>(value.length()) * sizeof(char32_t);
		}
		case Variant::NODE_PATH: {
			const String value = String(p_variant.operator NodePath());
			return static_cast<uint64_t>(value.length()) * sizeof(char32_t);
		}
		case Variant::PACKED_BYTE_ARRAY:
			return static_cast<uint64_t>(p_variant.operator PackedByteArray().size()) * sizeof(uint8_t);
		case Variant::PACKED_INT32_ARRAY:
			return static_cast<uint64_t>(p_variant.operator PackedInt32Array().size()) * sizeof(int32_t);
		case Variant::PACKED_INT64_ARRAY:
			return static_cast<uint64_t>(p_variant.operator PackedInt64Array().size()) * sizeof(int64_t);
		case Variant::PACKED_FLOAT32_ARRAY:
			return static_cast<uint64_t>(p_variant.operator PackedFloat32Array().size()) * sizeof(float);
		case Variant::PACKED_FLOAT64_ARRAY:
			return static_cast<uint64_t>(p_variant.operator PackedFloat64Array().size()) * sizeof(double);
		case Variant::PACKED_STRING_ARRAY: {
			const PackedStringArray array = p_variant;
			uint64_t total_bytes = static_cast<uint64_t>(array.size()) * sizeof(String);
			for (int index = 0; index < array.size(); index++) {
				total_bytes += static_cast<uint64_t>(array[index].length()) * sizeof(char32_t);
			}
			return total_bytes;
		}
		case Variant::PACKED_VECTOR2_ARRAY:
			return static_cast<uint64_t>(p_variant.operator PackedVector2Array().size()) * sizeof(Vector2);
		case Variant::PACKED_VECTOR3_ARRAY:
			return static_cast<uint64_t>(p_variant.operator PackedVector3Array().size()) * sizeof(Vector3);
		case Variant::PACKED_COLOR_ARRAY:
			return static_cast<uint64_t>(p_variant.operator PackedColorArray().size()) * sizeof(Color);
		case Variant::PACKED_VECTOR4_ARRAY:
			return static_cast<uint64_t>(p_variant.operator PackedVector4Array().size()) * sizeof(Vector4);
		default:
			return 0;
	}
}

class DecodeBudget {
	uint64_t buffer_bytes = 0;
	uint64_t remaining_bytes = 0;
	uint64_t decoded_string_table_bytes = 0;

public:
	explicit DecodeBudget(size_t p_buffer_size) :
			buffer_bytes(static_cast<uint64_t>(p_buffer_size)) {
		constexpr uint64_t MIN_DECODE_BUDGET = 64u * 1024u;
		constexpr uint64_t BUDGET_MULTIPLIER = 24u;
		if (buffer_bytes > UINT64_MAX / BUDGET_MULTIPLIER) {
			remaining_bytes = UINT64_MAX;
		} else {
			remaining_bytes = MAX(MIN_DECODE_BUDGET, buffer_bytes * BUDGET_MULTIPLIER);
		}
	}

	bool charge_bytes(uint64_t p_bytes) {
		if (p_bytes > remaining_bytes) {
			return false;
		}
		remaining_bytes -= p_bytes;
		return true;
	}

	bool charge_elements(uint64_t p_count, uint64_t p_bytes_per_element) {
		if (p_count == 0 || p_bytes_per_element == 0) {
			return true;
		}
		if (p_count > UINT64_MAX / p_bytes_per_element) {
			return false;
		}
		return charge_bytes(p_count * p_bytes_per_element);
	}

	bool charge_string_table_bytes(uint64_t p_utf8_length) {
		if (p_utf8_length > buffer_bytes || decoded_string_table_bytes > buffer_bytes - p_utf8_length) {
			return false;
		}
		decoded_string_table_bytes += p_utf8_length;
		return charge_elements(p_utf8_length + 1u, sizeof(char32_t));
	}
};

class BytecodeReader {
	const uint8_t *data = nullptr;
	size_t size = 0;
	size_t offset = 0;
	DecodeBudget budget;
	String *error_message = nullptr;

public:
	BytecodeReader(const Vector<uint8_t> &p_buffer, String *r_error_message) :
			data(p_buffer.ptr()),
			size(static_cast<size_t>(p_buffer.size())),
			offset(0),
			budget(static_cast<size_t>(p_buffer.size())),
			error_message(r_error_message) {}

	Error fail(Error p_error, const String &p_message) {
		if (error_message != nullptr) {
			*error_message = p_message;
		}
		return p_error;
	}

	size_t remaining() const {
		return size - offset;
	}

	const uint8_t *current_ptr() const {
		return data + offset;
	}

	bool charge_bytes(uint64_t p_bytes) {
		return budget.charge_bytes(p_bytes);
	}

	bool charge_elements(uint64_t p_count, uint64_t p_bytes_per_element) {
		return budget.charge_elements(p_count, p_bytes_per_element);
	}

	Error check_wire_count(uint32_t p_count, size_t p_min_bytes_per_item, uint64_t p_alloc_bytes_per_item, const char *p_label) {
		if (p_min_bytes_per_item > 0 && static_cast<size_t>(p_count) > remaining() / p_min_bytes_per_item) {
			return fail(ERR_INVALID_DATA, vformat("%s count %d exceeds remaining bytecode size.", p_label, p_count));
		}
		if (!budget.charge_elements(p_count, p_alloc_bytes_per_item)) {
			return fail(ERR_INVALID_DATA, vformat("%s count %d exceeds decode budget.", p_label, p_count));
		}
		return OK;
	}

	Error read_u8(uint8_t &r_value) {
		if (remaining() < 1) {
			return fail(ERR_FILE_EOF, "Unexpected end of bytecode stream reading u8.");
		}
		r_value = data[offset++];
		return OK;
	}

	Error read_bool(bool &r_value) {
		uint8_t raw = 0;
		Error error = read_u8(raw);
		if (error != OK) {
			return error;
		}
		if (raw > 1) {
			return fail(ERR_INVALID_DATA, vformat("Invalid boolean byte value %d (expected 0 or 1).", raw));
		}
		r_value = (raw == 1);
		return OK;
	}

	Error read_u32(uint32_t &r_value) {
		if (remaining() < 4) {
			return fail(ERR_FILE_EOF, "Unexpected end of bytecode stream reading u32.");
		}
		r_value = decode_uint32(data + offset);
		offset += 4;
		return OK;
	}

	Error read_i32(int32_t &r_value) {
		uint32_t raw = 0;
		Error error = read_u32(raw);
		if (error != OK) {
			return error;
		}
		r_value = static_cast<int32_t>(raw);
		return OK;
	}

	Error read_raw(uint8_t *r_dest, size_t p_length) {
		if (remaining() < p_length) {
			return fail(ERR_FILE_EOF, "Unexpected end of bytecode stream reading raw bytes.");
		}
		if (p_length > 0) {
			memcpy(r_dest, data + offset, p_length);
			offset += p_length;
		}
		return OK;
	}

	Error skip_bytes(size_t p_length) {
		if (remaining() < p_length) {
			return fail(ERR_FILE_EOF, "Unexpected end of bytecode stream.");
		}
		offset += p_length;
		return OK;
	}

	Error read_utf8_string(String &r_value, bool p_count_against_string_table) {
		uint32_t byte_length = 0;
		Error error = read_u32(byte_length);
		if (error != OK) {
			return error;
		}
		if (byte_length > remaining()) {
			return fail(ERR_FILE_EOF, vformat("UTF-8 string length %d exceeds remaining bytes.", byte_length));
		}
		if (p_count_against_string_table) {
			if (!budget.charge_string_table_bytes(byte_length)) {
				return fail(ERR_INVALID_DATA, "String table exceeds decode budget.");
			}
		} else {
			if (!budget.charge_elements(static_cast<uint64_t>(byte_length) + 1u, sizeof(char32_t))) {
				return fail(ERR_INVALID_DATA, "Header string exceeds decode budget.");
			}
		}

		String decoded;
		if (byte_length > 0) {
			if (decoded.append_utf8(reinterpret_cast<const char *>(data + offset), static_cast<int>(byte_length)) != OK) {
				return fail(ERR_INVALID_DATA, "Invalid UTF-8 byte sequence in bytecode string.");
			}
			offset += byte_length;
		}
		r_value = decoded;
		return OK;
	}
};

struct DecodedSkeletonBase {
	BaseClassKind kind = BASE_CLASS_NATIVE_ONLY;
	uint32_t local_class_index = 0;
	String external_path;
	String external_fqcn;
};

struct DecodedGlobalStore {
	int32_t code_offset = 0;
	StringName global_name;
};

} // namespace

class GDScriptBytecodeLoader::LoadContext {
	BytecodeReader reader;
	GDScriptBytecodeExternalResolver *resolver = nullptr;

	Vector<String> string_table;
	Vector<String> dependencies;

	Vector<Ref<GDScript>> local_classes;
	Vector<DecodedSkeletonBase> skeleton_bases;
	GDScriptBytecodeVerifier::GlobalStoreOffsetsMap global_store_offsets;

	static void reset_script_body_fields(GDScript *p_script) {
		if (p_script == nullptr) {
			return;
		}
		p_script->valid = false;
		p_script->members.clear();
		p_script->member_indices.clear();
		p_script->static_variables.clear();
		p_script->static_variables_indices.clear();
		p_script->constants.clear();
		p_script->_signals.clear();
		p_script->rpc_config.clear();
		p_script->lambda_info.clear();

		HashMap<StringName, GDScriptFunction *> old_member_functions(p_script->member_functions);
		p_script->member_functions.clear();
		p_script->initializer = nullptr;
		for (const KeyValue<StringName, GDScriptFunction *> &entry : old_member_functions) {
			if (entry.value != nullptr) {
				memdelete(entry.value);
			}
		}
		if (p_script->implicit_initializer != nullptr) {
			memdelete(p_script->implicit_initializer);
			p_script->implicit_initializer = nullptr;
		}
		if (p_script->implicit_ready != nullptr) {
			memdelete(p_script->implicit_ready);
			p_script->implicit_ready = nullptr;
		}
		if (p_script->static_initializer != nullptr) {
			memdelete(p_script->static_initializer);
			p_script->static_initializer = nullptr;
		}
	}

	Error read_string_index(String &r_out) {
		uint32_t index = 0;
		Error error = reader.read_u32(index);
		if (error != OK) {
			return error;
		}
		if (index >= static_cast<uint32_t>(string_table.size())) {
			return reader.fail(ERR_INVALID_DATA, vformat("String table index %d out of range (size=%d).", index, string_table.size()));
		}
		r_out = string_table[index];
		return OK;
	}

	Error read_string_name_index(StringName &r_out) {
		String value;
		Error error = read_string_index(value);
		if (error != OK) {
			return error;
		}
		r_out = StringName(value);
		return OK;
	}

	Error read_variant_type_u32(Variant::Type &r_type) {
		uint32_t raw_type = 0;
		Error error = reader.read_u32(raw_type);
		if (error != OK) {
			return error;
		}
		if (raw_type >= static_cast<uint32_t>(Variant::VARIANT_MAX)) {
			return reader.fail(ERR_INVALID_DATA, vformat("Variant::Type %d out of range.", raw_type));
		}
		r_type = static_cast<Variant::Type>(raw_type);
		return OK;
	}

	Error resolve_native_class(const StringName &p_class_name, Ref<GDScriptNativeClass> &r_native) {
		if (p_class_name == StringName()) {
			return reader.fail(ERR_INVALID_DATA, "Empty native class name in bytecode.");
		}
		GDScriptLanguage *language = GDScriptLanguage::get_singleton();
		if (language == nullptr) {
			return reader.fail(ERR_UNAVAILABLE, "GDScriptLanguage singleton is unavailable.");
		}
		const HashMap<StringName, int> &global_map = language->get_global_map();
		HashMap<StringName, int>::ConstIterator entry = global_map.find(p_class_name);
		if (!entry || entry->value < 0 || entry->value >= language->get_global_array_size()) {
			return reader.fail(ERR_CANT_RESOLVE, vformat("Native class \"%s\" is not registered in this build.", String(p_class_name)));
		}
		Object *native_object = language->get_global_array()[entry->value];
		GDScriptNativeClass *native_class = Object::cast_to<GDScriptNativeClass>(native_object);
		if (native_class == nullptr) {
			return reader.fail(ERR_CANT_RESOLVE, vformat("Global \"%s\" is not a GDScriptNativeClass.", String(p_class_name)));
		}
		r_native = Ref<GDScriptNativeClass>(native_class);
		return OK;
	}

	Error read_script_ref(Ref<GDScript> &r_script, bool &r_is_local_class) {
		uint8_t locality = 0;
		Error error = reader.read_u8(locality);
		if (error != OK) {
			return error;
		}
		if (locality == SCRIPT_REF_LOCAL) {
			uint32_t local_index = 0;
			error = reader.read_u32(local_index);
			if (error != OK) {
				return error;
			}
			if (local_index >= static_cast<uint32_t>(local_classes.size())) {
				return reader.fail(ERR_INVALID_DATA, vformat("Local ScriptRef index %d out of range (class_count=%d).", local_index, local_classes.size()));
			}
			r_script = local_classes[local_index];
			r_is_local_class = true;
			return OK;
		}
		if (locality == SCRIPT_REF_EXTERNAL) {
			String script_path;
			String script_fqcn;
			error = read_string_index(script_path);
			if (error != OK) {
				return error;
			}
			error = read_string_index(script_fqcn);
			if (error != OK) {
				return error;
			}
			if (script_path.is_empty()) {
				return reader.fail(ERR_INVALID_DATA, "External ScriptRef has empty path.");
			}
			if (resolver == nullptr) {
				return reader.fail(ERR_CANT_RESOLVE, vformat("Cannot resolve external script \"%s\" without an external resolver.", script_path));
			}
			Error resolve_error = OK;
			r_is_local_class = false;
			r_script = resolver->resolve_script(script_path, script_fqcn, r_is_local_class, resolve_error);
			if (resolve_error != OK || r_script.is_null()) {
				return reader.fail(resolve_error != OK ? resolve_error : ERR_CANT_RESOLVE, vformat("Failed to resolve external GDScript \"%s\" (%s).", script_path, script_fqcn));
			}
			return OK;
		}
		return reader.fail(ERR_INVALID_DATA, vformat("Invalid ScriptRef locality %d.", locality));
	}

	Error read_property_info(PropertyInfo &r_info) {
		Error error = read_variant_type_u32(r_info.type);
		if (error != OK) {
			return error;
		}
		error = read_string_index(r_info.name);
		if (error != OK) {
			return error;
		}
		error = read_string_name_index(r_info.class_name);
		if (error != OK) {
			return error;
		}
		uint32_t raw_hint = 0;
		error = reader.read_u32(raw_hint);
		if (error != OK) {
			return error;
		}
		if (raw_hint >= static_cast<uint32_t>(PROPERTY_HINT_MAX)) {
			return reader.fail(ERR_INVALID_DATA, vformat("PropertyHint %d out of range.", raw_hint));
		}
		r_info.hint = static_cast<PropertyHint>(raw_hint);
		error = read_string_index(r_info.hint_string);
		if (error != OK) {
			return error;
		}
		error = reader.read_u32(r_info.usage);
		return error;
	}

	Error read_method_info(MethodInfo &r_info) {
		Error error = read_string_index(r_info.name);
		if (error != OK) {
			return error;
		}
		error = read_property_info(r_info.return_val);
		if (error != OK) {
			return error;
		}
		error = reader.read_u32(r_info.flags);
		if (error != OK) {
			return error;
		}
		int32_t method_id = 0;
		error = reader.read_i32(method_id);
		if (error != OK) {
			return error;
		}
		r_info.id = method_id;

		uint32_t arg_count = 0;
		error = reader.read_u32(arg_count);
		if (error != OK) {
			return error;
		}
		if (arg_count > static_cast<uint32_t>(MAX_ARGUMENT_COUNT)) {
			return reader.fail(ERR_INVALID_DATA, vformat("MethodInfo argument count %d exceeds MAX_ARGUMENT_COUNT.", arg_count));
		}
		error = reader.check_wire_count(arg_count, 24, sizeof(PropertyInfo), "MethodInfo.arguments");
		if (error != OK) {
			return error;
		}
		r_info.arguments.resize(static_cast<int>(arg_count));
		for (uint32_t index = 0; index < arg_count; index++) {
			error = read_property_info(r_info.arguments.write[index]);
			if (error != OK) {
				return error;
			}
		}

		uint32_t defarg_count = 0;
		error = reader.read_u32(defarg_count);
		if (error != OK) {
			return error;
		}
		if (defarg_count > arg_count) {
			return reader.fail(ERR_INVALID_DATA, vformat("MethodInfo defarg_count %d exceeds arg_count %d.", defarg_count, arg_count));
		}
		error = reader.check_wire_count(defarg_count, 1, sizeof(Variant), "MethodInfo.default_arguments");
		if (error != OK) {
			return error;
		}
		r_info.default_arguments.resize(static_cast<int>(defarg_count));
		for (uint32_t index = 0; index < defarg_count; index++) {
			error = read_tagged_variant(r_info.default_arguments.write[index], 0);
			if (error != OK) {
				return error;
			}
		}

		int32_t return_metadata = 0;
		error = reader.read_i32(return_metadata);
		if (error != OK) {
			return error;
		}
		r_info.return_val_metadata = return_metadata;

		uint32_t arg_meta_count = 0;
		error = reader.read_u32(arg_meta_count);
		if (error != OK) {
			return error;
		}
		if (arg_meta_count != 0 && arg_meta_count != arg_count) {
			return reader.fail(ERR_INVALID_DATA, vformat("MethodInfo arg_meta_count %d must be 0 or arg_count (%d).", arg_meta_count, arg_count));
		}
		error = reader.check_wire_count(arg_meta_count, 4, sizeof(int), "MethodInfo.arguments_metadata");
		if (error != OK) {
			return error;
		}
		r_info.arguments_metadata.resize(static_cast<int>(arg_meta_count));
		for (uint32_t index = 0; index < arg_meta_count; index++) {
			int32_t meta_value = 0;
			error = reader.read_i32(meta_value);
			if (error != OK) {
				return error;
			}
			r_info.arguments_metadata.write[index] = meta_value;
		}

		return OK;
	}

	Error read_container_type(ContainerType &r_type) {
		Error error = read_variant_type_u32(r_type.builtin_type);
		if (error != OK) {
			return error;
		}
		error = read_string_name_index(r_type.class_name);
		if (error != OK) {
			return error;
		}
		bool has_script = false;
		error = reader.read_bool(has_script);
		if (error != OK) {
			return error;
		}

		if (r_type.class_name != StringName()) {
			if (r_type.builtin_type != Variant::OBJECT) {
				return reader.fail(ERR_INVALID_DATA, "ContainerType with class_name must have builtin_type == Variant::OBJECT.");
			}
			if (!ClassDB::class_exists(r_type.class_name)) {
				return reader.fail(ERR_CANT_RESOLVE, vformat("ContainerType native class \"%s\" does not exist in ClassDB.", String(r_type.class_name)));
			}
		}

		if (has_script) {
			if (r_type.builtin_type != Variant::OBJECT) {
				return reader.fail(ERR_INVALID_DATA, "ContainerType with script must have builtin_type == Variant::OBJECT.");
			}
			uint8_t script_tag = 0;
			error = reader.read_u8(script_tag);
			if (error != OK) {
				return error;
			}
			if (script_tag == TAG_SCRIPT_REF) {
				Ref<GDScript> gdscript;
				bool is_local = false;
				error = read_script_ref(gdscript, is_local);
				if (error != OK) {
					return error;
				}
				r_type.script = gdscript;
			} else if (script_tag == TAG_EXTERNAL_SCRIPT) {
				String script_path;
				error = read_string_index(script_path);
				if (error != OK) {
					return error;
				}
				if (script_path.is_empty() || resolver == nullptr) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("Cannot resolve external container script \"%s\".", script_path));
				}
				Error resolve_error = OK;
				Ref<Resource> resource = resolver->resolve_resource(script_path, "Script", resolve_error);
				Ref<Script> external_script = resource;
				if (resolve_error != OK || external_script.is_null()) {
					return reader.fail(resolve_error != OK ? resolve_error : ERR_CANT_RESOLVE, vformat("Failed to resolve external script \"%s\".", script_path));
				}
				r_type.script = external_script;
			} else {
				return reader.fail(ERR_INVALID_DATA, vformat("Invalid ContainerType script_tag %d.", script_tag));
			}
		}

		return OK;
	}

	Error read_data_type(GDScriptDataType &r_type, uint32_t p_depth, bool p_allow_void_return) {
		if (p_depth > MAX_TYPE_DEPTH) {
			return reader.fail(ERR_INVALID_DATA, "GDScriptDataType recursion exceeds MAX_TYPE_DEPTH.");
		}
		uint8_t raw_kind = 0;
		Error error = reader.read_u8(raw_kind);
		if (error != OK) {
			return error;
		}
		if (raw_kind > static_cast<uint8_t>(GDScriptDataType::GDSCRIPT)) {
			return reader.fail(ERR_INVALID_DATA, vformat("Invalid GDScriptDataType::Kind %d.", raw_kind));
		}
		r_type = GDScriptDataType();
		r_type.kind = static_cast<GDScriptDataType::Kind>(raw_kind);

		error = read_variant_type_u32(r_type.builtin_type);
		if (error != OK) {
			return error;
		}
		error = read_string_name_index(r_type.native_type);
		if (error != OK) {
			return error;
		}

		if (r_type.kind == GDScriptDataType::GDSCRIPT) {
			Ref<GDScript> gdscript;
			bool is_local_class = false;
			error = read_script_ref(gdscript, is_local_class);
			if (error != OK) {
				return error;
			}
			r_type.script_type = gdscript.ptr();
			if (!is_local_class) {
				r_type.script_type_ref = gdscript;
			}
		} else if (r_type.kind == GDScriptDataType::SCRIPT) {
			String script_path;
			error = read_string_index(script_path);
			if (error != OK) {
				return error;
			}
			if (script_path.is_empty() || resolver == nullptr) {
				return reader.fail(ERR_CANT_RESOLVE, vformat("Cannot resolve external SCRIPT type \"%s\".", script_path));
			}
			Error resolve_error = OK;
			Ref<Resource> resource = resolver->resolve_resource(script_path, "Script", resolve_error);
			Ref<Script> external_script = resource;
			if (resolve_error != OK || external_script.is_null()) {
				return reader.fail(resolve_error != OK ? resolve_error : ERR_CANT_RESOLVE, vformat("Failed to resolve external SCRIPT type \"%s\".", script_path));
			}
			r_type.script_type = external_script.ptr();
			r_type.script_type_ref = external_script;
		}

		uint32_t elem_count = 0;
		error = reader.read_u32(elem_count);
		if (error != OK) {
			return error;
		}

		// Enforce strict GDScriptDataType shape rules.
		switch (r_type.kind) {
			case GDScriptDataType::VARIANT:
				if (r_type.builtin_type != Variant::NIL || r_type.native_type != StringName() || elem_count != 0) {
					return reader.fail(ERR_INVALID_DATA, "Illegal VARIANT GDScriptDataType shape.");
				}
				break;
			case GDScriptDataType::BUILTIN:
				if (r_type.builtin_type == Variant::OBJECT) {
					return reader.fail(ERR_INVALID_DATA, "Illegal BUILTIN GDScriptDataType with Variant::OBJECT.");
				}
				if (r_type.builtin_type == Variant::NIL && !p_allow_void_return) {
					return reader.fail(ERR_INVALID_DATA, "Illegal BUILTIN GDScriptDataType with Variant::NIL.");
				}
				if (r_type.native_type != StringName()) {
					return reader.fail(ERR_INVALID_DATA, "Illegal BUILTIN GDScriptDataType with non-empty native_type.");
				}
				if (r_type.builtin_type == Variant::ARRAY) {
					if (elem_count != 0 && elem_count != 1) {
						return reader.fail(ERR_INVALID_DATA, vformat("Illegal ARRAY GDScriptDataType elem_count %d.", elem_count));
					}
				} else if (r_type.builtin_type == Variant::DICTIONARY) {
					if (elem_count != 0 && elem_count != 2) {
						return reader.fail(ERR_INVALID_DATA, vformat("Illegal DICTIONARY GDScriptDataType elem_count %d.", elem_count));
					}
				} else if (elem_count != 0) {
					return reader.fail(ERR_INVALID_DATA, vformat("Illegal non-container BUILTIN GDScriptDataType elem_count %d.", elem_count));
				}
				break;
			case GDScriptDataType::NATIVE:
				if (r_type.builtin_type != Variant::OBJECT || r_type.native_type == StringName() || elem_count != 0) {
					return reader.fail(ERR_INVALID_DATA, "Illegal NATIVE GDScriptDataType shape.");
				}
				if (!ClassDB::class_exists(r_type.native_type)) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("NATIVE GDScriptDataType class \"%s\" does not exist in ClassDB.", String(r_type.native_type)));
				}
				break;
			case GDScriptDataType::SCRIPT:
			case GDScriptDataType::GDSCRIPT:
				if (r_type.builtin_type != Variant::OBJECT || elem_count != 0 || r_type.script_type == nullptr) {
					return reader.fail(ERR_INVALID_DATA, "Illegal SCRIPT/GDSCRIPT GDScriptDataType shape.");
				}
				if (r_type.native_type != StringName() && !ClassDB::class_exists(r_type.native_type)) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("SCRIPT/GDSCRIPT native_type \"%s\" does not exist in ClassDB.", String(r_type.native_type)));
				}
				break;
		}

		if (elem_count > 0) {
			error = reader.check_wire_count(elem_count, 13, sizeof(GDScriptDataType), "GDScriptDataType.container_element_types");
			if (error != OK) {
				return error;
			}
			r_type.container_element_types.resize(static_cast<int>(elem_count));
			for (uint32_t index = 0; index < elem_count; index++) {
				error = read_data_type(r_type.container_element_types.write[index], p_depth + 1, false);
				if (error != OK) {
					return error;
				}
			}
		}

		return OK;
	}

	Error read_tagged_variant(Variant &r_variant, int p_depth) {
		if (p_depth > Variant::MAX_RECURSION_DEPTH) {
			return reader.fail(ERR_INVALID_DATA, "TaggedVariant recursion exceeds Variant::MAX_RECURSION_DEPTH.");
		}

		uint8_t raw_tag = 0;
		Error error = reader.read_u8(raw_tag);
		if (error != OK) {
			return error;
		}
		if (raw_tag >= TAG_MAX) {
			return reader.fail(ERR_INVALID_DATA, vformat("Invalid TaggedVariant tag %d.", raw_tag));
		}

		switch (static_cast<VariantTag>(raw_tag)) {
			case TAG_INLINE_VARIANT: {
				uint32_t byte_length = 0;
				error = reader.read_u32(byte_length);
				if (error != OK) {
					return error;
				}
				if (byte_length < 4 || byte_length > reader.remaining()) {
					return reader.fail(ERR_INVALID_DATA, vformat("Invalid TAG_INLINE_VARIANT byte_length %d.", byte_length));
				}
				if (!reader.charge_bytes(byte_length)) {
					return reader.fail(ERR_INVALID_DATA, "TAG_INLINE_VARIANT exceeds decode budget.");
				}

				// Inspect the marshalls header before calling decode_variant so disallowed container/object
				// payloads cannot trigger allocations before type rejection.
				const uint8_t *payload_ptr = reader.current_ptr();
				const uint32_t marshall_header = decode_uint32(payload_ptr);
				const Variant::Type header_type = static_cast<Variant::Type>(marshall_header & 0xFFu);
				if (!is_allowed_inline_variant_type(header_type)) {
					return reader.fail(ERR_INVALID_DATA, vformat("Disallowed Variant type %d inside TAG_INLINE_VARIANT.", static_cast<int>(header_type)));
				}

				int consumed_bytes = 0;
				error = decode_variant(r_variant, payload_ptr, static_cast<int>(byte_length), &consumed_bytes, false);
				if (error != OK || consumed_bytes != static_cast<int>(byte_length)) {
					return reader.fail(ERR_INVALID_DATA, "Failed to decode TAG_INLINE_VARIANT payload.");
				}
				if (!is_allowed_inline_variant_type(r_variant.get_type())) {
					return reader.fail(ERR_INVALID_DATA, vformat("Decoded disallowed Variant type %s inside TAG_INLINE_VARIANT.", Variant::get_type_name(r_variant.get_type())));
				}

				const uint64_t decoded_memory = measure_decoded_inline_variant_memory(r_variant);
				if (!reader.charge_bytes(decoded_memory)) {
					return reader.fail(ERR_INVALID_DATA, "Decoded TAG_INLINE_VARIANT exceeds decode budget.");
				}
				return reader.skip_bytes(byte_length);
			}
			case TAG_ARRAY: {
				bool is_read_only = false;
				error = reader.read_bool(is_read_only);
				if (error != OK) {
					return error;
				}
				ContainerType element_type;
				error = read_container_type(element_type);
				if (error != OK) {
					return error;
				}
				uint32_t array_size = 0;
				error = reader.read_u32(array_size);
				if (error != OK) {
					return error;
				}
				error = reader.check_wire_count(array_size, 1, sizeof(Variant), "TAG_ARRAY");
				if (error != OK) {
					return error;
				}

				Array array;
				if (element_type.builtin_type != Variant::NIL) {
					array.set_typed(element_type);
				}
				array.resize(static_cast<int>(array_size));
				for (uint32_t index = 0; index < array_size; index++) {
					Variant element;
					error = read_tagged_variant(element, p_depth + 1);
					if (error != OK) {
						return error;
					}
					array.set(static_cast<int>(index), element);
				}
				if (is_read_only) {
					array.make_read_only();
				}
				r_variant = array;
				return OK;
			}
			case TAG_DICTIONARY: {
				bool is_read_only = false;
				error = reader.read_bool(is_read_only);
				if (error != OK) {
					return error;
				}
				ContainerType key_type;
				error = read_container_type(key_type);
				if (error != OK) {
					return error;
				}
				ContainerType value_type;
				error = read_container_type(value_type);
				if (error != OK) {
					return error;
				}
				uint32_t dict_size = 0;
				error = reader.read_u32(dict_size);
				if (error != OK) {
					return error;
				}
				error = reader.check_wire_count(dict_size, 2, sizeof(Variant) * 2u, "TAG_DICTIONARY");
				if (error != OK) {
					return error;
				}

				Dictionary dictionary;
				if (key_type.builtin_type != Variant::NIL || value_type.builtin_type != Variant::NIL) {
					dictionary.set_typed(key_type, value_type);
				}
				for (uint32_t index = 0; index < dict_size; index++) {
					Variant key;
					Variant value;
					error = read_tagged_variant(key, p_depth + 1);
					if (error != OK) {
						return error;
					}
					error = read_tagged_variant(value, p_depth + 1);
					if (error != OK) {
						return error;
					}
					dictionary[key] = value;
				}
				if (is_read_only) {
					dictionary.make_read_only();
				}
				r_variant = dictionary;
				return OK;
			}
			case TAG_SCRIPT_REF: {
				Ref<GDScript> gdscript;
				bool is_local = false;
				error = read_script_ref(gdscript, is_local);
				if (error != OK) {
					return error;
				}
				r_variant = gdscript;
				return OK;
			}
			case TAG_EXTERNAL_SCRIPT: {
				String script_path;
				error = read_string_index(script_path);
				if (error != OK) {
					return error;
				}
				if (script_path.is_empty() || resolver == nullptr) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("Cannot resolve TAG_EXTERNAL_SCRIPT \"%s\".", script_path));
				}
				Error resolve_error = OK;
				Ref<Resource> resource = resolver->resolve_resource(script_path, "Script", resolve_error);
				Ref<Script> external_script = resource;
				if (resolve_error != OK || external_script.is_null()) {
					return reader.fail(resolve_error != OK ? resolve_error : ERR_CANT_RESOLVE, vformat("Failed to resolve external Script \"%s\".", script_path));
				}
				r_variant = external_script;
				return OK;
			}
			case TAG_EXTERNAL_RESOURCE: {
				String resource_path;
				error = read_string_index(resource_path);
				if (error != OK) {
					return error;
				}
				if (resource_path.is_empty() || resolver == nullptr) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("Cannot resolve TAG_EXTERNAL_RESOURCE \"%s\".", resource_path));
				}
				Error resolve_error = OK;
				Ref<Resource> resource = resolver->resolve_resource(resource_path, "Resource", resolve_error);
				if (resolve_error != OK || resource.is_null()) {
					return reader.fail(resolve_error != OK ? resolve_error : ERR_CANT_RESOLVE, vformat("Failed to resolve external Resource \"%s\".", resource_path));
				}
				r_variant = resource;
				return OK;
			}
			case TAG_NATIVE_CLASS: {
				StringName class_name;
				error = read_string_name_index(class_name);
				if (error != OK) {
					return error;
				}
				Ref<GDScriptNativeClass> native_class;
				error = resolve_native_class(class_name, native_class);
				if (error != OK) {
					return error;
				}
				r_variant = native_class;
				return OK;
			}
			case TAG_ENGINE_SINGLETON: {
				StringName singleton_name;
				error = read_string_name_index(singleton_name);
				if (error != OK) {
					return error;
				}
				if (Engine::get_singleton() == nullptr || !Engine::get_singleton()->has_singleton(singleton_name)) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("Engine singleton \"%s\" is not available in this build.", String(singleton_name)));
				}
				Object *singleton_object = Engine::get_singleton()->get_singleton_object(singleton_name);
				if (singleton_object == nullptr) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("Engine singleton \"%s\" resolved to null.", String(singleton_name)));
				}
				r_variant = singleton_object;
				return OK;
			}
			case TAG_UTILITY_CALLABLE: {
				StringName method_name;
				error = read_string_name_index(method_name);
				if (error != OK) {
					return error;
				}
				if (!GDScriptUtilityFunctions::function_exists(method_name) && !Variant::has_utility_function(method_name)) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("Utility function \"%s\" does not exist in this build.", String(method_name)));
				}
				r_variant = Callable(memnew(GDScriptUtilityCallable(method_name)));
				return OK;
			}
			case TAG_NULL_OBJECT:
				r_variant = Variant(static_cast<Object *>(nullptr));
				return OK;
			case TAG_DEFAULT_VALUE: {
				Variant::Type default_type = Variant::NIL;
				error = read_variant_type_u32(default_type);
				if (error != OK) {
					return error;
				}
				switch (default_type) {
					case Variant::CALLABLE:
						r_variant = Callable();
						return OK;
					case Variant::SIGNAL:
						r_variant = Signal();
						return OK;
					case Variant::RID:
						r_variant = RID();
						return OK;
					default:
						return reader.fail(ERR_INVALID_DATA, vformat("TAG_DEFAULT_VALUE only permits CALLABLE, SIGNAL, or RID (got %s).", Variant::get_type_name(default_type)));
				}
			}
			case TAG_MAX:
			default:
				return reader.fail(ERR_INVALID_DATA, vformat("Unsupported VariantTag %d.", raw_tag));
		}
	}

	Error read_skeleton_class(const Ref<GDScript> &p_script, GDScript *p_owner, const Ref<GDScript> &p_root_script, uint32_t p_depth) {
		if (p_depth > MAX_CLASS_DEPTH) {
			return reader.fail(ERR_INVALID_DATA, "SkeletonClass nesting exceeds MAX_CLASS_DEPTH.");
		}
		if (!reader.charge_bytes(sizeof(GDScript))) {
			return reader.fail(ERR_INVALID_DATA, "SkeletonClass exceeds decode budget.");
		}

		String fqcn;
		StringName local_name;
		StringName global_name;
		String simplified_icon_path;
		StringName native_class_name;
		uint8_t class_flags = 0;
		uint8_t base_kind = 0;

		Error error = read_string_index(fqcn);
		if (error != OK) {
			return error;
		}
		error = read_string_name_index(local_name);
		if (error != OK) {
			return error;
		}
		error = read_string_name_index(global_name);
		if (error != OK) {
			return error;
		}
		error = read_string_index(simplified_icon_path);
		if (error != OK) {
			return error;
		}
		error = read_string_name_index(native_class_name);
		if (error != OK) {
			return error;
		}
		error = reader.read_u8(class_flags);
		if (error != OK) {
			return error;
		}
		if ((class_flags & ~CLASS_FLAG_KNOWN_MASK) != 0) {
			return reader.fail(ERR_INVALID_DATA, vformat("Unknown SkeletonClass class_flags bits 0x%x.", class_flags));
		}
		error = reader.read_u8(base_kind);
		if (error != OK) {
			return error;
		}

		DecodedSkeletonBase base_descriptor;
		if (base_kind == BASE_CLASS_NATIVE_ONLY) {
			base_descriptor.kind = BASE_CLASS_NATIVE_ONLY;
		} else if (base_kind == BASE_CLASS_LOCAL) {
			base_descriptor.kind = BASE_CLASS_LOCAL;
			error = reader.read_u32(base_descriptor.local_class_index);
			if (error != OK) {
				return error;
			}
		} else if (base_kind == BASE_CLASS_EXTERNAL) {
			base_descriptor.kind = BASE_CLASS_EXTERNAL;
			error = read_string_index(base_descriptor.external_path);
			if (error != OK) {
				return error;
			}
			error = read_string_index(base_descriptor.external_fqcn);
			if (error != OK) {
				return error;
			}
			if (base_descriptor.external_path.is_empty()) {
				return reader.fail(ERR_INVALID_DATA, "External base class has empty path.");
			}
		} else {
			return reader.fail(ERR_INVALID_DATA, vformat("Invalid SkeletonClass base_kind %d.", base_kind));
		}

		Ref<GDScriptNativeClass> native_class;
		error = resolve_native_class(native_class_name, native_class);
		if (error != OK) {
			return error;
		}

		p_script->fully_qualified_name = fqcn;
		p_script->local_name = local_name;
		p_script->global_name = global_name;
		p_script->simplified_icon_path = simplified_icon_path;
		p_script->native = native_class;
		p_script->tool = (class_flags & CLASS_FLAG_TOOL) != 0;
		p_script->_is_abstract = (class_flags & CLASS_FLAG_ABSTRACT) != 0;
		p_script->_owner = p_owner;
		if (p_owner != nullptr) {
			p_script->path = p_root_script->path;
			p_script->path_valid = p_root_script->path_valid;
		}
		p_script->set_bytecode_backed(true);

		local_classes.push_back(p_script);
		skeleton_bases.push_back(base_descriptor);

		uint32_t subclass_count = 0;
		error = reader.read_u32(subclass_count);
		if (error != OK) {
			return error;
		}
		error = reader.check_wire_count(subclass_count, 26, sizeof(Ref<GDScript>), "SkeletonClass.subclasses");
		if (error != OK) {
			return error;
		}

		HashMap<StringName, Ref<GDScript>> previous_subclasses(p_script->subclasses);
		p_script->subclasses.clear();

		for (uint32_t index = 0; index < subclass_count; index++) {
			StringName subclass_key;
			error = read_string_name_index(subclass_key);
			if (error != OK) {
				return error;
			}
			if (p_script->subclasses.has(subclass_key)) {
				return reader.fail(ERR_INVALID_DATA, vformat("Duplicate subclass key \"%s\" in \"%s\".", String(subclass_key), fqcn));
			}
			Ref<GDScript> subclass;
			HashMap<StringName, Ref<GDScript>>::ConstIterator existing = previous_subclasses.find(subclass_key);
			if (existing && existing->value.is_valid()) {
				subclass = existing->value;
			} else {
				subclass.instantiate();
			}
			p_script->subclasses.insert(subclass_key, subclass);

			error = read_skeleton_class(subclass, p_script.ptr(), p_root_script, p_depth + 1);
			if (error != OK) {
				return error;
			}
		}

		return OK;
	}

	Error read_member_info_entry(StringName &r_name, GDScript::MemberInfo &r_info) {
		Error error = read_string_name_index(r_name);
		if (error != OK) {
			return error;
		}
		int32_t member_index = 0;
		error = reader.read_i32(member_index);
		if (error != OK) {
			return error;
		}
		if (member_index < 0) {
			return reader.fail(ERR_INVALID_DATA, vformat("MemberInfo \"%s\" has negative index %d.", String(r_name), member_index));
		}
		r_info.index = member_index;
		error = read_string_name_index(r_info.setter);
		if (error != OK) {
			return error;
		}
		error = read_string_name_index(r_info.getter);
		if (error != OK) {
			return error;
		}
		error = read_data_type(r_info.data_type, 0, false);
		if (error != OK) {
			return error;
		}
		return read_property_info(r_info.property_info);
	}

	Error read_function_record(
			GDScript *p_owning_script,
			uint32_t p_lambda_depth,
			const String &p_enclosing_function_name,
			const String &p_lambda_ordinal_path,
			GDScriptFunction *&r_function) {
		r_function = nullptr;
		if (p_lambda_depth > MAX_LAMBDA_DEPTH) {
			return reader.fail(ERR_INVALID_DATA, "Function lambda recursion exceeds MAX_LAMBDA_DEPTH.");
		}
		if (!reader.charge_bytes(sizeof(GDScriptFunction))) {
			return reader.fail(ERR_INVALID_DATA, "FunctionRecord exceeds decode budget.");
		}

		GDScriptFunction *function = memnew(GDScriptFunction);
		function->_script = p_owning_script;
		function->source = StringName(p_owning_script->path);

		struct FunctionGuard {
			GDScriptFunction *ptr = nullptr;
			~FunctionGuard() {
				if (ptr != nullptr) {
					memdelete(ptr);
				}
			}
			GDScriptFunction *release() {
				GDScriptFunction *released = ptr;
				ptr = nullptr;
				return released;
			}
		} function_guard{ function };

		Error error = read_string_name_index(function->name);
		if (error != OK) {
			return error;
		}
		error = reader.read_bool(function->_static);
		if (error != OK) {
			return error;
		}
		int32_t initial_line = 0;
		error = reader.read_i32(initial_line);
		if (error != OK || initial_line < 0) {
			return error != OK ? error : reader.fail(ERR_INVALID_DATA, vformat("Negative initial_line %d.", initial_line));
		}
		function->_initial_line = initial_line;

		int32_t argument_count = 0;
		int32_t vararg_index = -1;
		int32_t stack_size = 0;
		int32_t instruction_args_size = 0;
		error = reader.read_i32(argument_count);
		if (error == OK) {
			error = reader.read_i32(vararg_index);
		}
		if (error == OK) {
			error = reader.read_i32(stack_size);
		}
		if (error == OK) {
			error = reader.read_i32(instruction_args_size);
		}
		if (error != OK) {
			return error;
		}

		if (argument_count < 0 || argument_count > MAX_ARGUMENT_COUNT ||
				stack_size < GDScriptFunction::FIXED_ADDRESSES_MAX || stack_size > MAX_STACK_SIZE ||
				instruction_args_size < 0 || instruction_args_size > MAX_INSTRUCTION_ARGS_SIZE ||
				argument_count > stack_size - GDScriptFunction::FIXED_ADDRESSES_MAX) {
			return reader.fail(ERR_INVALID_DATA, vformat("Function \"%s\" scalar VM metadata exceeds ceilings.", String(function->name)));
		}

		const uint64_t frame_alloca_bytes =
				static_cast<uint64_t>(sizeof(Variant *)) * static_cast<uint64_t>(GDScriptFunction::FIXED_ADDRESSES_MAX) +
				static_cast<uint64_t>(sizeof(Variant *)) * static_cast<uint64_t>(instruction_args_size) +
				static_cast<uint64_t>(sizeof(Variant)) * static_cast<uint64_t>(stack_size);
		if (frame_alloca_bytes > MAX_FRAME_ALLOCA_BYTES) {
			return reader.fail(ERR_INVALID_DATA, vformat("Function \"%s\" frame size exceeds MAX_FRAME_ALLOCA_BYTES.", String(function->name)));
		}

		if (vararg_index != -1 && (vararg_index != GDScriptFunction::FIXED_ADDRESSES_MAX + argument_count || vararg_index >= stack_size)) {
			return reader.fail(ERR_INVALID_DATA, vformat("Function \"%s\" has invalid vararg_index %d.", String(function->name), vararg_index));
		}

		function->_argument_count = argument_count;
		function->_vararg_index = vararg_index;
		function->_stack_size = stack_size;
		function->_instruction_args_size = instruction_args_size;

		uint32_t arg_type_count = 0;
		error = reader.read_u32(arg_type_count);
		if (error != OK || arg_type_count != static_cast<uint32_t>(argument_count)) {
			return error != OK ? error : reader.fail(ERR_INVALID_DATA, vformat("Function \"%s\" arg_type_count %d != argument_count %d.", String(function->name), arg_type_count, argument_count));
		}
		error = reader.check_wire_count(arg_type_count, 13, sizeof(GDScriptDataType), "FunctionRecord.argument_types");
		if (error != OK) {
			return error;
		}
		function->argument_types.resize(static_cast<int>(arg_type_count));
		for (uint32_t index = 0; index < arg_type_count; index++) {
			error = read_data_type(function->argument_types.write[index], 0, false);
			if (error != OK) {
				return error;
			}
		}

		error = read_data_type(function->return_type, 0, true);
		if (error != OK) {
			return error;
		}

		error = read_method_info(function->method_info);
		if (error != OK) {
			return error;
		}

		error = read_tagged_variant(function->rpc_config, 0);
		if (error != OK) {
			return error;
		}

		uint32_t temp_slot_count = 0;
		error = reader.read_u32(temp_slot_count);
		if (error != OK) {
			return error;
		}
		if (temp_slot_count > static_cast<uint32_t>(stack_size)) {
			return reader.fail(ERR_INVALID_DATA, vformat("Function \"%s\" temp_slot_count %d exceeds stack_size %d.", String(function->name), temp_slot_count, stack_size));
		}
		error = reader.check_wire_count(temp_slot_count, 8, sizeof(Pair<int, Variant::Type>), "FunctionRecord.temporary_slots");
		if (error != OK) {
			return error;
		}
		function->temporary_slots.reserve(temp_slot_count);
		for (uint32_t index = 0; index < temp_slot_count; index++) {
			int32_t slot_index = 0;
			Variant::Type slot_type = Variant::NIL;
			error = reader.read_i32(slot_index);
			if (error == OK) {
				error = read_variant_type_u32(slot_type);
			}
			if (error != OK) {
				return error;
			}
			function->temporary_slots.push_back(Pair<int, Variant::Type>(slot_index, slot_type));
		}

		uint32_t code_size = 0;
		error = reader.read_u32(code_size);
		if (error != OK) {
			return error;
		}
		if (code_size == 0 || code_size > MAX_CODE_WORDS) {
			return reader.fail(ERR_INVALID_DATA, vformat("Function \"%s\" code_size %d out of bounds.", String(function->name), code_size));
		}
		error = reader.check_wire_count(code_size, 4, sizeof(int), "FunctionRecord.code");
		if (error != OK) {
			return error;
		}
		function->code.resize(static_cast<int>(code_size));
		for (uint32_t index = 0; index < code_size; index++) {
			int32_t word = 0;
			error = reader.read_i32(word);
			if (error != OK) {
				return error;
			}
			function->code.write[index] = word;
		}
		function->_code_size = static_cast<int>(code_size);
		function->_code_ptr = function->code.ptrw();

		uint32_t optional_arg_count = 0;
		uint32_t default_jump_count = 0;
		error = reader.read_u32(optional_arg_count);
		if (error == OK) {
			error = reader.read_u32(default_jump_count);
		}
		if (error != OK) {
			return error;
		}
		if (optional_arg_count > static_cast<uint32_t>(argument_count)) {
			return reader.fail(ERR_INVALID_DATA, vformat("Function \"%s\" optional_arg_count %d exceeds argument_count %d.", String(function->name), optional_arg_count, argument_count));
		}
		const uint32_t expected_default_jump_count = optional_arg_count == 0 ? 0u : (optional_arg_count + 1u);
		if (default_jump_count != expected_default_jump_count) {
			return reader.fail(ERR_INVALID_DATA, vformat("Function \"%s\" default_jump_count %d != expected %d.", String(function->name), default_jump_count, expected_default_jump_count));
		}
		error = reader.check_wire_count(default_jump_count, 4, sizeof(int), "FunctionRecord.default_jumps");
		if (error != OK) {
			return error;
		}
		function->default_arguments.resize(static_cast<int>(default_jump_count));
		for (uint32_t index = 0; index < default_jump_count; index++) {
			int32_t jump_target = 0;
			error = reader.read_i32(jump_target);
			if (error != OK) {
				return error;
			}
			function->default_arguments.write[index] = jump_target;
		}
		function->_default_arg_count = static_cast<int>(optional_arg_count);
		function->_default_arg_ptr = function->default_arguments.is_empty() ? nullptr : function->default_arguments.ptr();

		uint32_t constant_count = 0;
		error = reader.read_u32(constant_count);
		if (error != OK) {
			return error;
		}
		error = reader.check_wire_count(constant_count, 1, sizeof(Variant), "FunctionRecord.constants");
		if (error != OK) {
			return error;
		}
		function->constants.resize(static_cast<int>(constant_count));
		for (uint32_t index = 0; index < constant_count; index++) {
			error = read_tagged_variant(function->constants.write[index], 0);
			if (error != OK) {
				return error;
			}
		}
		function->_constant_count = static_cast<int>(constant_count);
		function->_constants_ptr = function->constants.is_empty() ? nullptr : function->constants.ptrw();

		uint32_t global_name_count = 0;
		error = reader.read_u32(global_name_count);
		if (error != OK) {
			return error;
		}
		error = reader.check_wire_count(global_name_count, 4, sizeof(StringName), "FunctionRecord.global_names");
		if (error != OK) {
			return error;
		}
		function->global_names.resize(static_cast<int>(global_name_count));
		for (uint32_t index = 0; index < global_name_count; index++) {
			error = read_string_name_index(function->global_names.write[index]);
			if (error != OK) {
				return error;
			}
		}
		function->_global_names_count = static_cast<int>(global_name_count);
		function->_global_names_ptr = function->global_names.is_empty() ? nullptr : function->global_names.ptr();

		// Fixup tables 0..11 (and DEBUG_ENABLED debug name vectors reconstructed from the same keys).
		// 0: FIXUP_OPERATOR
		{
			uint32_t count = 0;
			error = reader.read_u32(count);
			if (error == OK) {
				error = reader.check_wire_count(count, 12, sizeof(Variant::ValidatedOperatorEvaluator), "FIXUP_OPERATOR");
			}
			if (error != OK) {
				return error;
			}
			function->operator_funcs.resize(static_cast<int>(count));
#ifdef DEBUG_ENABLED
			function->operator_names.resize(static_cast<int>(count));
#endif
			for (uint32_t index = 0; index < count; index++) {
				uint32_t raw_op = 0;
				Variant::Type left_type = Variant::NIL;
				Variant::Type right_type = Variant::NIL;
				error = reader.read_u32(raw_op);
				if (error == OK) {
					error = read_variant_type_u32(left_type);
				}
				if (error == OK) {
					error = read_variant_type_u32(right_type);
				}
				if (error != OK || raw_op >= static_cast<uint32_t>(Variant::OP_MAX)) {
					return error != OK ? error : reader.fail(ERR_INVALID_DATA, vformat("FIXUP_OPERATOR operation %d out of range.", raw_op));
				}
				const Variant::Operator operation = static_cast<Variant::Operator>(raw_op);
				Variant::ValidatedOperatorEvaluator evaluator = Variant::get_validated_operator_evaluator(operation, left_type, right_type);
				if (evaluator == nullptr) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("Could not resolve validated operator %s(%s, %s).", Variant::get_operator_name(operation), Variant::get_type_name(left_type), Variant::get_type_name(right_type)));
				}
				function->operator_funcs.write[index] = evaluator;
#ifdef DEBUG_ENABLED
				function->operator_names.write[index] = Variant::get_operator_name(operation);
#endif
			}
			function->_operator_funcs_count = static_cast<int>(count);
			function->_operator_funcs_ptr = function->operator_funcs.is_empty() ? nullptr : function->operator_funcs.ptr();
		}

		// 1: FIXUP_SETTER
		{
			uint32_t count = 0;
			error = reader.read_u32(count);
			if (error == OK) {
				error = reader.check_wire_count(count, 8, sizeof(Variant::ValidatedSetter), "FIXUP_SETTER");
			}
			if (error != OK) {
				return error;
			}
			function->setters.resize(static_cast<int>(count));
#ifdef DEBUG_ENABLED
			function->setter_names.resize(static_cast<int>(count));
#endif
			for (uint32_t index = 0; index < count; index++) {
				Variant::Type builtin_type = Variant::NIL;
				StringName member_name;
				error = read_variant_type_u32(builtin_type);
				if (error == OK) {
					error = read_string_name_index(member_name);
				}
				if (error != OK) {
					return error;
				}
				Variant::ValidatedSetter setter = Variant::get_member_validated_setter(builtin_type, member_name);
				if (setter == nullptr) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("Could not resolve validated setter %s.%s.", Variant::get_type_name(builtin_type), String(member_name)));
				}
				function->setters.write[index] = setter;
#ifdef DEBUG_ENABLED
				function->setter_names.write[index] = String(member_name);
#endif
			}
			function->_setters_count = static_cast<int>(count);
			function->_setters_ptr = function->setters.is_empty() ? nullptr : function->setters.ptr();
		}

		// 2: FIXUP_GETTER
		{
			uint32_t count = 0;
			error = reader.read_u32(count);
			if (error == OK) {
				error = reader.check_wire_count(count, 8, sizeof(Variant::ValidatedGetter), "FIXUP_GETTER");
			}
			if (error != OK) {
				return error;
			}
			function->getters.resize(static_cast<int>(count));
#ifdef DEBUG_ENABLED
			function->getter_names.resize(static_cast<int>(count));
#endif
			for (uint32_t index = 0; index < count; index++) {
				Variant::Type builtin_type = Variant::NIL;
				StringName member_name;
				error = read_variant_type_u32(builtin_type);
				if (error == OK) {
					error = read_string_name_index(member_name);
				}
				if (error != OK) {
					return error;
				}
				Variant::ValidatedGetter getter = Variant::get_member_validated_getter(builtin_type, member_name);
				if (getter == nullptr) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("Could not resolve validated getter %s.%s.", Variant::get_type_name(builtin_type), String(member_name)));
				}
				function->getters.write[index] = getter;
#ifdef DEBUG_ENABLED
				function->getter_names.write[index] = String(member_name);
#endif
			}
			function->_getters_count = static_cast<int>(count);
			function->_getters_ptr = function->getters.is_empty() ? nullptr : function->getters.ptr();
		}

		// 3: FIXUP_KEYED_SETTER
		{
			uint32_t count = 0;
			error = reader.read_u32(count);
			if (error == OK) {
				error = reader.check_wire_count(count, 4, sizeof(Variant::ValidatedKeyedSetter), "FIXUP_KEYED_SETTER");
			}
			if (error != OK) {
				return error;
			}
			function->keyed_setters.resize(static_cast<int>(count));
			for (uint32_t index = 0; index < count; index++) {
				Variant::Type builtin_type = Variant::NIL;
				error = read_variant_type_u32(builtin_type);
				if (error != OK) {
					return error;
				}
				Variant::ValidatedKeyedSetter setter = Variant::get_member_validated_keyed_setter(builtin_type);
				if (setter == nullptr) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("Could not resolve keyed setter for %s.", Variant::get_type_name(builtin_type)));
				}
				function->keyed_setters.write[index] = setter;
			}
			function->_keyed_setters_count = static_cast<int>(count);
			function->_keyed_setters_ptr = function->keyed_setters.is_empty() ? nullptr : function->keyed_setters.ptr();
		}

		// 4: FIXUP_KEYED_GETTER
		{
			uint32_t count = 0;
			error = reader.read_u32(count);
			if (error == OK) {
				error = reader.check_wire_count(count, 4, sizeof(Variant::ValidatedKeyedGetter), "FIXUP_KEYED_GETTER");
			}
			if (error != OK) {
				return error;
			}
			function->keyed_getters.resize(static_cast<int>(count));
			for (uint32_t index = 0; index < count; index++) {
				Variant::Type builtin_type = Variant::NIL;
				error = read_variant_type_u32(builtin_type);
				if (error != OK) {
					return error;
				}
				Variant::ValidatedKeyedGetter getter = Variant::get_member_validated_keyed_getter(builtin_type);
				if (getter == nullptr) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("Could not resolve keyed getter for %s.", Variant::get_type_name(builtin_type)));
				}
				function->keyed_getters.write[index] = getter;
			}
			function->_keyed_getters_count = static_cast<int>(count);
			function->_keyed_getters_ptr = function->keyed_getters.is_empty() ? nullptr : function->keyed_getters.ptr();
		}

		// 5: FIXUP_INDEXED_SETTER
		{
			uint32_t count = 0;
			error = reader.read_u32(count);
			if (error == OK) {
				error = reader.check_wire_count(count, 4, sizeof(Variant::ValidatedIndexedSetter), "FIXUP_INDEXED_SETTER");
			}
			if (error != OK) {
				return error;
			}
			function->indexed_setters.resize(static_cast<int>(count));
			for (uint32_t index = 0; index < count; index++) {
				Variant::Type builtin_type = Variant::NIL;
				error = read_variant_type_u32(builtin_type);
				if (error != OK) {
					return error;
				}
				Variant::ValidatedIndexedSetter setter = Variant::get_member_validated_indexed_setter(builtin_type);
				if (setter == nullptr) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("Could not resolve indexed setter for %s.", Variant::get_type_name(builtin_type)));
				}
				function->indexed_setters.write[index] = setter;
			}
			function->_indexed_setters_count = static_cast<int>(count);
			function->_indexed_setters_ptr = function->indexed_setters.is_empty() ? nullptr : function->indexed_setters.ptr();
		}

		// 6: FIXUP_INDEXED_GETTER
		{
			uint32_t count = 0;
			error = reader.read_u32(count);
			if (error == OK) {
				error = reader.check_wire_count(count, 4, sizeof(Variant::ValidatedIndexedGetter), "FIXUP_INDEXED_GETTER");
			}
			if (error != OK) {
				return error;
			}
			function->indexed_getters.resize(static_cast<int>(count));
			for (uint32_t index = 0; index < count; index++) {
				Variant::Type builtin_type = Variant::NIL;
				error = read_variant_type_u32(builtin_type);
				if (error != OK) {
					return error;
				}
				Variant::ValidatedIndexedGetter getter = Variant::get_member_validated_indexed_getter(builtin_type);
				if (getter == nullptr) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("Could not resolve indexed getter for %s.", Variant::get_type_name(builtin_type)));
				}
				function->indexed_getters.write[index] = getter;
			}
			function->_indexed_getters_count = static_cast<int>(count);
			function->_indexed_getters_ptr = function->indexed_getters.is_empty() ? nullptr : function->indexed_getters.ptr();
		}

		// 7: FIXUP_BUILTIN_METHOD
		{
			uint32_t count = 0;
			error = reader.read_u32(count);
			if (error == OK) {
				error = reader.check_wire_count(count, 8, sizeof(Variant::ValidatedBuiltInMethod), "FIXUP_BUILTIN_METHOD");
			}
			if (error != OK) {
				return error;
			}
			function->builtin_methods.resize(static_cast<int>(count));
#ifdef DEBUG_ENABLED
			function->builtin_methods_names.resize(static_cast<int>(count));
#endif
			for (uint32_t index = 0; index < count; index++) {
				Variant::Type builtin_type = Variant::NIL;
				StringName method_name;
				error = read_variant_type_u32(builtin_type);
				if (error == OK) {
					error = read_string_name_index(method_name);
				}
				if (error != OK) {
					return error;
				}
				Variant::ValidatedBuiltInMethod method_ptr = Variant::get_validated_builtin_method(builtin_type, method_name);
				if (method_ptr == nullptr) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("Could not resolve builtin method %s.%s.", Variant::get_type_name(builtin_type), String(method_name)));
				}
				function->builtin_methods.write[index] = method_ptr;
#ifdef DEBUG_ENABLED
				function->builtin_methods_names.write[index] = String(method_name);
#endif
			}
			function->_builtin_methods_count = static_cast<int>(count);
			function->_builtin_methods_ptr = function->builtin_methods.is_empty() ? nullptr : function->builtin_methods.ptr();
		}

		// 8: FIXUP_CONSTRUCTOR
		{
			uint32_t count = 0;
			error = reader.read_u32(count);
			if (error == OK) {
				error = reader.check_wire_count(count, 8, sizeof(Variant::ValidatedConstructor), "FIXUP_CONSTRUCTOR");
			}
			if (error != OK) {
				return error;
			}
			function->constructors.resize(static_cast<int>(count));
#ifdef DEBUG_ENABLED
			function->constructors_names.resize(static_cast<int>(count));
#endif
			for (uint32_t index = 0; index < count; index++) {
				Variant::Type builtin_type = Variant::NIL;
				int32_t constructor_index = 0;
				error = read_variant_type_u32(builtin_type);
				if (error == OK) {
					error = reader.read_i32(constructor_index);
				}
				if (error != OK) {
					return error;
				}
				const int available_constructors = Variant::get_constructor_count(builtin_type);
				if (constructor_index < 0 || constructor_index >= available_constructors) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("Constructor index %d out of range for %s (count=%d).", constructor_index, Variant::get_type_name(builtin_type), available_constructors));
				}
				Variant::ValidatedConstructor constructor_ptr = Variant::get_validated_constructor(builtin_type, constructor_index);
				if (constructor_ptr == nullptr) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("Could not resolve validated constructor %s#%d.", Variant::get_type_name(builtin_type), constructor_index));
				}
				function->constructors.write[index] = constructor_ptr;
#ifdef DEBUG_ENABLED
				function->constructors_names.write[index] = Variant::get_type_name(builtin_type);
#endif
			}
			function->_constructors_count = static_cast<int>(count);
			function->_constructors_ptr = function->constructors.is_empty() ? nullptr : function->constructors.ptr();
		}

		// 9: FIXUP_UTILITY
		{
			uint32_t count = 0;
			error = reader.read_u32(count);
			if (error == OK) {
				error = reader.check_wire_count(count, 4, sizeof(Variant::ValidatedUtilityFunction), "FIXUP_UTILITY");
			}
			if (error != OK) {
				return error;
			}
			function->utilities.resize(static_cast<int>(count));
#ifdef DEBUG_ENABLED
			function->utilities_names.resize(static_cast<int>(count));
#endif
			for (uint32_t index = 0; index < count; index++) {
				StringName utility_name;
				error = read_string_name_index(utility_name);
				if (error != OK) {
					return error;
				}
				Variant::ValidatedUtilityFunction utility_ptr = Variant::get_validated_utility_function(utility_name);
				if (utility_ptr == nullptr) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("Could not resolve validated utility function \"%s\".", String(utility_name)));
				}
				function->utilities.write[index] = utility_ptr;
#ifdef DEBUG_ENABLED
				function->utilities_names.write[index] = String(utility_name);
#endif
			}
			function->_utilities_count = static_cast<int>(count);
			function->_utilities_ptr = function->utilities.is_empty() ? nullptr : function->utilities.ptr();
		}

		// 10: FIXUP_GDS_UTILITY
		{
			uint32_t count = 0;
			error = reader.read_u32(count);
			if (error == OK) {
				error = reader.check_wire_count(count, 4, sizeof(GDScriptUtilityFunctions::FunctionPtr), "FIXUP_GDS_UTILITY");
			}
			if (error != OK) {
				return error;
			}
			function->gds_utilities.resize(static_cast<int>(count));
#ifdef DEBUG_ENABLED
			function->gds_utilities_names.resize(static_cast<int>(count));
#endif
			for (uint32_t index = 0; index < count; index++) {
				StringName utility_name;
				error = read_string_name_index(utility_name);
				if (error != OK) {
					return error;
				}
				if (!GDScriptUtilityFunctions::function_exists(utility_name)) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("GDScript utility function \"%s\" does not exist in this build.", String(utility_name)));
				}
				GDScriptUtilityFunctions::FunctionPtr utility_ptr = GDScriptUtilityFunctions::get_function(utility_name);
				if (utility_ptr == nullptr) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("Could not resolve GDScript utility function \"%s\".", String(utility_name)));
				}
				function->gds_utilities.write[index] = utility_ptr;
#ifdef DEBUG_ENABLED
				function->gds_utilities_names.write[index] = String(utility_name);
#endif
			}
			function->_gds_utilities_count = static_cast<int>(count);
			function->_gds_utilities_ptr = function->gds_utilities.is_empty() ? nullptr : function->gds_utilities.ptr();
		}

		// 11: FIXUP_METHOD_BIND
		{
			uint32_t count = 0;
			error = reader.read_u32(count);
			if (error == OK) {
				error = reader.check_wire_count(count, 8, sizeof(MethodBind *), "FIXUP_METHOD_BIND");
			}
			if (error != OK) {
				return error;
			}
			function->methods.resize(static_cast<int>(count));
			for (uint32_t index = 0; index < count; index++) {
				StringName class_name;
				StringName method_name;
				error = read_string_name_index(class_name);
				if (error == OK) {
					error = read_string_name_index(method_name);
				}
				if (error != OK) {
					return error;
				}
				if (!ClassDB::class_exists(class_name)) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("Class \"%s\" referenced by MethodBind \"%s.%s\" is not registered in this build.", String(class_name), String(class_name), String(method_name)));
				}
				MethodBind *method_bind = ClassDB::get_method(class_name, method_name);
				if (method_bind == nullptr) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("Method \"%s.%s\" is not registered in this build.", String(class_name), String(method_name)));
				}
				function->methods.write[index] = method_bind;
			}
			function->_methods_count = static_cast<int>(count);
			function->_methods_ptr = function->methods.is_empty() ? nullptr : function->methods.ptrw();
		}

		// Rebake OPCODE_STORE_GLOBAL by name BEFORE verification.
		{
			uint32_t global_store_count = 0;
			error = reader.read_u32(global_store_count);
			if (error == OK) {
				error = reader.check_wire_count(global_store_count, 8, sizeof(int), "FunctionRecord.global_stores");
			}
			if (error != OK) {
				return error;
			}
			Vector<int> store_offsets;
			store_offsets.resize(static_cast<int>(global_store_count));
			GDScriptLanguage *language = GDScriptLanguage::get_singleton();
			if (language == nullptr) {
				return reader.fail(ERR_UNAVAILABLE, "GDScriptLanguage singleton is unavailable.");
			}
			const HashMap<StringName, int> &global_map = language->get_global_map();
			for (uint32_t index = 0; index < global_store_count; index++) {
				int32_t code_offset = 0;
				StringName global_name;
				error = reader.read_i32(code_offset);
				if (error == OK) {
					error = read_string_name_index(global_name);
				}
				if (error != OK) {
					return error;
				}
				if (code_offset < 2 || code_offset >= function->code.size() || function->code[code_offset - 2] != GDScriptFunction::OPCODE_STORE_GLOBAL) {
					return reader.fail(ERR_INVALID_DATA, vformat("Function \"%s\" global_store offset %d does not point to an OPCODE_STORE_GLOBAL operand.", String(function->name), code_offset));
				}
				if (function->code[code_offset] != 0) {
					return reader.fail(ERR_INVALID_DATA, vformat("Function \"%s\" OPCODE_STORE_GLOBAL operand at offset %d must be masked to 0 in bytecode.", String(function->name), code_offset));
				}
				HashMap<StringName, int>::ConstIterator global_entry = global_map.find(global_name);
				if (!global_entry) {
					return reader.fail(ERR_CANT_RESOLVE, vformat("Global \"%s\" referenced by function \"%s\" does not exist in runtime global map.", String(global_name), String(function->name)));
				}
				function->code.write[code_offset] = global_entry->value;
				store_offsets.write[index] = code_offset;
			}
			if (!store_offsets.is_empty()) {
				global_store_offsets.insert(function, store_offsets);
			}
		}

#ifdef DEBUG_ENABLED
		function->func_cname = (String(function->source) + " - " + String(function->name)).utf8();
		function->_func_cname = function->func_cname.get_data();

		const String base_function_name = p_enclosing_function_name.is_empty() ? String(function->name) : p_enclosing_function_name;
		String profile_signature = vformat("%s::%d::%s.%s", String(function->source), function->_initial_line, p_owning_script->fully_qualified_name, base_function_name);
		if (!p_lambda_ordinal_path.is_empty()) {
			profile_signature += "#lambda" + p_lambda_ordinal_path;
		}
		function->profile.signature = StringName(profile_signature);
#endif

		// Nested lambdas.
		uint32_t lambda_count = 0;
		error = reader.read_u32(lambda_count);
		if (error == OK) {
			error = reader.check_wire_count(lambda_count, 5, sizeof(GDScriptFunction *), "FunctionRecord.lambdas");
		}
		if (error != OK) {
			return error;
		}
		if (lambda_count > 0) {
			const String enclosing_name = p_enclosing_function_name.is_empty() ? String(function->name) : p_enclosing_function_name;
			for (uint32_t index = 0; index < lambda_count; index++) {
				int32_t capture_count = 0;
				bool use_self = false;
				error = reader.read_i32(capture_count);
				if (error == OK) {
					error = reader.read_bool(use_self);
				}
				if (error != OK || capture_count < 0) {
					return error != OK ? error : reader.fail(ERR_INVALID_DATA, vformat("Negative lambda capture_count %d.", capture_count));
				}
				const String child_ordinal_path = p_lambda_ordinal_path.is_empty() ? itos(static_cast<int64_t>(index)) : (p_lambda_ordinal_path + "." + itos(static_cast<int64_t>(index)));
				GDScriptFunction *lambda_function = nullptr;
				error = read_function_record(p_owning_script, p_lambda_depth + 1, enclosing_name, child_ordinal_path, lambda_function);
				if (error != OK) {
					return error;
				}
				function->lambdas.push_back(lambda_function);
				p_owning_script->lambda_info.insert(lambda_function, GDScript::LambdaInfo{ capture_count, use_self });
			}
			function->_lambdas_count = function->lambdas.size();
			function->_lambdas_ptr = function->lambdas.ptrw();
		}

		r_function = function_guard.release();
		return OK;
	}

	Error read_class_body(GDScript *p_script) {
		reset_script_body_fields(p_script);

		// Own members.
		uint32_t own_member_count = 0;
		Error error = reader.read_u32(own_member_count);
		if (error == OK) {
			error = reader.check_wire_count(own_member_count, 4, sizeof(StringName), "ClassBody.members");
		}
		if (error != OK) {
			return error;
		}
		for (uint32_t index = 0; index < own_member_count; index++) {
			StringName member_name;
			error = read_string_name_index(member_name);
			if (error != OK) {
				return error;
			}
			if (p_script->members.has(member_name)) {
				return reader.fail(ERR_INVALID_DATA, vformat("Duplicate own member \"%s\" in \"%s\".", String(member_name), p_script->fully_qualified_name));
			}
			p_script->members.insert(member_name);
		}

		// Member indices.
		uint32_t member_index_count = 0;
		error = reader.read_u32(member_index_count);
		if (error == OK) {
			error = reader.check_wire_count(member_index_count, 40, sizeof(GDScript::MemberInfo), "ClassBody.member_indices");
		}
		if (error != OK) {
			return error;
		}
		for (uint32_t index = 0; index < member_index_count; index++) {
			StringName member_name;
			GDScript::MemberInfo member_info;
			error = read_member_info_entry(member_name, member_info);
			if (error != OK) {
				return error;
			}
			if (p_script->member_indices.has(member_name)) {
				return reader.fail(ERR_INVALID_DATA, vformat("Duplicate member_indices key \"%s\" in \"%s\".", String(member_name), p_script->fully_qualified_name));
			}
			p_script->member_indices.insert(member_name, member_info);
		}

		// Static variable indices.
		uint32_t static_var_count = 0;
		error = reader.read_u32(static_var_count);
		if (error == OK) {
			error = reader.check_wire_count(static_var_count, 40, sizeof(GDScript::MemberInfo) + sizeof(Variant), "ClassBody.static_var_indices");
		}
		if (error != OK) {
			return error;
		}
		for (uint32_t index = 0; index < static_var_count; index++) {
			StringName static_name;
			GDScript::MemberInfo member_info;
			error = read_member_info_entry(static_name, member_info);
			if (error != OK) {
				return error;
			}
			if (p_script->static_variables_indices.has(static_name)) {
				return reader.fail(ERR_INVALID_DATA, vformat("Duplicate static_variables_indices key \"%s\" in \"%s\".", String(static_name), p_script->fully_qualified_name));
			}
			p_script->static_variables_indices.insert(static_name, member_info);
		}
		p_script->static_variables.resize(static_cast<int>(p_script->static_variables_indices.size()));

		// Constants.
		uint32_t constant_count = 0;
		error = reader.read_u32(constant_count);
		if (error == OK) {
			error = reader.check_wire_count(constant_count, 5, sizeof(Variant) + sizeof(StringName), "ClassBody.constants");
		}
		if (error != OK) {
			return error;
		}
		for (uint32_t index = 0; index < constant_count; index++) {
			StringName constant_name;
			Variant constant_value;
			error = read_string_name_index(constant_name);
			if (error == OK) {
				error = read_tagged_variant(constant_value, 0);
			}
			if (error != OK) {
				return error;
			}
			if (p_script->constants.has(constant_name)) {
				return reader.fail(ERR_INVALID_DATA, vformat("Duplicate constant \"%s\" in \"%s\".", String(constant_name), p_script->fully_qualified_name));
			}
			p_script->constants.insert(constant_name, constant_value);
		}

		// Signals.
		uint32_t signal_count = 0;
		error = reader.read_u32(signal_count);
		if (error == OK) {
			error = reader.check_wire_count(signal_count, 40, sizeof(MethodInfo), "ClassBody.signals");
		}
		if (error != OK) {
			return error;
		}
		for (uint32_t index = 0; index < signal_count; index++) {
			StringName signal_name;
			MethodInfo signal_info;
			error = read_string_name_index(signal_name);
			if (error == OK) {
				error = read_method_info(signal_info);
			}
			if (error != OK) {
				return error;
			}
			if (StringName(signal_info.name) != signal_name) {
				return reader.fail(ERR_INVALID_DATA, vformat("Signal key \"%s\" does not match MethodInfo.name \"%s\".", String(signal_name), signal_info.name));
			}
			if (p_script->_signals.has(signal_name)) {
				return reader.fail(ERR_INVALID_DATA, vformat("Duplicate signal \"%s\" in \"%s\".", String(signal_name), p_script->fully_qualified_name));
			}
			p_script->_signals.insert(signal_name, signal_info);
		}

		// RPC config (must decode to Dictionary).
		Variant rpc_variant;
		error = read_tagged_variant(rpc_variant, 0);
		if (error != OK) {
			return error;
		}
		if (rpc_variant.get_type() != Variant::DICTIONARY) {
			return reader.fail(ERR_INVALID_DATA, vformat("ClassBody rpc_config must decode to Dictionary (got %s).", Variant::get_type_name(rpc_variant.get_type())));
		}
		p_script->rpc_config = rpc_variant;

		// Member functions (decoded into a local map first so cleanup on failure cannot collide with member_functions).
		uint32_t member_func_count = 0;
		error = reader.read_u32(member_func_count);
		if (error == OK) {
			error = reader.check_wire_count(member_func_count, 40, sizeof(GDScriptFunction *), "ClassBody.member_functions");
		}
		if (error != OK) {
			return error;
		}
		HashMap<StringName, GDScriptFunction *> decoded_member_functions;
		auto cleanup_decoded_functions = [&]() {
			for (const KeyValue<StringName, GDScriptFunction *> &entry : decoded_member_functions) {
				if (entry.value != nullptr) {
					memdelete(entry.value);
				}
			}
			decoded_member_functions.clear();
		};
		for (uint32_t index = 0; index < member_func_count; index++) {
			StringName function_key;
			error = read_string_name_index(function_key);
			if (error != OK) {
				cleanup_decoded_functions();
				return error;
			}
			if (decoded_member_functions.has(function_key)) {
				cleanup_decoded_functions();
				return reader.fail(ERR_INVALID_DATA, vformat("Duplicate member function \"%s\" in \"%s\".", String(function_key), p_script->fully_qualified_name));
			}
			GDScriptFunction *function = nullptr;
			error = read_function_record(p_script, 0, String(), String(), function);
			if (error != OK) {
				cleanup_decoded_functions();
				return error;
			}
			if (function->name != function_key) {
				const String decoded_function_name = String(function->name);
				memdelete(function);
				cleanup_decoded_functions();
				return reader.fail(ERR_INVALID_DATA, vformat("Member function key \"%s\" does not match FunctionRecord.name \"%s\".", String(function_key), decoded_function_name));
			}
			decoded_member_functions.insert(function_key, function);
		}

		// Special functions.
		bool has_implicit_init = false;
		error = reader.read_bool(has_implicit_init);
		if (error != OK) {
			cleanup_decoded_functions();
			return error;
		}
		if (has_implicit_init) {
			error = read_function_record(p_script, 0, String(), String(), p_script->implicit_initializer);
			if (error != OK) {
				cleanup_decoded_functions();
				return error;
			}
		}

		bool has_implicit_ready = false;
		error = reader.read_bool(has_implicit_ready);
		if (error != OK) {
			cleanup_decoded_functions();
			return error;
		}
		if (has_implicit_ready) {
			error = read_function_record(p_script, 0, String(), String(), p_script->implicit_ready);
			if (error != OK) {
				cleanup_decoded_functions();
				return error;
			}
		}

		bool has_static_init = false;
		error = reader.read_bool(has_static_init);
		if (error != OK) {
			cleanup_decoded_functions();
			return error;
		}
		if (has_static_init) {
			error = read_function_record(p_script, 0, String(), String(), p_script->static_initializer);
			if (error != OK) {
				cleanup_decoded_functions();
				return error;
			}
		}

		for (const KeyValue<StringName, GDScriptFunction *> &entry : decoded_member_functions) {
			p_script->member_functions.insert(entry.key, entry.value);
		}
		const StringName init_function_name = GDScriptLanguage::get_singleton() != nullptr ? GDScriptLanguage::get_singleton()->strings._init : SNAME("_init");
		HashMap<StringName, GDScriptFunction *>::ConstIterator init_entry = p_script->member_functions.find(init_function_name);
		p_script->initializer = init_entry ? init_entry->value : nullptr;

		return OK;
	}

	Error link_base_classes(bool p_resolve_external_bases) {
		auto clear_all_bases = [&]() {
			for (int clear_index = 0; clear_index < local_classes.size(); clear_index++) {
				local_classes[clear_index]->base = Ref<GDScript>();
			}
		};

		for (int index = 0; index < local_classes.size(); index++) {
			GDScript *script = local_classes[index].ptr();
			const DecodedSkeletonBase &base_descriptor = skeleton_bases[index];
			switch (base_descriptor.kind) {
				case BASE_CLASS_NATIVE_ONLY:
					script->base = Ref<GDScript>();
					break;
				case BASE_CLASS_LOCAL:
					if (base_descriptor.local_class_index >= static_cast<uint32_t>(local_classes.size())) {
						clear_all_bases();
						return reader.fail(ERR_INVALID_DATA, vformat("Local base class index %d out of range.", base_descriptor.local_class_index));
					}
					if (base_descriptor.local_class_index == static_cast<uint32_t>(index)) {
						clear_all_bases();
						return reader.fail(ERR_INVALID_DATA, vformat("Class \"%s\" cannot inherit from itself.", script->fully_qualified_name));
					}
					script->base = local_classes[base_descriptor.local_class_index];
					break;
				case BASE_CLASS_EXTERNAL:
					if (p_resolve_external_bases) {
						if (resolver == nullptr) {
							clear_all_bases();
							return reader.fail(ERR_CANT_RESOLVE, vformat("Cannot resolve external base script \"%s\" without resolver.", base_descriptor.external_path));
						}
						Error resolve_error = OK;
						bool is_local = false;
						Ref<GDScript> external_base = resolver->resolve_script(base_descriptor.external_path, base_descriptor.external_fqcn, is_local, resolve_error);
						if (resolve_error != OK || external_base.is_null()) {
							clear_all_bases();
							return reader.fail(resolve_error != OK ? resolve_error : ERR_CANT_RESOLVE, vformat("Failed to resolve external base script \"%s\" (%s).", base_descriptor.external_path, base_descriptor.external_fqcn));
						}
						script->base = external_base;
					} else {
						script->base = Ref<GDScript>();
					}
					break;
			}
		}

		// Reject any cycle in the base-class chain and clear bases on failure to prevent Ref<GDScript> leaks.
		for (int index = 0; index < local_classes.size(); index++) {
			HashSet<const GDScript *> visited;
			const GDScript *cursor = local_classes[index].ptr();
			while (cursor != nullptr) {
				if (visited.has(cursor)) {
					const String cyclic_name = cursor->fully_qualified_name;
					clear_all_bases();
					return reader.fail(ERR_INVALID_DATA, vformat("Cyclic base class inheritance detected at \"%s\".", cyclic_name));
				}
				visited.insert(cursor);
				cursor = cursor->base.ptr();
			}
		}
		return OK;
	}

public:
	LoadContext(const Vector<uint8_t> &p_buffer, GDScriptBytecodeExternalResolver *p_resolver, String *r_error_message) :
			reader(p_buffer, r_error_message), resolver(p_resolver) {}

	Error read_header_and_flags(GDScriptBytecodeLoader::HeaderInfo &r_header) {
		uint8_t magic[4] = {};
		Error error = reader.read_raw(magic, 4);
		if (error != OK) {
			return error;
		}
		if (memcmp(magic, MAGIC, 4) != 0) {
			return reader.fail(ERR_FILE_CORRUPT, "Invalid GDScript bytecode magic bytes.");
		}

		error = reader.read_u32(r_header.format_version);
		if (error != OK) {
			return error;
		}
		if (r_header.format_version != FORMAT_VERSION) {
			return reader.fail(ERR_INVALID_DATA, vformat("Unsupported GDScript bytecode format_version %d (expected %d).", r_header.format_version, FORMAT_VERSION));
		}

		error = reader.read_raw(r_header.abi_id, 16);
		if (error != OK) {
			return error;
		}
		error = reader.read_u8(r_header.real_t_size);
		if (error != OK) {
			return error;
		}
		error = reader.read_u32(r_header.opcode_end);
		if (error != OK) {
			return error;
		}
		error = reader.read_utf8_string(r_header.diag_version_full_config, false);
		if (error != OK) {
			return error;
		}
		error = reader.read_utf8_string(r_header.diag_version_hash, false);
		if (error != OK) {
			return error;
		}

		// Hard ABI compatibility checks (diagnostic strings are read first so mismatch errors include them).
		if (memcmp(r_header.abi_id, GDSCRIPT_BYTECODE_ABI_ID, 16) != 0) {
			return reader.fail(ERR_INVALID_DATA, vformat("GDScript bytecode ABI mismatch (compiled with %s / %s, runtime is %s / %s).", r_header.diag_version_full_config, r_header.diag_version_hash, String(GODOT_VERSION_FULL_CONFIG), String(GODOT_VERSION_HASH)));
		}
		if (r_header.real_t_size != static_cast<uint8_t>(sizeof(real_t))) {
			return reader.fail(ERR_INVALID_DATA, vformat("GDScript bytecode real_t size mismatch (%d vs runtime %d).", r_header.real_t_size, static_cast<uint32_t>(sizeof(real_t))));
		}
		if (r_header.opcode_end != static_cast<uint32_t>(GDScriptFunction::OPCODE_END)) {
			return reader.fail(ERR_INVALID_DATA, vformat("GDScript bytecode OPCODE_END mismatch (%d vs runtime %d).", r_header.opcode_end, static_cast<uint32_t>(GDScriptFunction::OPCODE_END)));
		}

		error = reader.read_u32(r_header.script_flags);
		if (error != OK) {
			return error;
		}
		if ((r_header.script_flags & ~SCRIPT_FLAG_KNOWN_MASK) != 0) {
			return reader.fail(ERR_INVALID_DATA, vformat("Unknown ScriptFlags bits 0x%x.", r_header.script_flags));
		}

		return OK;
	}

	Error read_string_table_section() {
		uint32_t section_id = 0;
		Error error = reader.read_u32(section_id);
		if (error != OK) {
			return error;
		}
		if (section_id != SECTION_STRING_TABLE) {
			return reader.fail(ERR_INVALID_DATA, vformat("Expected SECTION_STRING_TABLE (0), got section %d.", section_id));
		}

		uint32_t string_count = 0;
		error = reader.read_u32(string_count);
		if (error != OK) {
			return error;
		}
		error = reader.check_wire_count(string_count, 4, sizeof(String), "SECTION_STRING_TABLE");
		if (error != OK) {
			return error;
		}

		string_table.resize(static_cast<int>(string_count));
		for (uint32_t index = 0; index < string_count; index++) {
			error = reader.read_utf8_string(string_table.write[index], true);
			if (error != OK) {
				return error;
			}
		}
		return OK;
	}

	Error read_dependencies_section() {
		uint32_t section_id = 0;
		Error error = reader.read_u32(section_id);
		if (error != OK) {
			return error;
		}
		if (section_id != SECTION_DEPENDENCIES) {
			return reader.fail(ERR_INVALID_DATA, vformat("Expected SECTION_DEPENDENCIES (1), got section %d.", section_id));
		}

		uint32_t dep_count = 0;
		error = reader.read_u32(dep_count);
		if (error != OK) {
			return error;
		}
		error = reader.check_wire_count(dep_count, 4, sizeof(String), "SECTION_DEPENDENCIES");
		if (error != OK) {
			return error;
		}

		dependencies.resize(static_cast<int>(dep_count));
		for (uint32_t index = 0; index < dep_count; index++) {
			error = read_string_index(dependencies.write[index]);
			if (error != OK) {
				return error;
			}
		}
		return OK;
	}

	Error read_skeleton_section(const Ref<GDScript> &p_target_script, const String &p_logical_path, bool p_resolve_external_bases) {
		uint32_t section_id = 0;
		Error error = reader.read_u32(section_id);
		if (error != OK) {
			return error;
		}
		if (section_id != SECTION_SKELETON) {
			return reader.fail(ERR_INVALID_DATA, vformat("Expected SECTION_SKELETON (2), got section %d.", section_id));
		}

		uint32_t expected_class_count = 0;
		error = reader.read_u32(expected_class_count);
		if (error != OK) {
			return error;
		}
		if (expected_class_count == 0) {
			return reader.fail(ERR_INVALID_DATA, "SECTION_SKELETON class_count cannot be 0.");
		}
		error = reader.check_wire_count(expected_class_count, 26, sizeof(Ref<GDScript>), "SECTION_SKELETON");
		if (error != OK) {
			return error;
		}

		if (!p_logical_path.is_empty()) {
			p_target_script->set_path_cache(p_logical_path);
		}
		p_target_script->valid = false;

		local_classes.clear();
		skeleton_bases.clear();

		error = read_skeleton_class(p_target_script, nullptr, p_target_script, 0);
		if (error != OK) {
			return error;
		}
		if (static_cast<uint32_t>(local_classes.size()) != expected_class_count) {
			return reader.fail(ERR_INVALID_DATA, vformat("SECTION_SKELETON class_count %d does not match decoded tree node count %d.", expected_class_count, local_classes.size()));
		}

		return link_base_classes(p_resolve_external_bases);
	}

	Error read_class_bodies_and_verify(String *r_error_message) {
		uint32_t section_id = 0;
		Error error = reader.read_u32(section_id);
		if (error != OK) {
			return error;
		}
		if (section_id != SECTION_CLASS_BODIES) {
			return reader.fail(ERR_INVALID_DATA, vformat("Expected SECTION_CLASS_BODIES (3), got section %d.", section_id));
		}

		auto rollback_bodies = [&]() {
			for (int index = 0; index < local_classes.size(); index++) {
				reset_script_body_fields(local_classes[index].ptr());
			}
		};

		for (int index = 0; index < local_classes.size(); index++) {
			error = read_class_body(local_classes[index].ptr());
			if (error != OK) {
				rollback_bodies();
				return error;
			}
		}

		if (reader.remaining() != 0) {
			rollback_bodies();
			return reader.fail(ERR_INVALID_DATA, vformat("Unexpected trailing bytes (%d) after SECTION_CLASS_BODIES.", static_cast<uint32_t>(reader.remaining())));
		}

		// Link-time verification across every class before marking any class valid.
		for (int index = 0; index < local_classes.size(); index++) {
			error = GDScriptBytecodeVerifier::verify_class(local_classes[index].ptr(), &global_store_offsets, r_error_message);
			if (error != OK) {
				rollback_bodies();
				return error;
			}
		}

		// Initialize static default values and mark valid only after every class passes verification.
		for (int index = 0; index < local_classes.size(); index++) {
			local_classes[index]->_static_default_init();
			local_classes[index]->valid = true;
		}

		return OK;
	}

	const Vector<String> &get_dependencies() const {
		return dependencies;
	}
};

Error GDScriptBytecodeLoader::check_header(
		const Vector<uint8_t> &p_buffer,
		HeaderInfo *r_header,
		String *r_error_message) {
	HeaderInfo local_header;
	LoadContext context(p_buffer, nullptr, r_error_message);
	Error error = context.read_header_and_flags(local_header);
	if (error != OK) {
		return error;
	}
	if (r_header != nullptr) {
		*r_header = local_header;
	}
	return OK;
}

Error GDScriptBytecodeLoader::read_dependencies(
		const Vector<uint8_t> &p_buffer,
		Vector<String> &r_dependencies,
		String *r_error_message) {
	r_dependencies.clear();
	HeaderInfo local_header;
	LoadContext context(p_buffer, nullptr, r_error_message);
	Error error = context.read_header_and_flags(local_header);
	if (error != OK) {
		return error;
	}
	error = context.read_string_table_section();
	if (error != OK) {
		return error;
	}
	error = context.read_dependencies_section();
	if (error != OK) {
		return error;
	}
	r_dependencies = context.get_dependencies();
	return OK;
}

Error GDScriptBytecodeLoader::load_skeleton(
		const Vector<uint8_t> &p_buffer,
		const Ref<GDScript> &p_target_script,
		const String &p_logical_path,
		HeaderInfo *r_header,
		String *r_error_message) {
	if (p_target_script.is_null()) {
		if (r_error_message != nullptr) {
			*r_error_message = "Target GDScript reference is null.";
		}
		return ERR_INVALID_PARAMETER;
	}

	HeaderInfo local_header;
	LoadContext context(p_buffer, nullptr, r_error_message);
	Error error = context.read_header_and_flags(local_header);
	if (error != OK) {
		return error;
	}
	error = context.read_string_table_section();
	if (error != OK) {
		return error;
	}
	error = context.read_dependencies_section();
	if (error != OK) {
		return error;
	}
	error = context.read_skeleton_section(p_target_script, p_logical_path, false);
	if (error != OK) {
		return error;
	}
	if (r_header != nullptr) {
		*r_header = local_header;
	}
	return OK;
}

Error GDScriptBytecodeLoader::load_full(
		const Vector<uint8_t> &p_buffer,
		const Ref<GDScript> &p_target_script,
		const String &p_logical_path,
		GDScriptBytecodeExternalResolver *p_resolver,
		HeaderInfo *r_header,
		String *r_error_message) {
	if (p_target_script.is_null()) {
		if (r_error_message != nullptr) {
			*r_error_message = "Target GDScript reference is null.";
		}
		return ERR_INVALID_PARAMETER;
	}

	HeaderInfo local_header;
	LoadContext context(p_buffer, p_resolver, r_error_message);
	Error error = context.read_header_and_flags(local_header);
	if (error != OK) {
		return error;
	}
	error = context.read_string_table_section();
	if (error != OK) {
		return error;
	}
	error = context.read_dependencies_section();
	if (error != OK) {
		return error;
	}
	error = context.read_skeleton_section(p_target_script, p_logical_path, true);
	if (error != OK) {
		return error;
	}
	error = context.read_class_bodies_and_verify(r_error_message);
	if (error != OK) {
		return error;
	}
	if (r_header != nullptr) {
		*r_header = local_header;
	}
	return OK;
}

Error GDScriptBytecodeLoader::initialize_static_data(const Ref<GDScript> &p_script) {
	ERR_FAIL_COND_V(p_script.is_null(), ERR_INVALID_PARAMETER);
	return p_script->_static_init();
}

Ref<Resource> GDScriptBytecodeCacheResolver::resolve_resource(const String &p_path, const String &p_type_hint, Error &r_error) {
	r_error = OK;
	if (p_path.is_empty()) {
		r_error = ERR_INVALID_PARAMETER;
		return Ref<Resource>();
	}

	Ref<Resource> resource = ResourceLoader::load(p_path, p_type_hint, ResourceFormatLoader::CACHE_MODE_REUSE, &r_error);
	if (r_error != OK || resource.is_null()) {
		if (r_error == OK) {
			r_error = ERR_CANT_RESOLVE;
		}
		return Ref<Resource>();
	}
	return resource;
}

Ref<GDScript> GDScriptBytecodeCacheResolver::resolve_script(const String &p_path, const String &p_fully_qualified_name, bool &r_is_local_class, Error &r_error) {
	r_is_local_class = false;
	r_error = OK;
	if (p_path.is_empty()) {
		r_error = ERR_INVALID_PARAMETER;
		return Ref<GDScript>();
	}

	Ref<GDScript> root_script;
	if (ResourceLoader::is_within_load()) {
		Ref<Resource> loaded_resource = ResourceLoader::load(p_path, "GDScript", ResourceFormatLoader::CACHE_MODE_REUSE, &r_error);
		root_script = loaded_resource;
	} else {
		root_script = GDScriptCache::get_full_script(p_path, r_error, owner_path);
	}

	if (r_error != OK || root_script.is_null()) {
		if (r_error == OK) {
			r_error = ERR_CANT_RESOLVE;
		}
		return Ref<GDScript>();
	}

	// Accept an unvalidated shell ONLY when it belongs to an in-flight load on the same load chain.
	// Failed non-reloading scripts must never be linked into bytecode.
	if (!root_script->is_valid() && !GDScriptCache::is_script_loading_on_current_chain(p_path, root_script.ptr())) {
		r_error = ERR_CANT_RESOLVE;
		return Ref<GDScript>();
	}

	if (p_fully_qualified_name.is_empty() || p_fully_qualified_name == root_script->get_fully_qualified_name()) {
		return root_script;
	}

	GDScript *resolved_class = root_script->find_class(p_fully_qualified_name);
	if (resolved_class == nullptr) {
		const String root_fqcn = root_script->get_fully_qualified_name();
		if (!root_fqcn.is_empty() && p_fully_qualified_name.begins_with(root_fqcn + "::")) {
			resolved_class = root_script->find_class(p_fully_qualified_name.trim_prefix(root_fqcn));
		}
	}
	if (resolved_class == nullptr) {
		const String canonical_root_path = GDScript::canonicalize_path(root_script->get_script_path());
		if (!canonical_root_path.is_empty() && p_fully_qualified_name.begins_with(canonical_root_path + "::")) {
			resolved_class = root_script->find_class(p_fully_qualified_name.trim_prefix(canonical_root_path));
		}
	}

	if (resolved_class == nullptr) {
		r_error = ERR_CANT_RESOLVE;
		return Ref<GDScript>();
	}

	return Ref<GDScript>(resolved_class);
}
