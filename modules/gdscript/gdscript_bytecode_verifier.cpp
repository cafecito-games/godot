/**************************************************************************/
/*  gdscript_bytecode_verifier.cpp                                        */
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

#include "gdscript_bytecode_verifier.h"

#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"

struct GDScriptBytecodeVerifier::Impl {
	static Error fail_verify(String *r_error_message, const String &p_message) {
		if (r_error_message != nullptr) {
			*r_error_message = p_message;
		}
		return ERR_INVALID_DATA;
	}

	static Error verify_packed_address(
			int32_t p_raw_address,
			int32_t p_stack_size,
			int32_t p_constant_count,
			int32_t p_member_count,
			bool p_allow_member_address,
			String *r_error_message) {
		const uint32_t raw_unsigned = static_cast<uint32_t>(p_raw_address);
		const uint32_t address_type = raw_unsigned >> GDScriptFunction::ADDR_BITS;
		const uint32_t address_index = raw_unsigned & static_cast<uint32_t>(GDScriptFunction::ADDR_MASK);

		if (address_type >= static_cast<uint32_t>(GDScriptFunction::ADDR_TYPE_MAX)) {
			return Impl::fail_verify(r_error_message, vformat("Invalid bytecode address type %d.", address_type));
		}

		switch (address_type) {
			case GDScriptFunction::ADDR_TYPE_STACK:
				if (address_index >= static_cast<uint32_t>(p_stack_size)) {
					return Impl::fail_verify(r_error_message, vformat("Stack address index %d out of range (stack_size=%d).", address_index, p_stack_size));
				}
				break;
			case GDScriptFunction::ADDR_TYPE_CONSTANT:
				if (address_index >= static_cast<uint32_t>(p_constant_count)) {
					return Impl::fail_verify(r_error_message, vformat("Constant address index %d out of range (constant_count=%d).", address_index, p_constant_count));
				}
				break;
			case GDScriptFunction::ADDR_TYPE_MEMBER:
				if (!p_allow_member_address) {
					return Impl::fail_verify(r_error_message, "Member address is illegal in a function without an instance.");
				}
				if (address_index >= static_cast<uint32_t>(p_member_count)) {
					return Impl::fail_verify(r_error_message, vformat("Member address index %d out of range (member_count=%d).", address_index, p_member_count));
				}
				break;
			default:
				return Impl::fail_verify(r_error_message, vformat("Unsupported bytecode address type %d.", address_type));
		}

		return OK;
	}

	template <typename TargetClassT>
	static TargetClassT *resolve_constant_object_operand(
			int32_t p_raw_address,
			const GDScriptFunction *p_function) {
		const uint32_t raw_unsigned = static_cast<uint32_t>(p_raw_address);
		const uint32_t address_type = raw_unsigned >> GDScriptFunction::ADDR_BITS;
		const uint32_t address_index = raw_unsigned & static_cast<uint32_t>(GDScriptFunction::ADDR_MASK);
		if (address_type != GDScriptFunction::ADDR_TYPE_CONSTANT || address_index >= static_cast<uint32_t>(p_function->_constant_count)) {
			return nullptr;
		}
		const Variant &constant_value = p_function->_constants_ptr[address_index];
		if (constant_value.get_type() != Variant::OBJECT) {
			return nullptr;
		}
		bool was_freed = false;
		Object *object = constant_value.get_validated_object_with_check(was_freed);
		if (was_freed || object == nullptr) {
			return nullptr;
		}
		return Object::cast_to<TargetClassT>(object);
	}

	static Error verify_contiguous_member_indices(
			const HashMap<StringName, GDScript::MemberInfo> &p_indices,
			const char *p_label,
			String *r_error_message) {
		const int expected_count = static_cast<int>(p_indices.size());
		LocalVector<uint8_t> seen;
		seen.resize(expected_count);
		for (int index = 0; index < expected_count; index++) {
			seen[index] = 0;
		}

		for (const KeyValue<StringName, GDScript::MemberInfo> &entry : p_indices) {
			const int member_index = entry.value.index;
			if (member_index < 0 || member_index >= expected_count) {
				return Impl::fail_verify(r_error_message, vformat("%s entry \"%s\" has out-of-range index %d (count=%d).", p_label, String(entry.key), member_index, expected_count));
			}
			if (seen[member_index] != 0) {
				return Impl::fail_verify(r_error_message, vformat("%s contains duplicate index %d.", p_label, member_index));
			}
			seen[member_index] = 1;
		}

		return OK;
	}

