/**************************************************************************/
/*  gdscript_bytecode_export.cpp                                          */
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

#include "gdscript_bytecode_export.h"

#ifdef GDSCRIPT_BYTECODE_EXPORT_ENABLED

#include "gdscript_bytecode_abi.gen.h"
#include "gdscript_utility_callable.h"

#include "core/config/engine.h"
#include "core/io/marshalls.h"
#include "core/object/class_db.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/variant/container_type_validate.h"
#include "core/version.h"

namespace {

using namespace GDScriptBytecodeFormat;

struct StringBytewiseComparator {
	_FORCE_INLINE_ bool operator()(const String &p_left, const String &p_right) const {
		return p_left < p_right;
	}
};

struct StringNameBytewiseComparator {
	_FORCE_INLINE_ bool operator()(const StringName &p_left, const StringName &p_right) const {
		return String(p_left) < String(p_right);
	}
};

struct GlobalStoreOffsetComparator {
	_FORCE_INLINE_ bool operator()(const GDScriptFunction::ExportFixups::GlobalStoreFixup &p_left, const GDScriptFunction::ExportFixups::GlobalStoreFixup &p_right) const {
		return p_left.code_offset < p_right.code_offset;
	}
};

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

class BytecodeWriter {
	Vector<uint8_t> bytes;

public:
	void write_u8(uint8_t p_value) {
		bytes.push_back(p_value);
	}

	void write_bool(bool p_value) {
		write_u8(p_value ? 1u : 0u);
	}

	void write_u32(uint32_t p_value) {
		const int old_size = bytes.size();
		bytes.resize(old_size + 4);
		encode_uint32(p_value, bytes.ptrw() + old_size);
	}

	void write_i32(int32_t p_value) {
		write_u32(static_cast<uint32_t>(p_value));
	}

	void write_raw(const uint8_t *p_data, int p_length) {
		if (p_length <= 0) {
			return;
		}
		const int old_size = bytes.size();
		bytes.resize(old_size + p_length);
		memcpy(bytes.ptrw() + old_size, p_data, static_cast<size_t>(p_length));
	}

	void write_utf8_string(const String &p_value) {
		const CharString utf8 = p_value.utf8();
		const int byte_length = utf8.length();
		write_u32(static_cast<uint32_t>(byte_length));
		write_raw(reinterpret_cast<const uint8_t *>(utf8.get_data()), byte_length);
	}

	const Vector<uint8_t> &get_bytes() const { return bytes; }
};

} // namespace

class GDScriptBytecodeExporter::ExportContext {
	const GDScript *root_script = nullptr;
	GDScriptBytecodeExporter::Options options;
	String *error_message = nullptr;

	Vector<const GDScript *> local_classes;
	HashMap<const GDScript *, uint32_t> local_class_indices;
	HashMap<const Object *, StringName> engine_singleton_names;

	Vector<String> string_table;
	HashMap<String, uint32_t> string_table_indices;
	HashSet<String> external_dependencies;

	Error fail(Error p_error, const String &p_message) {
		if (error_message != nullptr) {
			*error_message = p_message;
		}
		return p_error;
	}

	uint32_t intern_string(const String &p_value) {
		HashMap<String, uint32_t>::ConstIterator existing = string_table_indices.find(p_value);
		if (existing) {
			return existing->value;
		}
		const uint32_t new_index = static_cast<uint32_t>(string_table.size());
		string_table.push_back(p_value);
		string_table_indices.insert(p_value, new_index);
		return new_index;
	}

	uint32_t intern_string_name(const StringName &p_value) {
		return intern_string(String(p_value));
	}

	void record_dependency(const String &p_path) {
		if (!p_path.is_empty()) {
			external_dependencies.insert(p_path);
		}
	}

	Error collect_local_classes(const GDScript *p_script, uint32_t p_depth) {
		if (p_script == nullptr) {
			return fail(ERR_INVALID_DATA, "Cannot export null GDScript class.");
		}
		if (p_depth > MAX_CLASS_DEPTH) {
			return fail(ERR_INVALID_DATA, "GDScript subclass nesting exceeds MAX_CLASS_DEPTH.");
		}
		if (local_class_indices.has(p_script)) {
			return fail(ERR_INVALID_DATA, "Duplicate class pointer encountered in local class tree.");
		}

		const uint32_t class_index = static_cast<uint32_t>(local_classes.size());
		local_classes.push_back(p_script);
		local_class_indices.insert(p_script, class_index);

		Vector<StringName> sorted_subclass_keys;
		sorted_subclass_keys.resize(p_script->subclasses.size());
		int write_index = 0;
		for (const KeyValue<StringName, Ref<GDScript>> &entry : p_script->subclasses) {
			sorted_subclass_keys.write[write_index++] = entry.key;
		}
		sorted_subclass_keys.sort_custom<StringNameBytewiseComparator>();

		for (const StringName &subclass_key : sorted_subclass_keys) {
			const Ref<GDScript> &subclass = p_script->subclasses[subclass_key];
			Error error = collect_local_classes(subclass.ptr(), p_depth + 1);
			if (error != OK) {
				return error;
			}
		}
		return OK;
	}

	Error write_script_ref(BytecodeWriter &r_writer, const GDScript *p_script) {
		if (p_script == nullptr) {
			return fail(ERR_INVALID_DATA, "Cannot serialize null GDScript reference.");
		}
		HashMap<const GDScript *, uint32_t>::ConstIterator local_entry = local_class_indices.find(p_script);
		if (local_entry) {
			r_writer.write_u8(SCRIPT_REF_LOCAL);
			r_writer.write_u32(local_entry->value);
			return OK;
		}

		const String external_path = p_script->get_script_path();
		if (external_path.is_empty()) {
			return fail(ERR_INVALID_DATA, vformat("External GDScript \"%s\" has no resource path.", p_script->get_fully_qualified_name()));
		}
		record_dependency(external_path);
		r_writer.write_u8(SCRIPT_REF_EXTERNAL);
		r_writer.write_u32(intern_string(external_path));
		r_writer.write_u32(intern_string(p_script->get_fully_qualified_name()));
		return OK;
	}