	static Error verify_operand(
			GDScriptBytecodeFormat::OpcodeOperandKind p_kind,
			int32_t p_value,
			int p_word_offset,
			int p_instruction_ip,
			GDScriptFunction::Opcode p_opcode,
			int32_t p_vararg_count_value,
			const GDScriptFunction *p_function,
			const GDScript *p_owning_script,
			int32_t p_stack_size,
			int32_t p_constant_count,
			int32_t p_member_count,
			bool p_allow_member_address,
			int32_t p_global_array_size,
			LocalVector<int> &r_jump_targets,
			LocalVector<uint8_t> r_used_fixups[GDScriptBytecodeFormat::FIXUP_TABLE_COUNT],
			LocalVector<uint8_t> &r_used_lambdas,
			HashSet<int> &r_seen_global_store_offsets,
			String *r_error_message) {
		using namespace GDScriptBytecodeFormat;
		switch (p_kind) {
			case OPERAND_ADDRESS:
				return Impl::verify_packed_address(p_value, p_stack_size, p_constant_count, p_member_count, p_allow_member_address, r_error_message);
			case OPERAND_JUMP_TARGET:
				r_jump_targets.push_back(p_value);
				return OK;
			case OPERAND_GLOBAL_NAME:
				if (p_value < 0 || p_value >= p_function->_global_names_count) {
					return Impl::fail_verify(r_error_message, vformat("Global name index %d out of range (count=%d).", p_value, p_function->_global_names_count));
				}
				return OK;
			case OPERAND_VARIANT_TYPE:
				if (p_value < 0 || p_value >= Variant::VARIANT_MAX) {
					return Impl::fail_verify(r_error_message, vformat("Variant::Type operand %d out of range.", p_value));
				}
				return OK;
			case OPERAND_VARIANT_OPERATOR:
				if (p_value < 0 || p_value >= Variant::OP_MAX) {
					return Impl::fail_verify(r_error_message, vformat("Variant::Operator operand %d out of range.", p_value));
				}
				return OK;
			case OPERAND_OPERATOR_CACHE_ZERO:
				if (p_value != 0) {
					return Impl::fail_verify(r_error_message, vformat("OPCODE_OPERATOR inline cache word at offset %d must be 0 (got %d).", p_word_offset, p_value));
				}
				return OK;
			case OPERAND_STATIC_VARIABLE_INDEX: {
				if (p_value < 0) {
					return Impl::fail_verify(r_error_message, vformat("Negative static variable index %d.", p_value));
				}
				const int32_t class_address = p_function->_code_ptr[p_instruction_ip + 2];
				const GDScript *target_script = nullptr;
				if (class_address == GDScriptFunction::ADDR_CLASS) {
					target_script = p_owning_script;
				} else {
					target_script = Impl::resolve_constant_object_operand<GDScript>(class_address, p_function);
					if (target_script == nullptr) {
						return Impl::fail_verify(r_error_message, vformat("Static variable access at offset %d must target ADDR_CLASS or a constant GDScript.", p_instruction_ip));
					}
				}
				if (p_value >= target_script->static_variables.size()) {
					return Impl::fail_verify(r_error_message, vformat("Static variable index %d out of range for class \"%s\" (size=%d).", p_value, target_script->fully_qualified_name, target_script->static_variables.size()));
				}
				return OK;
			}
			case OPERAND_GLOBAL_INDEX:
				if (p_value < 0 || p_value >= p_global_array_size) {
					return Impl::fail_verify(r_error_message, vformat("Global store index %d out of range (global_array_size=%d).", p_value, p_global_array_size));
				}
				r_seen_global_store_offsets.insert(p_word_offset);
				return OK;
			case OPERAND_LINE_NUMBER:
				if (p_value < 0) {
					return Impl::fail_verify(r_error_message, vformat("Negative line number operand %d.", p_value));
				}
				return OK;
			case OPERAND_LAMBDA_INDEX: {
				if (p_value < 0 || p_value >= p_function->_lambdas_count) {
					return Impl::fail_verify(r_error_message, vformat("Lambda index %d out of range (count=%d).", p_value, p_function->_lambdas_count));
				}
				GDScriptFunction *lambda_function = p_function->_lambdas_ptr[p_value];
				if (lambda_function == nullptr) {
					return Impl::fail_verify(r_error_message, vformat("Null lambda function at index %d.", p_value));
				}
				HashMap<GDScriptFunction *, GDScript::LambdaInfo>::ConstIterator lambda_entry = p_owning_script->lambda_info.find(lambda_function);
				if (!lambda_entry) {
					return Impl::fail_verify(r_error_message, vformat("Missing lambda_info metadata for lambda index %d.", p_value));
				}
				if (lambda_entry->value.capture_count != p_vararg_count_value) {
					return Impl::fail_verify(r_error_message, vformat("Lambda capture_count mismatch at index %d (instruction=%d, lambda_info=%d).", p_value, p_vararg_count_value, lambda_entry->value.capture_count));
				}
				const bool opcode_uses_self = (p_opcode == GDScriptFunction::OPCODE_CREATE_SELF_LAMBDA);
				if (lambda_entry->value.use_self != opcode_uses_self) {
					return Impl::fail_verify(r_error_message, vformat("Lambda use_self mismatch at index %d.", p_value));
				}
				r_used_lambdas[p_value] = 1;
				return OK;
			}
			case OPERAND_VARARG_COUNT:
				if (p_value < 0) {
					return Impl::fail_verify(r_error_message, vformat("Negative variable-width count operand %d.", p_value));
				}
				return OK;
			case OPERAND_FIXUP_OPERATOR:
				if (p_value < 0 || p_value >= p_function->_operator_funcs_count || p_function->_operator_funcs_ptr[p_value] == nullptr) {
					return Impl::fail_verify(r_error_message, vformat("Operator fixup index %d out of range or null.", p_value));
				}
				r_used_fixups[FIXUP_OPERATOR][p_value] = 1;
				return OK;
			case OPERAND_FIXUP_SETTER:
				if (p_value < 0 || p_value >= p_function->_setters_count || p_function->_setters_ptr[p_value] == nullptr) {
					return Impl::fail_verify(r_error_message, vformat("Setter fixup index %d out of range or null.", p_value));
				}
				r_used_fixups[FIXUP_SETTER][p_value] = 1;
				return OK;
			case OPERAND_FIXUP_GETTER:
				if (p_value < 0 || p_value >= p_function->_getters_count || p_function->_getters_ptr[p_value] == nullptr) {
					return Impl::fail_verify(r_error_message, vformat("Getter fixup index %d out of range or null.", p_value));
				}
				r_used_fixups[FIXUP_GETTER][p_value] = 1;
				return OK;
			case OPERAND_FIXUP_KEYED_SETTER:
				if (p_value < 0 || p_value >= p_function->_keyed_setters_count || p_function->_keyed_setters_ptr[p_value] == nullptr) {
					return Impl::fail_verify(r_error_message, vformat("Keyed setter fixup index %d out of range or null.", p_value));
				}
				r_used_fixups[FIXUP_KEYED_SETTER][p_value] = 1;
				return OK;
			case OPERAND_FIXUP_KEYED_GETTER:
				if (p_value < 0 || p_value >= p_function->_keyed_getters_count || p_function->_keyed_getters_ptr[p_value] == nullptr) {
					return Impl::fail_verify(r_error_message, vformat("Keyed getter fixup index %d out of range or null.", p_value));
				}
				r_used_fixups[FIXUP_KEYED_GETTER][p_value] = 1;
				return OK;
			case OPERAND_FIXUP_INDEXED_SETTER:
				if (p_value < 0 || p_value >= p_function->_indexed_setters_count || p_function->_indexed_setters_ptr[p_value] == nullptr) {
					return Impl::fail_verify(r_error_message, vformat("Indexed setter fixup index %d out of range or null.", p_value));
				}
				r_used_fixups[FIXUP_INDEXED_SETTER][p_value] = 1;
				return OK;
			case OPERAND_FIXUP_INDEXED_GETTER:
				if (p_value < 0 || p_value >= p_function->_indexed_getters_count || p_function->_indexed_getters_ptr[p_value] == nullptr) {
					return Impl::fail_verify(r_error_message, vformat("Indexed getter fixup index %d out of range or null.", p_value));
				}
				r_used_fixups[FIXUP_INDEXED_GETTER][p_value] = 1;
				return OK;
			case OPERAND_FIXUP_BUILTIN_METHOD:
				if (p_value < 0 || p_value >= p_function->_builtin_methods_count || p_function->_builtin_methods_ptr[p_value] == nullptr) {
					return Impl::fail_verify(r_error_message, vformat("Builtin method fixup index %d out of range or null.", p_value));
				}
				r_used_fixups[FIXUP_BUILTIN_METHOD][p_value] = 1;
				return OK;
			case OPERAND_FIXUP_CONSTRUCTOR:
				if (p_value < 0 || p_value >= p_function->_constructors_count || p_function->_constructors_ptr[p_value] == nullptr) {
					return Impl::fail_verify(r_error_message, vformat("Constructor fixup index %d out of range or null.", p_value));
				}
				r_used_fixups[FIXUP_CONSTRUCTOR][p_value] = 1;
				return OK;
			case OPERAND_FIXUP_UTILITY:
				if (p_value < 0 || p_value >= p_function->_utilities_count || p_function->_utilities_ptr[p_value] == nullptr) {
					return Impl::fail_verify(r_error_message, vformat("Utility fixup index %d out of range or null.", p_value));
				}
				r_used_fixups[FIXUP_UTILITY][p_value] = 1;
				return OK;
			case OPERAND_FIXUP_GDS_UTILITY:
				if (p_value < 0 || p_value >= p_function->_gds_utilities_count || p_function->_gds_utilities_ptr[p_value] == nullptr) {
					return Impl::fail_verify(r_error_message, vformat("GDScript utility fixup index %d out of range or null.", p_value));
				}
				r_used_fixups[FIXUP_GDS_UTILITY][p_value] = 1;
				return OK;
			case OPERAND_FIXUP_METHOD_BIND:
				if (p_value < 0 || p_value >= p_function->_methods_count || p_function->_methods_ptr[p_value] == nullptr) {
					return Impl::fail_verify(r_error_message, vformat("MethodBind fixup index %d out of range or null.", p_value));
				}
				r_used_fixups[FIXUP_METHOD_BIND][p_value] = 1;
				return OK;
			case OPERAND_NONE:
			default:
				return Impl::fail_verify(r_error_message, "Unexpected OPERAND_NONE in opcode layout table.");
		}
	}

	template <typename VectorT, typename PtrT>
	static bool check_table_sync(const VectorT &p_vector, int p_count, const PtrT *p_ptr) {
		if (p_count != p_vector.size()) {
			return false;
		}
		if (p_count == 0) {
			return p_ptr == nullptr;
		}
		return p_ptr == p_vector.ptr();
	}

	static Error verify_script_tree_recursive(
			const GDScript *p_script,
			const GDScriptBytecodeVerifier::GlobalStoreOffsetsMap *p_global_store_offsets,
			uint32_t p_class_depth,
			String *r_error_message) {
		if (p_class_depth > GDScriptBytecodeFormat::MAX_CLASS_DEPTH) {
			return Impl::fail_verify(r_error_message, "Script subclass hierarchy exceeds MAX_CLASS_DEPTH.");
		}
		Error class_error = GDScriptBytecodeVerifier::verify_class(p_script, p_global_store_offsets, r_error_message);
		if (class_error != OK) {
			return class_error;
		}
		for (const KeyValue<StringName, Ref<GDScript>> &subclass_entry : p_script->get_subclasses()) {
			if (subclass_entry.value.is_null()) {
				return Impl::fail_verify(r_error_message, vformat("Null subclass \"%s\" in script \"%s\".", String(subclass_entry.key), p_script->get_fully_qualified_name()));
			}
			Error subclass_error = verify_script_tree_recursive(subclass_entry.value.ptr(), p_global_store_offsets, p_class_depth + 1, r_error_message);
			if (subclass_error != OK) {
				return subclass_error;
			}
		}
		return OK;
	}
};