	Error write_skeleton_class(BytecodeWriter &r_writer, const GDScript *p_script, uint32_t p_depth) {
		if (p_depth > MAX_CLASS_DEPTH) {
			return fail(ERR_INVALID_DATA, "GDScript subclass depth exceeds MAX_CLASS_DEPTH.");
		}
		if (p_script->native.is_null() || p_script->native->get_name() == StringName()) {
			return fail(ERR_INVALID_DATA, vformat("Class \"%s\" has no valid native base class.", p_script->fully_qualified_name));
		}

		r_writer.write_u32(intern_string(p_script->fully_qualified_name));
		r_writer.write_u32(intern_string_name(p_script->local_name));
		r_writer.write_u32(intern_string_name(p_script->global_name));
		r_writer.write_u32(intern_string(p_script->simplified_icon_path));
		r_writer.write_u32(intern_string_name(p_script->native->get_name()));

		uint8_t class_flags = 0;
		if (p_script->tool) {
			class_flags |= CLASS_FLAG_TOOL;
		}
		if (p_script->_is_abstract) {
			class_flags |= CLASS_FLAG_ABSTRACT;
		}
		r_writer.write_u8(class_flags);

		if (p_script->base.is_null()) {
			r_writer.write_u8(BASE_CLASS_NATIVE_ONLY);
		} else {
			HashMap<const GDScript *, uint32_t>::ConstIterator local_base = local_class_indices.find(p_script->base.ptr());
			if (local_base) {
				r_writer.write_u8(BASE_CLASS_LOCAL);
				r_writer.write_u32(local_base->value);
			} else {
				const String base_path = p_script->base->get_script_path();
				if (base_path.is_empty()) {
					return fail(ERR_INVALID_DATA, vformat("External base script \"%s\" of \"%s\" has no resource path.", p_script->base->get_fully_qualified_name(), p_script->fully_qualified_name));
				}
				record_dependency(base_path);
				r_writer.write_u8(BASE_CLASS_EXTERNAL);
				r_writer.write_u32(intern_string(base_path));
				r_writer.write_u32(intern_string(p_script->base->get_fully_qualified_name()));
			}
		}

		Vector<StringName> sorted_subclass_keys;
		sorted_subclass_keys.resize(p_script->subclasses.size());
		int write_index = 0;
		for (const KeyValue<StringName, Ref<GDScript>> &entry : p_script->subclasses) {
			sorted_subclass_keys.write[write_index++] = entry.key;
		}
		sorted_subclass_keys.sort_custom<StringNameBytewiseComparator>();

		r_writer.write_u32(static_cast<uint32_t>(sorted_subclass_keys.size()));
		for (const StringName &subclass_key : sorted_subclass_keys) {
			r_writer.write_u32(intern_string_name(subclass_key));
			Error error = write_skeleton_class(r_writer, p_script->subclasses[subclass_key].ptr(), p_depth + 1);
			if (error != OK) {
				return error;
			}
		}

		return OK;
	}

	Error write_property_info(BytecodeWriter &r_writer, const PropertyInfo &p_info) {
		if (p_info.type < Variant::NIL || p_info.type >= Variant::VARIANT_MAX) {
			return fail(ERR_INVALID_DATA, vformat("PropertyInfo \"%s\" has invalid Variant::Type.", p_info.name));
		}
		if (p_info.hint < PROPERTY_HINT_NONE || p_info.hint >= PROPERTY_HINT_MAX) {
			return fail(ERR_INVALID_DATA, vformat("PropertyInfo \"%s\" has invalid PropertyHint.", p_info.name));
		}

		r_writer.write_u32(static_cast<uint32_t>(p_info.type));
		r_writer.write_u32(intern_string(p_info.name));
		r_writer.write_u32(intern_string_name(p_info.class_name));
		r_writer.write_u32(static_cast<uint32_t>(p_info.hint));
		r_writer.write_u32(intern_string(p_info.hint_string));
		r_writer.write_u32(p_info.usage);
		return OK;
	}

	Error write_method_info(BytecodeWriter &r_writer, const MethodInfo &p_info) {
		if (p_info.default_arguments.size() > p_info.arguments.size()) {
			return fail(ERR_INVALID_DATA, vformat("MethodInfo \"%s\" has more default_arguments than arguments.", p_info.name));
		}
		if (!p_info.arguments_metadata.is_empty() && p_info.arguments_metadata.size() != p_info.arguments.size()) {
			return fail(ERR_INVALID_DATA, vformat("MethodInfo \"%s\" has mismatched arguments_metadata count.", p_info.name));
		}

		r_writer.write_u32(intern_string(p_info.name));
		Error error = write_property_info(r_writer, p_info.return_val);
		if (error != OK) {
			return error;
		}
		r_writer.write_u32(p_info.flags);
		r_writer.write_i32(static_cast<int32_t>(p_info.id));

		r_writer.write_u32(static_cast<uint32_t>(p_info.arguments.size()));
		for (int index = 0; index < p_info.arguments.size(); index++) {
			error = write_property_info(r_writer, p_info.arguments[index]);
			if (error != OK) {
				return error;
			}
		}

		r_writer.write_u32(static_cast<uint32_t>(p_info.default_arguments.size()));
		for (int index = 0; index < p_info.default_arguments.size(); index++) {
			error = write_tagged_variant(r_writer, p_info.default_arguments[index], 0, false);
			if (error != OK) {
				return error;
			}
		}

		r_writer.write_i32(static_cast<int32_t>(p_info.return_val_metadata));
		r_writer.write_u32(static_cast<uint32_t>(p_info.arguments_metadata.size()));
		for (int index = 0; index < p_info.arguments_metadata.size(); index++) {
			r_writer.write_i32(static_cast<int32_t>(p_info.arguments_metadata[index]));
		}

		return OK;
	}

	Error write_container_type(BytecodeWriter &r_writer, const ContainerType &p_type) {
		if (p_type.builtin_type < Variant::NIL || p_type.builtin_type >= Variant::VARIANT_MAX) {
			return fail(ERR_INVALID_DATA, "ContainerType has invalid builtin_type.");
		}
		r_writer.write_u32(static_cast<uint32_t>(p_type.builtin_type));
		r_writer.write_u32(intern_string_name(p_type.class_name));
		if (p_type.script.is_valid()) {
			r_writer.write_bool(true);
			if (const GDScript *gdscript = Object::cast_to<GDScript>(p_type.script.ptr())) {
				r_writer.write_u8(TAG_SCRIPT_REF);
				return write_script_ref(r_writer, gdscript);
			}
			const String script_path = p_type.script->get_path();
			if (script_path.is_empty()) {
				return fail(ERR_INVALID_DATA, "ContainerType references an external Script without a resource path.");
			}
			record_dependency(script_path);
			r_writer.write_u8(TAG_EXTERNAL_SCRIPT);
			r_writer.write_u32(intern_string(script_path));
			return OK;
		}
		r_writer.write_bool(false);
		return OK;
	}