Error GDScriptBytecodeVerifier::verify_function(
		const GDScriptFunction *p_function,
		const GDScript *p_owning_script,
		const GlobalStoreOffsetsMap *p_global_store_offsets,
		uint32_t p_lambda_depth,
		String *r_error_message) {
	using namespace GDScriptBytecodeFormat;

	if (p_function == nullptr || p_owning_script == nullptr) {
		return Impl::fail_verify(r_error_message, "Function or owning script pointer is null.");
	}
	if (p_lambda_depth > MAX_LAMBDA_DEPTH) {
		return Impl::fail_verify(r_error_message, "Lambda nesting exceeds MAX_LAMBDA_DEPTH.");
	}
	if (p_function->_script != p_owning_script) {
		return Impl::fail_verify(r_error_message, vformat("Function \"%s\" has mismatched owning script pointer.", String(p_function->name)));
	}
	if (p_function->_initial_line < 0) {
		return Impl::fail_verify(r_error_message, vformat("Function \"%s\" has negative initial_line %d.", String(p_function->name), p_function->_initial_line));
	}

	// Scalar VM execution ceilings.
	if (p_function->_stack_size < GDScriptFunction::FIXED_ADDRESSES_MAX || p_function->_stack_size > MAX_STACK_SIZE) {
		return Impl::fail_verify(r_error_message, vformat("Function \"%s\" stack_size %d out of range [%d, %d].", String(p_function->name), p_function->_stack_size, GDScriptFunction::FIXED_ADDRESSES_MAX, MAX_STACK_SIZE));
	}
	if (p_function->_instruction_args_size < 0 || p_function->_instruction_args_size > MAX_INSTRUCTION_ARGS_SIZE) {
		return Impl::fail_verify(r_error_message, vformat("Function \"%s\" instruction_args_size %d out of range [0, %d].", String(p_function->name), p_function->_instruction_args_size, MAX_INSTRUCTION_ARGS_SIZE));
	}
	if (p_function->_argument_count < 0 || p_function->_argument_count > MAX_ARGUMENT_COUNT) {
		return Impl::fail_verify(r_error_message, vformat("Function \"%s\" argument_count %d out of range [0, %d].", String(p_function->name), p_function->_argument_count, MAX_ARGUMENT_COUNT));
	}
	if (p_function->_argument_count > p_function->_stack_size - GDScriptFunction::FIXED_ADDRESSES_MAX) {
		return Impl::fail_verify(r_error_message, vformat("Function \"%s\" argument_count %d exceeds available stack slots.", String(p_function->name), p_function->_argument_count));
	}

	const uint64_t frame_alloca_bytes =
			static_cast<uint64_t>(sizeof(Variant *)) * static_cast<uint64_t>(GDScriptFunction::FIXED_ADDRESSES_MAX) +
			static_cast<uint64_t>(sizeof(Variant *)) * static_cast<uint64_t>(p_function->_instruction_args_size) +
			static_cast<uint64_t>(sizeof(Variant)) * static_cast<uint64_t>(p_function->_stack_size);
	if (frame_alloca_bytes > MAX_FRAME_ALLOCA_BYTES) {
		return Impl::fail_verify(r_error_message, vformat("Function \"%s\" frame size %d exceeds MAX_FRAME_ALLOCA_BYTES (%d).", String(p_function->name), static_cast<uint32_t>(frame_alloca_bytes), MAX_FRAME_ALLOCA_BYTES));
	}

	// Exact vararg contract.
	const bool has_vararg = p_function->_vararg_index >= 0;
	if (has_vararg) {
		const int expected_vararg_index = GDScriptFunction::FIXED_ADDRESSES_MAX + p_function->_argument_count;
		if (p_function->_vararg_index != expected_vararg_index || p_function->_vararg_index >= p_function->_stack_size) {
			return Impl::fail_verify(r_error_message, vformat("Function \"%s\" has invalid vararg_index %d (expected %d < stack_size=%d).", String(p_function->name), p_function->_vararg_index, expected_vararg_index, p_function->_stack_size));
		}
	} else if (p_function->_vararg_index != -1) {
		return Impl::fail_verify(r_error_message, vformat("Function \"%s\" has invalid negative vararg_index %d.", String(p_function->name), p_function->_vararg_index));
	}
	const bool method_info_is_vararg = (p_function->method_info.flags & METHOD_FLAG_VARARG) != 0;
	if (method_info_is_vararg != has_vararg) {
		return Impl::fail_verify(r_error_message, vformat("Function \"%s\" METHOD_FLAG_VARARG disagrees with vararg_index.", String(p_function->name)));
	}

	// Argument types & MethodInfo parity.
	if (p_function->argument_types.size() != p_function->_argument_count) {
		return Impl::fail_verify(r_error_message, vformat("Function \"%s\" argument_types size mismatch.", String(p_function->name)));
	}
	if (p_function->method_info.arguments.size() != p_function->_argument_count) {
		return Impl::fail_verify(r_error_message, vformat("Function \"%s\" method_info.arguments size mismatch.", String(p_function->name)));
	}
	if (!p_function->method_info.arguments_metadata.is_empty() && p_function->method_info.arguments_metadata.size() != p_function->method_info.arguments.size()) {
		return Impl::fail_verify(r_error_message, vformat("Function \"%s\" method_info.arguments_metadata size mismatch.", String(p_function->name)));
	}

	// Default-argument sentinel contract.
	if (p_function->_default_arg_count < 0 || p_function->_default_arg_count > p_function->_argument_count) {
		return Impl::fail_verify(r_error_message, vformat("Function \"%s\" optional_arg_count %d out of range [0, %d].", String(p_function->name), p_function->_default_arg_count, p_function->_argument_count));
	}
	const int expected_default_jumps = p_function->_default_arg_count == 0 ? 0 : (p_function->_default_arg_count + 1);
	if (p_function->default_arguments.size() != expected_default_jumps) {
		return Impl::fail_verify(r_error_message, vformat("Function \"%s\" default_arguments size %d does not match expected %d.", String(p_function->name), p_function->default_arguments.size(), expected_default_jumps));
	}
	const int *expected_default_ptr = p_function->default_arguments.is_empty() ? nullptr : p_function->default_arguments.ptr();
	if (p_function->_default_arg_ptr != expected_default_ptr) {
		return Impl::fail_verify(r_error_message, vformat("Function \"%s\" _default_arg_ptr is out of sync.", String(p_function->name)));
	}
	if (p_function->method_info.default_arguments.size() != p_function->_default_arg_count) {
		return Impl::fail_verify(r_error_message, vformat("Function \"%s\" method_info.default_arguments size %d does not match optional_arg_count %d.", String(p_function->name), p_function->method_info.default_arguments.size(), p_function->_default_arg_count));
	}

	// Temporary slots uniqueness, non-overlap with fixed/parameter/vararg slots, and initializable type.
	const int min_temporary_slot = GDScriptFunction::FIXED_ADDRESSES_MAX + p_function->_argument_count + (has_vararg ? 1 : 0);
	LocalVector<uint8_t> seen_temporary_slots;
	seen_temporary_slots.resize(p_function->_stack_size);
	for (int slot = 0; slot < p_function->_stack_size; slot++) {
		seen_temporary_slots[slot] = 0;
	}
	for (const Pair<int, Variant::Type> &temporary_slot : p_function->temporary_slots) {
		if (temporary_slot.first < min_temporary_slot || temporary_slot.first >= p_function->_stack_size) {
			return Impl::fail_verify(r_error_message, vformat("Function \"%s\" temporary slot %d out of range [%d, %d).", String(p_function->name), temporary_slot.first, min_temporary_slot, p_function->_stack_size));
		}
		if (seen_temporary_slots[temporary_slot.first] != 0) {
			return Impl::fail_verify(r_error_message, vformat("Function \"%s\" contains duplicate temporary slot %d.", String(p_function->name), temporary_slot.first));
		}
		seen_temporary_slots[temporary_slot.first] = 1;
		if (temporary_slot.second <= Variant::NIL || temporary_slot.second >= Variant::VARIANT_MAX) {
			return Impl::fail_verify(r_error_message, vformat("Function \"%s\" temporary slot %d has invalid initializable Variant::Type %d.", String(p_function->name), temporary_slot.first, static_cast<int>(temporary_slot.second)));
		}
	}

	// Code and table pointer/count synchronization.
	if (p_function->code.is_empty() || static_cast<uint32_t>(p_function->code.size()) > MAX_CODE_WORDS) {
		return Impl::fail_verify(r_error_message, vformat("Function \"%s\" code size %d out of range [1, %d].", String(p_function->name), p_function->code.size(), MAX_CODE_WORDS));
	}
	if (!Impl::check_table_sync(p_function->code, p_function->_code_size, p_function->_code_ptr) ||
			!Impl::check_table_sync(p_function->constants, p_function->_constant_count, p_function->_constants_ptr) ||
			!Impl::check_table_sync(p_function->global_names, p_function->_global_names_count, p_function->_global_names_ptr) ||
			!Impl::check_table_sync(p_function->operator_funcs, p_function->_operator_funcs_count, p_function->_operator_funcs_ptr) ||
			!Impl::check_table_sync(p_function->setters, p_function->_setters_count, p_function->_setters_ptr) ||
			!Impl::check_table_sync(p_function->getters, p_function->_getters_count, p_function->_getters_ptr) ||
			!Impl::check_table_sync(p_function->keyed_setters, p_function->_keyed_setters_count, p_function->_keyed_setters_ptr) ||
			!Impl::check_table_sync(p_function->keyed_getters, p_function->_keyed_getters_count, p_function->_keyed_getters_ptr) ||
			!Impl::check_table_sync(p_function->indexed_setters, p_function->_indexed_setters_count, p_function->_indexed_setters_ptr) ||
			!Impl::check_table_sync(p_function->indexed_getters, p_function->_indexed_getters_count, p_function->_indexed_getters_ptr) ||
			!Impl::check_table_sync(p_function->builtin_methods, p_function->_builtin_methods_count, p_function->_builtin_methods_ptr) ||
			!Impl::check_table_sync(p_function->constructors, p_function->_constructors_count, p_function->_constructors_ptr) ||
			!Impl::check_table_sync(p_function->utilities, p_function->_utilities_count, p_function->_utilities_ptr) ||
			!Impl::check_table_sync(p_function->gds_utilities, p_function->_gds_utilities_count, p_function->_gds_utilities_ptr) ||
			!Impl::check_table_sync(p_function->methods, p_function->_methods_count, p_function->_methods_ptr) ||
			!Impl::check_table_sync(p_function->lambdas, p_function->_lambdas_count, p_function->_lambdas_ptr)) {
		return Impl::fail_verify(r_error_message, vformat("Function \"%s\" raw table pointers or counts are out of sync.", String(p_function->name)));
	}

	const int code_size = p_function->_code_size;
	const int member_count = static_cast<int>(p_owning_script->member_indices.size());
	const int global_array_size = GDScriptLanguage::get_singleton() != nullptr ? GDScriptLanguage::get_singleton()->get_global_array_size() : 0;

	LocalVector<uint8_t> valid_boundaries;
	valid_boundaries.resize(code_size);
	for (int index = 0; index < code_size; index++) {
		valid_boundaries[index] = 0;
	}

	LocalVector<int> jump_targets;
	LocalVector<uint8_t> used_fixups[FIXUP_TABLE_COUNT];
	const int fixup_counts[FIXUP_TABLE_COUNT] = {
		p_function->_operator_funcs_count,
		p_function->_setters_count,
		p_function->_getters_count,
		p_function->_keyed_setters_count,
		p_function->_keyed_getters_count,
		p_function->_indexed_setters_count,
		p_function->_indexed_getters_count,
		p_function->_builtin_methods_count,
		p_function->_constructors_count,
		p_function->_utilities_count,
		p_function->_gds_utilities_count,
		p_function->_methods_count,
	};
	for (uint32_t table_index = 0; table_index < FIXUP_TABLE_COUNT; table_index++) {
		used_fixups[table_index].resize(fixup_counts[table_index]);
		for (int entry_index = 0; entry_index < fixup_counts[table_index]; entry_index++) {
			used_fixups[table_index][entry_index] = 0;
		}
	}

	LocalVector<uint8_t> used_lambdas;
	used_lambdas.resize(p_function->_lambdas_count);
	for (int lambda_index = 0; lambda_index < p_function->_lambdas_count; lambda_index++) {
		used_lambdas[lambda_index] = 0;
	}

	HashSet<int> seen_global_store_offsets;

	// Functions executing without a GDScriptInstance (static functions and non-self lambdas)
	// cannot safely reference ADDR_TYPE_MEMBER or instance-only opcodes under release builds.
	bool has_instance = !p_function->_static;
	if (has_instance && p_lambda_depth > 0) {
		HashMap<GDScriptFunction *, GDScript::LambdaInfo>::ConstIterator self_lambda_entry = p_owning_script->lambda_info.find(const_cast<GDScriptFunction *>(p_function));
		has_instance = self_lambda_entry && self_lambda_entry->value.use_self;
	}

	// Pass 1: linear decode driven by gdscript_opcode_layout.inc.
	int instruction_pointer = 0;
	bool saw_terminal_end = false;
	while (instruction_pointer < code_size) {
		valid_boundaries[instruction_pointer] = 1;

		const int raw_opcode = p_function->_code_ptr[instruction_pointer];
		if (raw_opcode < 0 || raw_opcode > GDScriptFunction::OPCODE_END) {
			return Impl::fail_verify(r_error_message, vformat("Function \"%s\" contains unknown opcode %d at offset %d.", String(p_function->name), raw_opcode, instruction_pointer));
		}

		const GDScriptFunction::Opcode opcode = static_cast<GDScriptFunction::Opcode>(raw_opcode);
		const OpcodeLayout &layout = GDScriptFunction::get_opcode_layout(opcode);

		if (opcode == GDScriptFunction::OPCODE_END) {
			if (instruction_pointer + 1 != code_size) {
				return Impl::fail_verify(r_error_message, vformat("Function \"%s\" contains mid-stream OPCODE_END at offset %d (code_size=%d).", String(p_function->name), instruction_pointer, code_size));
			}
			saw_terminal_end = true;
			instruction_pointer += 1;
			break;
		}

		if (!has_instance && (opcode == GDScriptFunction::OPCODE_SET_MEMBER || opcode == GDScriptFunction::OPCODE_GET_MEMBER || opcode == GDScriptFunction::OPCODE_CALL_SELF_BASE || opcode == GDScriptFunction::OPCODE_CREATE_SELF_LAMBDA)) {
			return Impl::fail_verify(r_error_message, vformat("Function \"%s\" uses instance-only opcode %d without an instance.", String(p_function->name), raw_opcode));
		}

		if (opcode == GDScriptFunction::OPCODE_JUMP_TO_DEF_ARGUMENT) {
			if (p_function->_default_arg_count <= 0 || p_function->_default_arg_ptr == nullptr) {
				return Impl::fail_verify(r_error_message, vformat("Function \"%s\" uses OPCODE_JUMP_TO_DEF_ARGUMENT without default arguments.", String(p_function->name)));
			}
		}

		if (!layout.is_vararg) {
			if (layout.word_count > code_size - instruction_pointer) {
				return Impl::fail_verify(r_error_message, vformat("Function \"%s\" has truncated fixed instruction %d at offset %d.", String(p_function->name), raw_opcode, instruction_pointer));
			}

			for (uint8_t operand_index = 0; operand_index < layout.operand_count; operand_index++) {
				const int word_offset = instruction_pointer + 1 + static_cast<int>(operand_index);
				const Error operand_error = Impl::verify_operand(
						layout.operands[operand_index],
						p_function->_code_ptr[word_offset],
						word_offset,
						instruction_pointer,
						opcode,
						0,
						p_function,
						p_owning_script,
						p_function->_stack_size,
						p_function->_constant_count,
						member_count,
						has_instance,
						global_array_size,
						jump_targets,
						used_fixups,
						used_lambdas,
						seen_global_store_offsets,
						r_error_message);
				if (operand_error != OK) {
					return operand_error;
				}
			}

			// Validate constant type operands for *_NATIVE and *_SCRIPT opcodes that dereference unconditionally in release builds.
			switch (opcode) {
				case GDScriptFunction::OPCODE_CAST_TO_NATIVE:
				case GDScriptFunction::OPCODE_ASSIGN_TYPED_NATIVE:
					if (Impl::resolve_constant_object_operand<GDScriptNativeClass>(p_function->_code_ptr[instruction_pointer + 3], p_function) == nullptr) {
						return Impl::fail_verify(r_error_message, vformat("Function \"%s\" opcode %d at offset %d requires a constant GDScriptNativeClass operand.", String(p_function->name), raw_opcode, instruction_pointer));
					}
					break;
				case GDScriptFunction::OPCODE_RETURN_TYPED_NATIVE:
					if (Impl::resolve_constant_object_operand<GDScriptNativeClass>(p_function->_code_ptr[instruction_pointer + 2], p_function) == nullptr) {
						return Impl::fail_verify(r_error_message, vformat("Function \"%s\" OPCODE_RETURN_TYPED_NATIVE at offset %d requires a constant GDScriptNativeClass operand.", String(p_function->name), instruction_pointer));
					}
					break;
				case GDScriptFunction::OPCODE_TYPE_TEST_SCRIPT:
				case GDScriptFunction::OPCODE_CAST_TO_SCRIPT:
				case GDScriptFunction::OPCODE_ASSIGN_TYPED_SCRIPT:
					if (Impl::resolve_constant_object_operand<Script>(p_function->_code_ptr[instruction_pointer + 3], p_function) == nullptr) {
						return Impl::fail_verify(r_error_message, vformat("Function \"%s\" opcode %d at offset %d requires a constant Script operand.", String(p_function->name), raw_opcode, instruction_pointer));
					}
					break;
				case GDScriptFunction::OPCODE_RETURN_TYPED_SCRIPT:
					if (Impl::resolve_constant_object_operand<Script>(p_function->_code_ptr[instruction_pointer + 2], p_function) == nullptr) {
						return Impl::fail_verify(r_error_message, vformat("Function \"%s\" OPCODE_RETURN_TYPED_SCRIPT at offset %d requires a constant Script operand.", String(p_function->name), instruction_pointer));
					}
					break;
				default:
					break;
			}

			instruction_pointer += layout.word_count;
		} else {
			if (2 > code_size - instruction_pointer) {
				return Impl::fail_verify(r_error_message, vformat("Function \"%s\" has truncated variable-width instruction %d at offset %d.", String(p_function->name), raw_opcode, instruction_pointer));
			}

			const int instruction_arg_count = p_function->_code_ptr[instruction_pointer + 1];
			if (instruction_arg_count < 0 || instruction_arg_count > p_function->_instruction_args_size) {
				return Impl::fail_verify(r_error_message, vformat("Function \"%s\" variable-width instruction %d has invalid instr_arg_count %d (limit=%d).", String(p_function->name), raw_opcode, instruction_arg_count, p_function->_instruction_args_size));
			}

			if (instruction_arg_count > code_size - (instruction_pointer + 2)) {
				return Impl::fail_verify(r_error_message, vformat("Function \"%s\" variable-width instruction %d address array exceeds code_size.", String(p_function->name), raw_opcode));
			}

			const int tail_offset = instruction_pointer + 2 + instruction_arg_count;
			if (layout.word_count > code_size - tail_offset) {
				return Impl::fail_verify(r_error_message, vformat("Function \"%s\" variable-width instruction %d trailing words exceed code_size.", String(p_function->name), raw_opcode));
			}

			for (int arg_index = 0; arg_index < instruction_arg_count; arg_index++) {
				const Error address_error = Impl::verify_packed_address(
						p_function->_code_ptr[instruction_pointer + 2 + arg_index],
						p_function->_stack_size,
						p_function->_constant_count,
						member_count,
						has_instance,
						r_error_message);
				if (address_error != OK) {
					return address_error;
				}
			}

			const int32_t vararg_count_value = p_function->_code_ptr[tail_offset + layout.count_tail_offset];
			if (vararg_count_value < 0) {
				return Impl::fail_verify(r_error_message, vformat("Function \"%s\" variable-width instruction %d has negative count %d.", String(p_function->name), raw_opcode, vararg_count_value));
			}

			const int64_t expected_instr_args = static_cast<int64_t>(vararg_count_value) * static_cast<int64_t>(layout.count_multiplier) + static_cast<int64_t>(layout.count_addend);
			if (expected_instr_args != static_cast<int64_t>(instruction_arg_count)) {
				return Impl::fail_verify(r_error_message, vformat("Function \"%s\" variable-width instruction %d violates layout formula (instr_arg_count=%d, expected=%d).", String(p_function->name), raw_opcode, instruction_arg_count, static_cast<int>(expected_instr_args)));
			}

			for (uint8_t operand_index = 0; operand_index < layout.operand_count; operand_index++) {
				const int word_offset = tail_offset + static_cast<int>(operand_index);
				const Error operand_error = Impl::verify_operand(
						layout.operands[operand_index],
						p_function->_code_ptr[word_offset],
						word_offset,
						instruction_pointer,
						opcode,
						vararg_count_value,
						p_function,
						p_owning_script,
						p_function->_stack_size,
						p_function->_constant_count,
						member_count,
						has_instance,
						global_array_size,
						jump_targets,
						used_fixups,
						used_lambdas,
						seen_global_store_offsets,
						r_error_message);
				if (operand_error != OK) {
					return operand_error;
				}
			}

			instruction_pointer = tail_offset + layout.word_count;
		}
	}

	if (!saw_terminal_end || instruction_pointer != code_size) {
		return Impl::fail_verify(r_error_message, vformat("Function \"%s\" does not end with a terminal OPCODE_END.", String(p_function->name)));
	}

	// Verify every fixup table entry was referenced by at least one instruction operand.
	for (uint32_t table_index = 0; table_index < FIXUP_TABLE_COUNT; table_index++) {
		for (int entry_index = 0; entry_index < fixup_counts[table_index]; entry_index++) {
			if (used_fixups[table_index][entry_index] == 0) {
				return Impl::fail_verify(r_error_message, vformat("Function \"%s\" contains unreferenced fixup entry %d in fixup table %d.", String(p_function->name), entry_index, table_index));
			}
		}
	}

	// Verify exact 1:1 coverage between global_stores[] metadata and OPCODE_STORE_GLOBAL operands.
	if (p_global_store_offsets != nullptr) {
		GlobalStoreOffsetsMap::ConstIterator store_entry = p_global_store_offsets->find(p_function);
		const int expected_store_count = store_entry ? store_entry->value.size() : 0;
		if (expected_store_count != static_cast<int>(seen_global_store_offsets.size())) {
			return Impl::fail_verify(r_error_message, vformat("Function \"%s\" OPCODE_STORE_GLOBAL count (%d) does not match global_stores count (%d).", String(p_function->name), static_cast<int>(seen_global_store_offsets.size()), expected_store_count));
		}
		if (store_entry) {
			HashSet<int> unique_expected_offsets;
			for (int offset : store_entry->value) {
				if (unique_expected_offsets.has(offset)) {
					return Impl::fail_verify(r_error_message, vformat("Function \"%s\" contains duplicate global_stores offset %d.", String(p_function->name), offset));
				}
				unique_expected_offsets.insert(offset);
				if (!seen_global_store_offsets.has(offset)) {
					return Impl::fail_verify(r_error_message, vformat("Function \"%s\" global_stores offset %d does not target an OPCODE_STORE_GLOBAL operand.", String(p_function->name), offset));
				}
			}
		}
	}

	// Pass 2: verify all jump targets and default_arguments entries land on instruction boundaries.
	for (uint32_t target_index = 0; target_index < jump_targets.size(); target_index++) {
		const int target_offset = jump_targets[target_index];
		if (target_offset < 0 || target_offset >= code_size || valid_boundaries[target_offset] == 0) {
			return Impl::fail_verify(r_error_message, vformat("Function \"%s\" has invalid jump target %d.", String(p_function->name), target_offset));
		}
	}
	for (int default_index = 0; default_index < p_function->default_arguments.size(); default_index++) {
		const int target_offset = p_function->default_arguments[default_index];
		if (target_offset < 0 || target_offset >= code_size || valid_boundaries[target_offset] == 0) {
			return Impl::fail_verify(r_error_message, vformat("Function \"%s\" has invalid default_arguments[%d] target %d.", String(p_function->name), default_index, target_offset));
		}
	}

	// Verify nested lambdas recursively.
	for (int lambda_index = 0; lambda_index < p_function->_lambdas_count; lambda_index++) {
		GDScriptFunction *lambda_function = p_function->_lambdas_ptr[lambda_index];
		if (lambda_function == nullptr) {
			return Impl::fail_verify(r_error_message, vformat("Function \"%s\" has null lambda at index %d.", String(p_function->name), lambda_index));
		}
		HashMap<GDScriptFunction *, GDScript::LambdaInfo>::ConstIterator lambda_entry = p_owning_script->lambda_info.find(lambda_function);
		if (!lambda_entry || lambda_entry->value.capture_count < 0) {
			return Impl::fail_verify(r_error_message, vformat("Function \"%s\" lambda %d has missing or invalid lambda_info.", String(p_function->name), lambda_index));
		}
		if (used_lambdas[lambda_index] == 0) {
			return Impl::fail_verify(r_error_message, vformat("Function \"%s\" contains unreferenced lambda at index %d.", String(p_function->name), lambda_index));
		}
		Error lambda_error = verify_function(lambda_function, p_owning_script, p_global_store_offsets, p_lambda_depth + 1, r_error_message);
		if (lambda_error != OK) {
			return lambda_error;
		}
	}

	return OK;
}