	Error write_data_type(BytecodeWriter &r_writer, const GDScriptDataType &p_type, uint32_t p_depth, bool p_allow_void_return) {
		if (p_depth > MAX_TYPE_DEPTH) {
			return fail(ERR_INVALID_DATA, "GDScriptDataType nesting exceeds MAX_TYPE_DEPTH.");
		}
		if (p_type.builtin_type < Variant::NIL || p_type.builtin_type >= Variant::VARIANT_MAX) {
			return fail(ERR_INVALID_DATA, "GDScriptDataType has invalid builtin_type.");
		}

		Vector<GDScriptDataType> element_types = p_type.container_element_types;

		switch (p_type.kind) {
			case GDScriptDataType::VARIANT:
				if (p_type.builtin_type != Variant::NIL || p_type.native_type != StringName() || !element_types.is_empty()) {
					return fail(ERR_INVALID_DATA, "Invalid VARIANT GDScriptDataType shape.");
				}
				break;
			case GDScriptDataType::BUILTIN:
				if (p_type.builtin_type == Variant::OBJECT) {
					return fail(ERR_INVALID_DATA, "Invalid BUILTIN GDScriptDataType with Variant::OBJECT.");
				}
				if (p_type.builtin_type == Variant::NIL && !p_allow_void_return) {
					return fail(ERR_INVALID_DATA, "Invalid BUILTIN GDScriptDataType with Variant::NIL outside return_type.");
				}
				if (p_type.native_type != StringName()) {
					return fail(ERR_INVALID_DATA, "Invalid BUILTIN GDScriptDataType with non-empty native_type.");
				}
				if (p_type.builtin_type == Variant::ARRAY) {
					if (element_types.size() > 1) {
						return fail(ERR_INVALID_DATA, "Invalid ARRAY GDScriptDataType element count.");
					}
				} else if (p_type.builtin_type == Variant::DICTIONARY) {
					if (element_types.size() == 1) {
						// Normalize key-only typed dictionaries (e.g. Dictionary[int, Variant]) to 2 entries.
						element_types.push_back(GDScriptDataType());
					}
					if (element_types.size() != 0 && element_types.size() != 2) {
						return fail(ERR_INVALID_DATA, "Invalid DICTIONARY GDScriptDataType element count.");
					}
				} else if (!element_types.is_empty()) {
					return fail(ERR_INVALID_DATA, "Invalid non-container BUILTIN GDScriptDataType with element types.");
				}
				break;
			case GDScriptDataType::NATIVE:
				if (p_type.builtin_type != Variant::OBJECT || p_type.native_type == StringName() || !element_types.is_empty()) {
					return fail(ERR_INVALID_DATA, "Invalid NATIVE GDScriptDataType shape.");
				}
				break;
			case GDScriptDataType::SCRIPT:
				if (p_type.builtin_type != Variant::OBJECT || !element_types.is_empty()) {
					return fail(ERR_INVALID_DATA, "Invalid SCRIPT GDScriptDataType shape.");
				}
				if (p_type.script_type == nullptr || p_type.script_type->get_path().is_empty()) {
					return fail(ERR_INVALID_DATA, "SCRIPT GDScriptDataType references a null or pathless Script.");
				}
				break;
			case GDScriptDataType::GDSCRIPT:
				if (p_type.builtin_type != Variant::OBJECT || !element_types.is_empty()) {
					return fail(ERR_INVALID_DATA, "Invalid GDSCRIPT GDScriptDataType shape.");
				}
				if (Object::cast_to<GDScript>(p_type.script_type) == nullptr) {
					return fail(ERR_INVALID_DATA, "GDSCRIPT GDScriptDataType does not reference a GDScript.");
				}
				break;
			default:
				return fail(ERR_INVALID_DATA, "Unknown GDScriptDataType::Kind.");
		}

		r_writer.write_u8(static_cast<uint8_t>(p_type.kind));
		r_writer.write_u32(static_cast<uint32_t>(p_type.builtin_type));
		r_writer.write_u32(intern_string_name(p_type.native_type));

		if (p_type.kind == GDScriptDataType::GDSCRIPT) {
			const GDScript *referenced_script = Object::cast_to<GDScript>(p_type.script_type);
			Error error = write_script_ref(r_writer, referenced_script);
			if (error != OK) {
				return error;
			}
		} else if (p_type.kind == GDScriptDataType::SCRIPT) {
			const String script_path = p_type.script_type->get_path();
			record_dependency(script_path);
			r_writer.write_u32(intern_string(script_path));
		}

		r_writer.write_u32(static_cast<uint32_t>(element_types.size()));
		for (int index = 0; index < element_types.size(); index++) {
			Error error = write_data_type(r_writer, element_types[index], p_depth + 1, false);
			if (error != OK) {
				return error;
			}
		}

		return OK;
	}

	Error write_tagged_variant(BytecodeWriter &r_writer, const Variant &p_variant, int p_depth, bool p_sort_string_dict_keys) {
		if (p_depth > Variant::MAX_RECURSION_DEPTH) {
			return fail(ERR_INVALID_DATA, "Variant nesting exceeds Variant::MAX_RECURSION_DEPTH.");
		}

		const Variant::Type variant_type = p_variant.get_type();
		if (is_allowed_inline_variant_type(variant_type)) {
			int encoded_length = 0;
			Error size_error = encode_variant(p_variant, nullptr, encoded_length, false);
			if (size_error != OK || encoded_length < 4) {
				return fail(ERR_INVALID_DATA, vformat("Failed to measure inline Variant of type %s.", Variant::get_type_name(variant_type)));
			}
			Vector<uint8_t> encoded_bytes;
			encoded_bytes.resize(encoded_length);
			int written_length = 0;
			Error encode_error = encode_variant(p_variant, encoded_bytes.ptrw(), written_length, false);
			if (encode_error != OK || written_length != encoded_length) {
				return fail(ERR_INVALID_DATA, vformat("Failed to encode inline Variant of type %s.", Variant::get_type_name(variant_type)));
			}
			r_writer.write_u8(TAG_INLINE_VARIANT);
			r_writer.write_u32(static_cast<uint32_t>(written_length));
			r_writer.write_raw(encoded_bytes.ptr(), written_length);
			return OK;
		}

		switch (variant_type) {
			case Variant::ARRAY: {
				const Array array = p_variant;
				r_writer.write_u8(TAG_ARRAY);
				r_writer.write_bool(array.is_read_only());
				Error type_error = write_container_type(r_writer, array.get_element_type());
				if (type_error != OK) {
					return type_error;
				}
				const int array_size = array.size();
				r_writer.write_u32(static_cast<uint32_t>(array_size));
				for (int index = 0; index < array_size; index++) {
					Error element_error = write_tagged_variant(r_writer, array[index], p_depth + 1, false);
					if (element_error != OK) {
						return element_error;
					}
				}
				return OK;
			}
			case Variant::DICTIONARY: {
				const Dictionary dictionary = p_variant;
				r_writer.write_u8(TAG_DICTIONARY);
				r_writer.write_bool(dictionary.is_read_only());
				Error key_type_error = write_container_type(r_writer, dictionary.get_key_type());
				if (key_type_error != OK) {
					return key_type_error;
				}
				Error value_type_error = write_container_type(r_writer, dictionary.get_value_type());
				if (value_type_error != OK) {
					return value_type_error;
				}

				const LocalVector<Variant> raw_keys = dictionary.get_key_list();
				Vector<Variant> ordered_keys;
				ordered_keys.resize(raw_keys.size());
				for (uint32_t index = 0; index < raw_keys.size(); index++) {
					ordered_keys.write[index] = raw_keys[index];
				}

				if (p_sort_string_dict_keys && ordered_keys.size() > 1) {
					bool all_string_like = true;
					for (int index = 0; index < ordered_keys.size(); index++) {
						const Variant::Type key_type = ordered_keys[index].get_type();
						if (key_type != Variant::STRING && key_type != Variant::STRING_NAME) {
							all_string_like = false;
							break;
						}
					}
					if (all_string_like) {
						struct VariantStringKeyComparator {
							_FORCE_INLINE_ bool operator()(const Variant &p_left, const Variant &p_right) const {
								const String left_string = p_left;
								const String right_string = p_right;
								if (left_string != right_string) {
									return left_string < right_string;
								}
								return p_left.get_type() < p_right.get_type();
							}
						};
						ordered_keys.sort_custom<VariantStringKeyComparator>();
					}
				}

				r_writer.write_u32(static_cast<uint32_t>(ordered_keys.size()));
				for (int index = 0; index < ordered_keys.size(); index++) {
					const Variant &key = ordered_keys[index];
					Error key_error = write_tagged_variant(r_writer, key, p_depth + 1, p_sort_string_dict_keys);
					if (key_error != OK) {
						return key_error;
					}
					Error value_error = write_tagged_variant(r_writer, dictionary[key], p_depth + 1, p_sort_string_dict_keys);
					if (value_error != OK) {
						return value_error;
					}
				}
				return OK;
			}
			case Variant::OBJECT: {
				bool was_freed = false;
				Object *object = p_variant.get_validated_object_with_check(was_freed);
				if (was_freed) {
					return fail(ERR_INVALID_DATA, "Cannot serialize a previously freed Object in bytecode.");
				}
				if (object == nullptr) {
					r_writer.write_u8(TAG_NULL_OBJECT);
					return OK;
				}
				if (const GDScript *gdscript = Object::cast_to<GDScript>(object)) {
					r_writer.write_u8(TAG_SCRIPT_REF);
					return write_script_ref(r_writer, gdscript);
				}
				if (const Script *other_script = Object::cast_to<Script>(object)) {
					const String script_path = other_script->get_path();
					if (script_path.is_empty()) {
						return fail(ERR_INVALID_DATA, "Cannot serialize an external Script without a resource path.");
					}
					record_dependency(script_path);
					r_writer.write_u8(TAG_EXTERNAL_SCRIPT);
					r_writer.write_u32(intern_string(script_path));
					return OK;
				}
				if (const GDScriptNativeClass *native_class = Object::cast_to<GDScriptNativeClass>(object)) {
					r_writer.write_u8(TAG_NATIVE_CLASS);
					r_writer.write_u32(intern_string_name(native_class->get_name()));
					return OK;
				}
				HashMap<const Object *, StringName>::ConstIterator singleton_entry = engine_singleton_names.find(object);
				if (singleton_entry) {
					r_writer.write_u8(TAG_ENGINE_SINGLETON);
					r_writer.write_u32(intern_string_name(singleton_entry->value));
					return OK;
				}
				if (const Resource *resource = Object::cast_to<Resource>(object)) {
					const String resource_path = resource->get_path();
					if (resource_path.is_empty()) {
						return fail(ERR_INVALID_DATA, vformat("Cannot serialize unnamed Resource of class \"%s\" in bytecode.", resource->get_class()));
					}
					record_dependency(resource_path);
					r_writer.write_u8(TAG_EXTERNAL_RESOURCE);
					r_writer.write_u32(intern_string(resource_path));
					return OK;
				}
				return fail(ERR_INVALID_DATA, vformat("Cannot serialize live Object instance of class \"%s\" in bytecode.", object->get_class()));
			}
			case Variant::CALLABLE: {
				const Callable callable = p_variant;
				if (callable.is_null()) {
					r_writer.write_u8(TAG_DEFAULT_VALUE);
					r_writer.write_u32(static_cast<uint32_t>(Variant::CALLABLE));
					return OK;
				}
				const GDScriptUtilityCallable *utility_callable = GDScriptUtilityCallable::get_from_callable(callable);
				if (utility_callable != nullptr && utility_callable->is_valid()) {
					r_writer.write_u8(TAG_UTILITY_CALLABLE);
					r_writer.write_u32(intern_string_name(utility_callable->get_method()));
					return OK;
				}
				return fail(ERR_INVALID_DATA, "Cannot serialize non-utility live Callable in bytecode.");
			}
			case Variant::SIGNAL: {
				const Signal signal = p_variant;
				if (signal.is_null()) {
					r_writer.write_u8(TAG_DEFAULT_VALUE);
					r_writer.write_u32(static_cast<uint32_t>(Variant::SIGNAL));
					return OK;
				}
				return fail(ERR_INVALID_DATA, "Cannot serialize live Signal in bytecode.");
			}
			case Variant::RID: {
				const RID rid = p_variant;
				if (!rid.is_valid()) {
					r_writer.write_u8(TAG_DEFAULT_VALUE);
					r_writer.write_u32(static_cast<uint32_t>(Variant::RID));
					return OK;
				}
				return fail(ERR_INVALID_DATA, "Cannot serialize valid live RID in bytecode.");
			}
			default:
				return fail(ERR_INVALID_DATA, vformat("Unsupported Variant type %d in bytecode export.", static_cast<int>(variant_type)));
		}
	}