Error GDScriptBytecodeVerifier::verify_class(
		const GDScript *p_script,
		const GlobalStoreOffsetsMap *p_global_store_offsets,
		String *r_error_message) {
	if (p_script == nullptr) {
		return Impl::fail_verify(r_error_message, "Script pointer is null.");
	}
	if (p_script->native.is_null()) {
		return Impl::fail_verify(r_error_message, vformat("Class \"%s\" has null native base.", p_script->fully_qualified_name));
	}

	// Verify members is a subset of member_indices.
	for (const StringName &member_name : p_script->members) {
		if (!p_script->member_indices.has(member_name)) {
			return Impl::fail_verify(r_error_message, vformat("Class \"%s\" member \"%s\" is missing from member_indices.", p_script->fully_qualified_name, String(member_name)));
		}
	}

	Error member_indices_error = Impl::verify_contiguous_member_indices(p_script->member_indices, "member_indices", r_error_message);
	if (member_indices_error != OK) {
		return member_indices_error;
	}

	Error static_indices_error = Impl::verify_contiguous_member_indices(p_script->static_variables_indices, "static_variables_indices", r_error_message);
	if (static_indices_error != OK) {
		return static_indices_error;
	}

	if (p_script->static_variables.size() != static_cast<int>(p_script->static_variables_indices.size())) {
		return Impl::fail_verify(r_error_message, vformat("Class \"%s\" static_variables storage size (%d) does not match static_variables_indices (%d).", p_script->fully_qualified_name, p_script->static_variables.size(), static_cast<int>(p_script->static_variables_indices.size())));
	}

	// Complete inherited MemberInfo contract against GDScript base class.
	if (p_script->base.is_valid()) {
		const GDScript *base_script = p_script->base.ptr();
		if (p_script->member_indices.size() < base_script->member_indices.size()) {
			return Impl::fail_verify(r_error_message, vformat("Class \"%s\" has fewer member_indices than its base class \"%s\".", p_script->fully_qualified_name, base_script->fully_qualified_name));
		}
		for (const KeyValue<StringName, GDScript::MemberInfo> &base_entry : base_script->member_indices) {
			HashMap<StringName, GDScript::MemberInfo>::ConstIterator derived_entry = p_script->member_indices.find(base_entry.key);
			if (!derived_entry) {
				return Impl::fail_verify(r_error_message, vformat("Class \"%s\" is missing inherited member \"%s\" from base \"%s\".", p_script->fully_qualified_name, String(base_entry.key), base_script->fully_qualified_name));
			}
			const GDScript::MemberInfo &base_info = base_entry.value;
			const GDScript::MemberInfo &derived_info = derived_entry->value;
			if (derived_info.index != base_info.index ||
					derived_info.setter != base_info.setter ||
					derived_info.getter != base_info.getter ||
					derived_info.data_type != base_info.data_type ||
					!(derived_info.property_info == base_info.property_info)) {
				return Impl::fail_verify(r_error_message, vformat("Class \"%s\" inherited member \"%s\" does not match base class MemberInfo.", p_script->fully_qualified_name, String(base_entry.key)));
			}
		}
	}

	// Signals key == MethodInfo.name.
	for (const KeyValue<StringName, MethodInfo> &signal_entry : p_script->_signals) {
		if (StringName(signal_entry.value.name) != signal_entry.key) {
			return Impl::fail_verify(r_error_message, vformat("Class \"%s\" signal key \"%s\" does not match MethodInfo.name \"%s\".", p_script->fully_qualified_name, String(signal_entry.key), signal_entry.value.name));
		}
	}

	// Member functions key == function->name, plus initializer consistency.
	const StringName init_function_name = GDScriptLanguage::get_singleton() != nullptr ? GDScriptLanguage::get_singleton()->strings._init : SNAME("_init");
	const GDScriptFunction *expected_initializer = nullptr;
	for (const KeyValue<StringName, GDScriptFunction *> &function_entry : p_script->member_functions) {
		const GDScriptFunction *function = function_entry.value;
		if (function == nullptr) {
			return Impl::fail_verify(r_error_message, vformat("Class \"%s\" contains null member function \"%s\".", p_script->fully_qualified_name, String(function_entry.key)));
		}
		if (function->get_name() != function_entry.key) {
			return Impl::fail_verify(r_error_message, vformat("Class \"%s\" member_functions key \"%s\" does not match function->name \"%s\".", p_script->fully_qualified_name, String(function_entry.key), String(function->get_name())));
		}
		if (function_entry.key == init_function_name) {
			expected_initializer = function;
		}
		Error function_error = verify_function(function, p_script, p_global_store_offsets, 0, r_error_message);
		if (function_error != OK) {
			return function_error;
		}
	}
	if (p_script->initializer != expected_initializer) {
		return Impl::fail_verify(r_error_message, vformat("Class \"%s\" initializer pointer does not match member_functions[\"_init\"].", p_script->fully_qualified_name));
	}

	if (p_script->implicit_initializer != nullptr) {
		Error error = verify_function(p_script->implicit_initializer, p_script, p_global_store_offsets, 0, r_error_message);
		if (error != OK) {
			return error;
		}
	}
	if (p_script->implicit_ready != nullptr) {
		Error error = verify_function(p_script->implicit_ready, p_script, p_global_store_offsets, 0, r_error_message);
		if (error != OK) {
			return error;
		}
	}
	if (p_script->static_initializer != nullptr) {
		Error error = verify_function(p_script->static_initializer, p_script, p_global_store_offsets, 0, r_error_message);
		if (error != OK) {
			return error;
		}
	}

	return OK;
}

Error GDScriptBytecodeVerifier::verify_script_tree(
		const GDScript *p_root_script,
		const GlobalStoreOffsetsMap *p_global_store_offsets,
		String *r_error_message) {
	return Impl::verify_script_tree_recursive(p_root_script, p_global_store_offsets, 0, r_error_message);
}