	Error write_member_info_entry(BytecodeWriter &r_writer, const StringName &p_name, const GDScript::MemberInfo &p_info) {
		if (p_info.index < 0) {
			return fail(ERR_INVALID_DATA, vformat("MemberInfo \"%s\" has negative index.", String(p_name)));
		}
		r_writer.write_u32(intern_string_name(p_name));
		r_writer.write_i32(static_cast<int32_t>(p_info.index));
		r_writer.write_u32(intern_string_name(p_info.setter));
		r_writer.write_u32(intern_string_name(p_info.getter));
		Error type_error = write_data_type(r_writer, p_info.data_type, 0, false);
		if (type_error != OK) {
			return type_error;
		}
		return write_property_info(r_writer, p_info.property_info);
	}

	Error write_function_record(BytecodeWriter &r_writer, const GDScript *p_owning_script, const GDScriptFunction *p_function, uint32_t p_lambda_depth) {
		if (p_function == nullptr) {
			return fail(ERR_INVALID_DATA, "Cannot serialize null GDScriptFunction.");
		}
		if (p_lambda_depth > MAX_LAMBDA_DEPTH) {
			return fail(ERR_INVALID_DATA, "Lambda nesting exceeds MAX_LAMBDA_DEPTH.");
		}

		const GDScriptFunction::ExportFixups &fixups = p_function->export_fixups;
		if (fixups.operators.size() != p_function->operator_funcs.size() ||
				fixups.setters.size() != p_function->setters.size() ||
				fixups.getters.size() != p_function->getters.size() ||
				fixups.keyed_setters.size() != p_function->keyed_setters.size() ||
				fixups.keyed_getters.size() != p_function->keyed_getters.size() ||
				fixups.indexed_setters.size() != p_function->indexed_setters.size() ||
				fixups.indexed_getters.size() != p_function->indexed_getters.size() ||
				fixups.builtin_methods.size() != p_function->builtin_methods.size() ||
				fixups.constructors.size() != p_function->constructors.size() ||
				fixups.utilities.size() != p_function->utilities.size() ||
				fixups.gds_utilities.size() != p_function->gds_utilities.size() ||
				fixups.method_binds.size() != p_function->methods.size()) {
			return fail(ERR_INVALID_DATA, vformat("Function \"%s\" ExportFixups tables do not match pointer tables.", String(p_function->name)));
		}

		if (p_function->argument_types.size() != p_function->_argument_count) {
			return fail(ERR_INVALID_DATA, vformat("Function \"%s\" argument_types size does not match argument_count.", String(p_function->name)));
		}
		const int expected_default_jumps = p_function->_default_arg_count == 0 ? 0 : (p_function->_default_arg_count + 1);
		if (p_function->default_arguments.size() != expected_default_jumps) {
			return fail(ERR_INVALID_DATA, vformat("Function \"%s\" default_arguments size mismatch.", String(p_function->name)));
		}
		if (p_function->code.is_empty() || static_cast<uint32_t>(p_function->code.size()) > MAX_CODE_WORDS) {
			return fail(ERR_INVALID_DATA, vformat("Function \"%s\" code size out of bounds.", String(p_function->name)));
		}

		r_writer.write_u32(intern_string_name(p_function->name));
		r_writer.write_bool(p_function->_static);
		const int32_t serialized_initial_line = (options.release_profile && !options.emit_line_opcodes) ? 0 : MAX(0, p_function->_initial_line);
		r_writer.write_i32(serialized_initial_line);
		r_writer.write_i32(static_cast<int32_t>(p_function->_argument_count));
		r_writer.write_i32(static_cast<int32_t>(p_function->_vararg_index));
		r_writer.write_i32(static_cast<int32_t>(p_function->_stack_size));
		r_writer.write_i32(static_cast<int32_t>(p_function->_instruction_args_size));

		r_writer.write_u32(static_cast<uint32_t>(p_function->argument_types.size()));
		for (int index = 0; index < p_function->argument_types.size(); index++) {
			Error arg_type_error = write_data_type(r_writer, p_function->argument_types[index], 0, false);
			if (arg_type_error != OK) {
				return arg_type_error;
			}
		}

		Error return_type_error = write_data_type(r_writer, p_function->return_type, 0, true);
		if (return_type_error != OK) {
			return return_type_error;
		}

		Error method_info_error = write_method_info(r_writer, p_function->method_info);
		if (method_info_error != OK) {
			return method_info_error;
		}

		Error rpc_error = write_tagged_variant(r_writer, p_function->rpc_config, 0, true);
		if (rpc_error != OK) {
			return rpc_error;
		}

		r_writer.write_u32(static_cast<uint32_t>(p_function->temporary_slots.size()));
		for (const Pair<int, Variant::Type> &slot : p_function->temporary_slots) {
			r_writer.write_i32(static_cast<int32_t>(slot.first));
			r_writer.write_u32(static_cast<uint32_t>(slot.second));
		}

		// Sanitize code stream: mask OPCODE_STORE_GLOBAL operands to 0 and zero OPCODE_OPERATOR inline-cache words.
		Vector<int32_t> sanitized_code;
		sanitized_code.resize(p_function->code.size());
		for (int index = 0; index < p_function->code.size(); index++) {
			sanitized_code.write[index] = static_cast<int32_t>(p_function->code[index]);
		}

		Vector<GDScriptFunction::ExportFixups::GlobalStoreFixup> sorted_global_stores = fixups.global_stores;
		sorted_global_stores.sort_custom<GlobalStoreOffsetComparator>();
		for (const GDScriptFunction::ExportFixups::GlobalStoreFixup &store_fixup : sorted_global_stores) {
			if (store_fixup.global_name == StringName()) {
				return fail(ERR_INVALID_DATA, vformat("Function \"%s\" has global store fixup without global_name at offset %d.", String(p_function->name), store_fixup.code_offset));
			}
			if (store_fixup.code_offset < 2 || store_fixup.code_offset >= sanitized_code.size() || sanitized_code[store_fixup.code_offset - 2] != GDScriptFunction::OPCODE_STORE_GLOBAL) {
				return fail(ERR_INVALID_DATA, vformat("Function \"%s\" global store fixup offset %d does not point to OPCODE_STORE_GLOBAL.", String(p_function->name), store_fixup.code_offset));
			}
			sanitized_code.write[store_fixup.code_offset] = 0;
		}

		for (int operator_offset : fixups.operator_cache_offsets) {
			const int cache_end = operator_offset + 7 + OPERATOR_INLINE_CACHE_PTR_WORDS;
			if (operator_offset < 0 || cache_end > sanitized_code.size() || sanitized_code[operator_offset] != GDScriptFunction::OPCODE_OPERATOR) {
				return fail(ERR_INVALID_DATA, vformat("Function \"%s\" operator_cache_offset %d does not point to OPCODE_OPERATOR.", String(p_function->name), operator_offset));
			}
			for (int word = 5; word < 7 + OPERATOR_INLINE_CACHE_PTR_WORDS; word++) {
				sanitized_code.write[operator_offset + word] = 0;
			}
		}

		r_writer.write_u32(static_cast<uint32_t>(sanitized_code.size()));
		for (int index = 0; index < sanitized_code.size(); index++) {
			r_writer.write_i32(sanitized_code[index]);
		}

		r_writer.write_u32(static_cast<uint32_t>(p_function->_default_arg_count));
		r_writer.write_u32(static_cast<uint32_t>(p_function->default_arguments.size()));
		for (int index = 0; index < p_function->default_arguments.size(); index++) {
			r_writer.write_i32(static_cast<int32_t>(p_function->default_arguments[index]));
		}

		r_writer.write_u32(static_cast<uint32_t>(p_function->constants.size()));
		for (int index = 0; index < p_function->constants.size(); index++) {
			Error constant_error = write_tagged_variant(r_writer, p_function->constants[index], 0, false);
			if (constant_error != OK) {
				return constant_error;
			}
		}

		r_writer.write_u32(static_cast<uint32_t>(p_function->global_names.size()));
		for (int index = 0; index < p_function->global_names.size(); index++) {
			r_writer.write_u32(intern_string_name(p_function->global_names[index]));
		}

		// Fixup tables 0..11 in fixed order.
		// 0: FIXUP_OPERATOR
		r_writer.write_u32(static_cast<uint32_t>(fixups.operators.size()));
		for (const GDScriptFunction::ExportFixups::OperatorFixup &entry : fixups.operators) {
			r_writer.write_u32(static_cast<uint32_t>(entry.operation));
			r_writer.write_u32(static_cast<uint32_t>(entry.left_type));
			r_writer.write_u32(static_cast<uint32_t>(entry.right_type));
		}
		// 1: FIXUP_SETTER
		r_writer.write_u32(static_cast<uint32_t>(fixups.setters.size()));
		for (const GDScriptFunction::ExportFixups::MemberFixup &entry : fixups.setters) {
			r_writer.write_u32(static_cast<uint32_t>(entry.builtin_type));
			r_writer.write_u32(intern_string_name(entry.member_name));
		}
		// 2: FIXUP_GETTER
		r_writer.write_u32(static_cast<uint32_t>(fixups.getters.size()));
		for (const GDScriptFunction::ExportFixups::MemberFixup &entry : fixups.getters) {
			r_writer.write_u32(static_cast<uint32_t>(entry.builtin_type));
			r_writer.write_u32(intern_string_name(entry.member_name));
		}
		// 3: FIXUP_KEYED_SETTER
		r_writer.write_u32(static_cast<uint32_t>(fixups.keyed_setters.size()));
		for (Variant::Type entry_type : fixups.keyed_setters) {
			r_writer.write_u32(static_cast<uint32_t>(entry_type));
		}
		// 4: FIXUP_KEYED_GETTER
		r_writer.write_u32(static_cast<uint32_t>(fixups.keyed_getters.size()));
		for (Variant::Type entry_type : fixups.keyed_getters) {
			r_writer.write_u32(static_cast<uint32_t>(entry_type));
		}
		// 5: FIXUP_INDEXED_SETTER
		r_writer.write_u32(static_cast<uint32_t>(fixups.indexed_setters.size()));
		for (Variant::Type entry_type : fixups.indexed_setters) {
			r_writer.write_u32(static_cast<uint32_t>(entry_type));
		}
		// 6: FIXUP_INDEXED_GETTER
		r_writer.write_u32(static_cast<uint32_t>(fixups.indexed_getters.size()));
		for (Variant::Type entry_type : fixups.indexed_getters) {
			r_writer.write_u32(static_cast<uint32_t>(entry_type));
		}
		// 7: FIXUP_BUILTIN_METHOD
		r_writer.write_u32(static_cast<uint32_t>(fixups.builtin_methods.size()));
		for (const GDScriptFunction::ExportFixups::BuiltinMethodFixup &entry : fixups.builtin_methods) {
			r_writer.write_u32(static_cast<uint32_t>(entry.builtin_type));
			r_writer.write_u32(intern_string_name(entry.method_name));
		}
		// 8: FIXUP_CONSTRUCTOR
		r_writer.write_u32(static_cast<uint32_t>(fixups.constructors.size()));
		for (const GDScriptFunction::ExportFixups::ConstructorFixup &entry : fixups.constructors) {
			r_writer.write_u32(static_cast<uint32_t>(entry.builtin_type));
			r_writer.write_i32(static_cast<int32_t>(entry.constructor_index));
		}
		// 9: FIXUP_UTILITY
		r_writer.write_u32(static_cast<uint32_t>(fixups.utilities.size()));
		for (const StringName &utility_name : fixups.utilities) {
			r_writer.write_u32(intern_string_name(utility_name));
		}
		// 10: FIXUP_GDS_UTILITY
		r_writer.write_u32(static_cast<uint32_t>(fixups.gds_utilities.size()));
		for (const StringName &gds_utility_name : fixups.gds_utilities) {
			r_writer.write_u32(intern_string_name(gds_utility_name));
		}
		// 11: FIXUP_METHOD_BIND
		r_writer.write_u32(static_cast<uint32_t>(fixups.method_binds.size()));
		for (const GDScriptFunction::ExportFixups::MethodBindFixup &entry : fixups.method_binds) {
			r_writer.write_u32(intern_string_name(entry.class_name));
			r_writer.write_u32(intern_string_name(entry.method_name));
		}

		// Global stores.
		r_writer.write_u32(static_cast<uint32_t>(sorted_global_stores.size()));
		for (const GDScriptFunction::ExportFixups::GlobalStoreFixup &store_fixup : sorted_global_stores) {
			r_writer.write_i32(static_cast<int32_t>(store_fixup.code_offset));
			r_writer.write_u32(intern_string_name(store_fixup.global_name));
		}

		// Nested lambdas.
		r_writer.write_u32(static_cast<uint32_t>(p_function->lambdas.size()));
		for (int index = 0; index < p_function->lambdas.size(); index++) {
			GDScriptFunction *lambda_function = p_function->lambdas[index];
			HashMap<GDScriptFunction *, GDScript::LambdaInfo>::ConstIterator lambda_entry = p_owning_script->lambda_info.find(lambda_function);
			if (!lambda_entry) {
				return fail(ERR_INVALID_DATA, vformat("Function \"%s\" lambda %d is missing lambda_info metadata.", String(p_function->name), index));
			}
			r_writer.write_i32(static_cast<int32_t>(lambda_entry->value.capture_count));
			r_writer.write_bool(lambda_entry->value.use_self);
			Error lambda_error = write_function_record(r_writer, p_owning_script, lambda_function, p_lambda_depth + 1);
			if (lambda_error != OK) {
				return lambda_error;
			}
		}

		return OK;
	}

	Error write_class_body(BytecodeWriter &r_writer, const GDScript *p_script) {
		// Own members (sorted).
		Vector<StringName> sorted_members;
		sorted_members.resize(p_script->members.size());
		int write_index = 0;
		for (const StringName &member_name : p_script->members) {
			sorted_members.write[write_index++] = member_name;
		}
		sorted_members.sort_custom<StringNameBytewiseComparator>();

		r_writer.write_u32(static_cast<uint32_t>(sorted_members.size()));
		for (const StringName &member_name : sorted_members) {
			r_writer.write_u32(intern_string_name(member_name));
		}

		// Member indices (sorted).
		Vector<StringName> sorted_member_indices;
		sorted_member_indices.resize(p_script->member_indices.size());
		write_index = 0;
		for (const KeyValue<StringName, GDScript::MemberInfo> &entry : p_script->member_indices) {
			sorted_member_indices.write[write_index++] = entry.key;
		}
		sorted_member_indices.sort_custom<StringNameBytewiseComparator>();

		r_writer.write_u32(static_cast<uint32_t>(sorted_member_indices.size()));
		for (const StringName &member_name : sorted_member_indices) {
			Error error = write_member_info_entry(r_writer, member_name, p_script->member_indices[member_name]);
			if (error != OK) {
				return error;
			}
		}

		// Static variable indices (sorted).
		Vector<StringName> sorted_static_indices;
		sorted_static_indices.resize(p_script->static_variables_indices.size());
		write_index = 0;
		for (const KeyValue<StringName, GDScript::MemberInfo> &entry : p_script->static_variables_indices) {
			sorted_static_indices.write[write_index++] = entry.key;
		}
		sorted_static_indices.sort_custom<StringNameBytewiseComparator>();

		r_writer.write_u32(static_cast<uint32_t>(sorted_static_indices.size()));
		for (const StringName &static_name : sorted_static_indices) {
			Error error = write_member_info_entry(r_writer, static_name, p_script->static_variables_indices[static_name]);
			if (error != OK) {
				return error;
			}
		}

		// Constants (sorted).
		Vector<StringName> sorted_constants;
		sorted_constants.resize(p_script->constants.size());
		write_index = 0;
		for (const KeyValue<StringName, Variant> &entry : p_script->constants) {
			sorted_constants.write[write_index++] = entry.key;
		}
		sorted_constants.sort_custom<StringNameBytewiseComparator>();

		r_writer.write_u32(static_cast<uint32_t>(sorted_constants.size()));
		for (const StringName &constant_name : sorted_constants) {
			r_writer.write_u32(intern_string_name(constant_name));
			Error error = write_tagged_variant(r_writer, p_script->constants[constant_name], 0, false);
			if (error != OK) {
				return error;
			}
		}

		// Signals (sorted).
		Vector<StringName> sorted_signals;
		sorted_signals.resize(p_script->_signals.size());
		write_index = 0;
		for (const KeyValue<StringName, MethodInfo> &entry : p_script->_signals) {
			sorted_signals.write[write_index++] = entry.key;
		}
		sorted_signals.sort_custom<StringNameBytewiseComparator>();

		r_writer.write_u32(static_cast<uint32_t>(sorted_signals.size()));
		for (const StringName &signal_name : sorted_signals) {
			r_writer.write_u32(intern_string_name(signal_name));
			Error error = write_method_info(r_writer, p_script->_signals[signal_name]);
			if (error != OK) {
				return error;
			}
		}

		// RPC config (Dictionary with sorted keys).
		Error rpc_error = write_tagged_variant(r_writer, Variant(p_script->rpc_config), 0, true);
		if (rpc_error != OK) {
			return rpc_error;
		}

		// Member functions (sorted).
		Vector<StringName> sorted_functions;
		sorted_functions.resize(p_script->member_functions.size());
		write_index = 0;
		for (const KeyValue<StringName, GDScriptFunction *> &entry : p_script->member_functions) {
			sorted_functions.write[write_index++] = entry.key;
		}
		sorted_functions.sort_custom<StringNameBytewiseComparator>();

		r_writer.write_u32(static_cast<uint32_t>(sorted_functions.size()));
		for (const StringName &function_name : sorted_functions) {
			r_writer.write_u32(intern_string_name(function_name));
			Error error = write_function_record(r_writer, p_script, p_script->member_functions[function_name], 0);
			if (error != OK) {
				return error;
			}
		}

		// Special functions.
		r_writer.write_bool(p_script->implicit_initializer != nullptr);
		if (p_script->implicit_initializer != nullptr) {
			Error error = write_function_record(r_writer, p_script, p_script->implicit_initializer, 0);
			if (error != OK) {
				return error;
			}
		}

		r_writer.write_bool(p_script->implicit_ready != nullptr);
		if (p_script->implicit_ready != nullptr) {
			Error error = write_function_record(r_writer, p_script, p_script->implicit_ready, 0);
			if (error != OK) {
				return error;
			}
		}

		r_writer.write_bool(p_script->static_initializer != nullptr);
		if (p_script->static_initializer != nullptr) {
			Error error = write_function_record(r_writer, p_script, p_script->static_initializer, 0);
			if (error != OK) {
				return error;
			}
		}

		return OK;
	}

public:
	ExportContext(const GDScript *p_root_script, const GDScriptBytecodeExporter::Options &p_options, String *r_error_message) :
			root_script(p_root_script), options(p_options), error_message(r_error_message) {
		if (Engine::get_singleton() != nullptr) {
			List<Engine::Singleton> singletons;
			Engine::get_singleton()->get_singletons(&singletons);
			for (const Engine::Singleton &singleton : singletons) {
				if (singleton.ptr != nullptr && !engine_singleton_names.has(singleton.ptr)) {
					engine_singleton_names.insert(singleton.ptr, singleton.name);
				}
			}
		}
	}

	Error serialize(Vector<uint8_t> &r_buffer) {
		if (root_script == nullptr || !root_script->is_valid()) {
			return fail(ERR_INVALID_PARAMETER, "Cannot serialize null or invalid GDScript.");
		}

		Error collect_error = collect_local_classes(root_script, 0);
		if (collect_error != OK) {
			return collect_error;
		}

		// Encode SECTION_SKELETON and SECTION_CLASS_BODIES into a scratch buffer first so
		// SECTION_STRING_TABLE and SECTION_DEPENDENCIES can be spliced ahead of them.
		BytecodeWriter body_scratch;
		body_scratch.write_u32(SECTION_SKELETON);
		body_scratch.write_u32(static_cast<uint32_t>(local_classes.size()));
		Error skeleton_error = write_skeleton_class(body_scratch, root_script, 0);
		if (skeleton_error != OK) {
			return skeleton_error;
		}

		body_scratch.write_u32(SECTION_CLASS_BODIES);
		for (int class_index = 0; class_index < local_classes.size(); class_index++) {
			Error class_error = write_class_body(body_scratch, local_classes[class_index]);
			if (class_error != OK) {
				return class_error;
			}
		}

		// Sort and deduplicate external dependencies; intern their paths (already present in string table).
		Vector<String> sorted_dependencies;
		sorted_dependencies.resize(external_dependencies.size());
		int dep_index = 0;
		for (const String &dep_path : external_dependencies) {
			sorted_dependencies.write[dep_index++] = dep_path;
		}
		sorted_dependencies.sort_custom<StringBytewiseComparator>();

		Vector<uint32_t> dependency_string_indices;
		dependency_string_indices.resize(sorted_dependencies.size());
		for (int index = 0; index < sorted_dependencies.size(); index++) {
			dependency_string_indices.write[index] = intern_string(sorted_dependencies[index]);
		}

		// Assemble final container: Header -> ScriptFlags -> Section 0 -> Section 1 -> Section 2 -> Section 3.
		BytecodeWriter output;
		output.write_raw(MAGIC, 4);
		output.write_u32(FORMAT_VERSION);
		output.write_raw(GDSCRIPT_BYTECODE_ABI_ID, 16);
		output.write_u8(static_cast<uint8_t>(sizeof(real_t)));
		output.write_u32(static_cast<uint32_t>(GDScriptFunction::OPCODE_END));
		output.write_utf8_string(String(GODOT_VERSION_FULL_CONFIG));
		output.write_utf8_string(String(GODOT_VERSION_HASH));

		uint32_t script_flags = 0;
		bool has_static_data = options.has_static_data;
		if (options.auto_detect_static_data) {
			has_static_data = has_static_data || GDScriptBytecodeExporter::detect_static_data(Ref<GDScript>(const_cast<GDScript *>(root_script)));
		}
		if (has_static_data) {
			script_flags |= SCRIPT_FLAG_HAS_STATIC_DATA;
		}
		if (options.annotated_static_unload) {
			script_flags |= SCRIPT_FLAG_ANNOTATED_STATIC_UNLOAD;
		}
		output.write_u32(script_flags);

		// Section 0: SECTION_STRING_TABLE.
		output.write_u32(SECTION_STRING_TABLE);
		output.write_u32(static_cast<uint32_t>(string_table.size()));
		for (int index = 0; index < string_table.size(); index++) {
			output.write_utf8_string(string_table[index]);
		}

		// Section 1: SECTION_DEPENDENCIES.
		output.write_u32(SECTION_DEPENDENCIES);
		output.write_u32(static_cast<uint32_t>(dependency_string_indices.size()));
		for (int index = 0; index < dependency_string_indices.size(); index++) {
			output.write_u32(dependency_string_indices[index]);
		}

		// Section 2 & 3 from scratch buffer.
		const Vector<uint8_t> &scratch_bytes = body_scratch.get_bytes();
		output.write_raw(scratch_bytes.ptr(), scratch_bytes.size());

		r_buffer = output.get_bytes();
		return OK;
	}
	static void collect_function_named_globals(const GDScriptFunction *p_function, HashSet<StringName> &r_seen) {
		if (p_function == nullptr) {
			return;
		}
		const GDScriptFunction::ExportFixups &fixups = p_function->get_export_fixups();
		for (const StringName &global_name : fixups.named_globals) {
			r_seen.insert(global_name);
		}
		for (int lambda_index = 0; lambda_index < p_function->lambdas.size(); lambda_index++) {
			collect_function_named_globals(p_function->lambdas[lambda_index], r_seen);
		}
	}

	static void collect_class_named_globals(const GDScript *p_script, HashSet<StringName> &r_seen) {
		if (p_script == nullptr) {
			return;
		}
		for (const KeyValue<StringName, GDScriptFunction *> &entry : p_script->get_member_functions()) {
			collect_function_named_globals(entry.value, r_seen);
		}
		collect_function_named_globals(p_script->get_implicit_initializer(), r_seen);
		collect_function_named_globals(p_script->get_implicit_ready(), r_seen);
		collect_function_named_globals(p_script->get_static_initializer(), r_seen);

		for (const KeyValue<StringName, Ref<GDScript>> &subclass_entry : p_script->get_subclasses()) {
			collect_class_named_globals(subclass_entry.value.ptr(), r_seen);
		}
	}

	static bool detect_class_static_data(const GDScript *p_script) {
		if (p_script == nullptr) {
			return false;
		}
		if (p_script->get_static_initializer() != nullptr) {
			return true;
		}
		for (const KeyValue<StringName, Ref<GDScript>> &subclass_entry : p_script->get_subclasses()) {
			if (detect_class_static_data(subclass_entry.value.ptr())) {
				return true;
			}
		}
		return false;
	}
};

bool GDScriptBytecodeExporter::detect_static_data(const Ref<GDScript> &p_script) {
	return ExportContext::detect_class_static_data(p_script.ptr());
}

Vector<StringName> GDScriptBytecodeExporter::collect_unsupported_named_globals(const Ref<GDScript> &p_script) {
	HashSet<StringName> unique_named_globals;
	ExportContext::collect_class_named_globals(p_script.ptr(), unique_named_globals);

	Vector<StringName> result;
	result.resize(unique_named_globals.size());
	int write_index = 0;
	for (const StringName &global_name : unique_named_globals) {
		result.write[write_index++] = global_name;
	}
	result.sort_custom<StringNameBytewiseComparator>();
	return result;
}

Error GDScriptBytecodeExporter::serialize(
		const Ref<GDScript> &p_script,
		Vector<uint8_t> &r_buffer,
		const Options &p_options,
		String *r_error_message) {
	r_buffer.clear();
	if (p_script.is_null()) {
		if (r_error_message != nullptr) {
			*r_error_message = "Cannot serialize null GDScript reference.";
		}
		return ERR_INVALID_PARAMETER;
	}
	ExportContext context(p_script.ptr(), p_options, r_error_message);
	return context.serialize(r_buffer);
}

#endif // GDSCRIPT_BYTECODE_EXPORT_ENABLED
