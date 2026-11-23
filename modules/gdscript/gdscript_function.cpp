/**************************************************************************/
/*  gdscript_function.cpp                                                 */
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

#include "gdscript_function.h"

#include "gdscript.h"

#include "core/object/class_db.h"
#include "scene/scene_string_names.h"

std::atomic<uint64_t> GDScriptFunction::_script_clear_epoch{ 1 };

GDScriptFunction *GDScriptFunction::_populate_call_cache(int p_methodname_idx, const GDScript *p_target_script) const {
	const StringName &method_name = _global_names_ptr[p_methodname_idx];
	if (unlikely(method_name == SceneStringName(_ready))) {
		return nullptr;
	}

	const GDScript *script_cursor = p_target_script;
	GDScriptFunction *found_function = nullptr;
	while (script_cursor) {
		if (likely(script_cursor->valid)) {
			HashMap<StringName, GDScriptFunction *>::ConstIterator entry = script_cursor->member_functions.find(method_name);
			if (entry) {
				found_function = entry->value;
				break;
			}
		}
		script_cursor = script_cursor->base.ptr();
	}

	if (found_function == nullptr) {
		return nullptr;
	}

	CallInlineCache *cache_array = _call_cache.load(std::memory_order_acquire);
	if (unlikely(cache_array == nullptr)) {
		if (_global_names_count <= 0) {
			return found_function;
		}
		CallInlineCache *allocated = memnew_arr(CallInlineCache, _global_names_count);
		CallInlineCache *expected = nullptr;
		if (_call_cache.compare_exchange_strong(expected, allocated, std::memory_order_acq_rel, std::memory_order_acquire)) {
			cache_array = allocated;
		} else {
			memdelete_arr(allocated);
			cache_array = expected;
		}
	}

	CallInlineCache &cache_entry = cache_array[p_methodname_idx];
	const uint64_t current_epoch = _script_clear_epoch.load(std::memory_order_acquire);

	uint64_t expected_meta = cache_entry.epoch_and_count.load(std::memory_order_acquire);
	if ((expected_meta & 0xFF) == CallInlineCache::LOCKED_STATE) {
		return found_function;
	}
	const uint64_t locked_meta = (expected_meta & ~uint64_t(0xFF)) | CallInlineCache::LOCKED_STATE;
	if (!cache_entry.epoch_and_count.compare_exchange_strong(expected_meta, locked_meta, std::memory_order_acq_rel, std::memory_order_acquire)) {
		return found_function;
	}

	uint32_t count = ((expected_meta >> 8) == current_epoch) ? static_cast<uint32_t>(expected_meta & 0xFF) : 0;
	bool already_cached = false;
	for (uint32_t slot_index = 0; slot_index < count; slot_index++) {
		if (cache_entry.slots[slot_index].script == p_target_script) {
			cache_entry.slots[slot_index].function = found_function;
			already_cached = true;
			break;
		}
	}
	if (!already_cached && count < CallInlineCache::MAX_SLOTS) {
		cache_entry.slots[count].script = p_target_script;
		cache_entry.slots[count].function = found_function;
		count++;
	}

	cache_entry.epoch_and_count.store((current_epoch << 8) | count, std::memory_order_release);
	return found_function;
}

GDScriptFunction *GDScriptFunction::_find_gdscript_function_in_chain(const GDScript *p_script, const StringName &p_name) {
	const GDScript *script_cursor = p_script;
	while (script_cursor) {
		if (likely(script_cursor->valid)) {
			HashMap<StringName, GDScriptFunction *>::ConstIterator entry = script_cursor->member_functions.find(p_name);
			if (entry) {
				return entry->value;
			}
		}
		script_cursor = script_cursor->base.ptr();
	}
	return nullptr;
}

const GDScriptFunction::MemberCacheSlot *GDScriptFunction::_populate_gdscript_member_cache(int p_name_idx, const GDScript *p_target_script) const {
	const StringName &member_name = _global_names_ptr[p_name_idx];
	HashMap<StringName, GDScript::MemberInfo>::ConstIterator member_it = p_target_script->member_indices.find(member_name);
	if (!member_it) {
		return nullptr;
	}

	const GDScript::MemberInfo &member_info = member_it->value;
	MemberCacheSlot populated;
	populated.key = p_target_script;
	populated.member_index = member_info.index;
	populated.member_data_type = &member_info.data_type;

	if (member_info.getter == StringName() && member_info.setter == StringName()) {
		populated.kind = MemberCacheSlot::GDSCRIPT_DIRECT;
	} else {
		populated.kind = MemberCacheSlot::GDSCRIPT_ACCESSOR;
		if (member_info.getter != StringName()) {
			populated.getter_func = _find_gdscript_function_in_chain(p_target_script, member_info.getter);
			if (populated.getter_func == nullptr) {
				return nullptr;
			}
		}
		if (member_info.setter != StringName()) {
			populated.setter_func = _find_gdscript_function_in_chain(p_target_script, member_info.setter);
			if (populated.setter_func == nullptr) {
				return nullptr;
			}
		}
	}

	MemberInlineCache *cache_array = _member_cache.load(std::memory_order_acquire);
	if (unlikely(cache_array == nullptr)) {
		if (_global_names_count <= 0) {
			return nullptr;
		}
		MemberInlineCache *allocated = memnew_arr(MemberInlineCache, _global_names_count);
		MemberInlineCache *expected = nullptr;
		if (_member_cache.compare_exchange_strong(expected, allocated, std::memory_order_acq_rel, std::memory_order_acquire)) {
			cache_array = allocated;
		} else {
			memdelete_arr(allocated);
			cache_array = expected;
		}
	}

	MemberInlineCache &cache_entry = cache_array[p_name_idx];
	const uint64_t current_epoch = _script_clear_epoch.load(std::memory_order_acquire);
	uint64_t expected_meta = cache_entry.epoch_and_count.load(std::memory_order_acquire);
	if ((expected_meta & 0xFF) == MemberInlineCache::LOCKED_STATE) {
		return nullptr;
	}
	const uint64_t locked_meta = (expected_meta & ~uint64_t(0xFF)) | MemberInlineCache::LOCKED_STATE;
	if (!cache_entry.epoch_and_count.compare_exchange_strong(expected_meta, locked_meta, std::memory_order_acq_rel, std::memory_order_acquire)) {
		return nullptr;
	}

	uint32_t count = ((expected_meta >> 8) == current_epoch) ? static_cast<uint32_t>(expected_meta & 0xFF) : 0;
	const MemberCacheSlot *result_slot = nullptr;
	for (uint32_t slot_index = 0; slot_index < count; slot_index++) {
		if (cache_entry.slots[slot_index].key == p_target_script) {
			cache_entry.slots[slot_index] = populated;
			result_slot = &cache_entry.slots[slot_index];
			break;
		}
	}
	if (result_slot == nullptr && count < MemberInlineCache::MAX_SLOTS) {
		cache_entry.slots[count] = populated;
		result_slot = &cache_entry.slots[count];
		count++;
	}

	cache_entry.epoch_and_count.store((current_epoch << 8) | count, std::memory_order_release);
	return result_slot;
}

const GDScriptFunction::MemberCacheSlot *GDScriptFunction::_populate_native_member_cache(int p_name_idx, const Object *p_object) const {
	const StringName &class_name = p_object->get_class_name();
	if (ClassDB::get_api_type(class_name) != ClassDB::API_CORE) {
		return nullptr;
	}

	const StringName &property_name = _global_names_ptr[p_name_idx];
	bool valid_index = false;
	const int property_index = ClassDB::get_property_index(class_name, property_name, &valid_index);
	if (!valid_index || property_index >= 0) {
		return nullptr;
	}

	MethodBind *native_getter = nullptr;
	Variant::Type getter_return_type = Variant::NIL;
	const StringName getter_name = ClassDB::get_property_getter(class_name, property_name);
	if (getter_name != StringName()) {
		MethodBind *candidate = ClassDB::get_method(class_name, getter_name);
		if (candidate && !candidate->is_static() && !candidate->is_vararg() && candidate->has_return() && candidate->get_argument_count() == 0) {
			const Variant::Type return_type = candidate->get_return_info().type;
			if (return_type != Variant::NIL) {
				native_getter = candidate;
				getter_return_type = return_type;
			}
		}
	}

	MethodBind *native_setter = nullptr;
	Variant::Type setter_arg_type = Variant::NIL;
#ifndef TOOLS_ENABLED
	const StringName setter_name = ClassDB::get_property_setter(class_name, property_name);
	if (setter_name != StringName()) {
		MethodBind *candidate = ClassDB::get_method(class_name, setter_name);
		if (candidate && !candidate->is_static() && !candidate->is_vararg() && !candidate->has_return() && candidate->get_argument_count() == 1) {
			MethodInfo method_info;
			if (ClassDB::get_method_info(class_name, setter_name, &method_info) && method_info.arguments.size() == 1) {
				const Variant::Type argument_type = method_info.arguments[0].type;
				if (argument_type != Variant::NIL && argument_type != Variant::OBJECT && argument_type != Variant::ARRAY && argument_type != Variant::DICTIONARY) {
					native_setter = candidate;
					setter_arg_type = argument_type;
				}
			}
		}
	}
#endif

	if (native_getter == nullptr && native_setter == nullptr) {
		return nullptr;
	}

	const void *gdtype_key = &p_object->get_gdtype();
	MemberCacheSlot populated;
	populated.kind = MemberCacheSlot::NATIVE_PROPERTY;
	populated.key = gdtype_key;
	populated.native_getter = native_getter;
	populated.getter_return_type = getter_return_type;
	populated.native_setter = native_setter;
	populated.setter_arg_type = setter_arg_type;

	MemberInlineCache *cache_array = _member_cache.load(std::memory_order_acquire);
	if (unlikely(cache_array == nullptr)) {
		if (_global_names_count <= 0) {
			return nullptr;
		}
		MemberInlineCache *allocated = memnew_arr(MemberInlineCache, _global_names_count);
		MemberInlineCache *expected = nullptr;
		if (_member_cache.compare_exchange_strong(expected, allocated, std::memory_order_acq_rel, std::memory_order_acquire)) {
			cache_array = allocated;
		} else {
			memdelete_arr(allocated);
			cache_array = expected;
		}
	}

	MemberInlineCache &cache_entry = cache_array[p_name_idx];
	const uint64_t current_epoch = _script_clear_epoch.load(std::memory_order_acquire);
	uint64_t expected_meta = cache_entry.epoch_and_count.load(std::memory_order_acquire);
	if ((expected_meta & 0xFF) == MemberInlineCache::LOCKED_STATE) {
		return nullptr;
	}
	const uint64_t locked_meta = (expected_meta & ~uint64_t(0xFF)) | MemberInlineCache::LOCKED_STATE;
	if (!cache_entry.epoch_and_count.compare_exchange_strong(expected_meta, locked_meta, std::memory_order_acq_rel, std::memory_order_acquire)) {
		return nullptr;
	}

	uint32_t count = ((expected_meta >> 8) == current_epoch) ? static_cast<uint32_t>(expected_meta & 0xFF) : 0;
	const MemberCacheSlot *result_slot = nullptr;
	for (uint32_t slot_index = 0; slot_index < count; slot_index++) {
		if (cache_entry.slots[slot_index].key == gdtype_key) {
			cache_entry.slots[slot_index] = populated;
			result_slot = &cache_entry.slots[slot_index];
			break;
		}
	}
	if (result_slot == nullptr && count < MemberInlineCache::MAX_SLOTS) {
		cache_entry.slots[count] = populated;
		result_slot = &cache_entry.slots[count];
		count++;
	}

	cache_entry.epoch_and_count.store((current_epoch << 8) | count, std::memory_order_release);
	return result_slot;
}

bool GDScriptDataType::is_type(const Variant &p_variant, bool p_allow_implicit_conversion) const {
	switch (kind) {
		case VARIANT: {
			return true;
		} break;
		case BUILTIN: {
			Variant::Type var_type = p_variant.get_type();
			bool valid = builtin_type == var_type;
			if (valid && builtin_type == Variant::ARRAY && has_container_element_type(0)) {
				Array array = p_variant;
				if (array.is_typed()) {
					const GDScriptDataType &elem_type = container_element_types[0];
					Variant::Type array_builtin_type = (Variant::Type)array.get_typed_builtin();
					StringName array_native_type = array.get_typed_class_name();
					Ref<Script> array_script_type_ref = array.get_typed_script();

					if (array_script_type_ref.is_valid()) {
						valid = (elem_type.kind == SCRIPT || elem_type.kind == GDSCRIPT) && elem_type.script_type == array_script_type_ref.ptr();
					} else if (array_native_type != StringName()) {
						valid = elem_type.kind == NATIVE && elem_type.native_type == array_native_type;
					} else {
						valid = elem_type.kind == BUILTIN && elem_type.builtin_type == array_builtin_type;
					}
				} else {
					valid = false;
				}
			} else if (valid && builtin_type == Variant::DICTIONARY && has_container_element_types()) {
				Dictionary dictionary = p_variant;
				if (dictionary.is_typed()) {
					if (dictionary.is_typed_key()) {
						GDScriptDataType key = get_container_element_type_or_variant(0);
						Variant::Type key_builtin_type = (Variant::Type)dictionary.get_typed_key_builtin();
						StringName key_native_type = dictionary.get_typed_key_class_name();
						Ref<Script> key_script_type_ref = dictionary.get_typed_key_script();

						if (key_script_type_ref.is_valid()) {
							valid = (key.kind == SCRIPT || key.kind == GDSCRIPT) && key.script_type == key_script_type_ref.ptr();
						} else if (key_native_type != StringName()) {
							valid = key.kind == NATIVE && key.native_type == key_native_type;
						} else {
							valid = key.kind == BUILTIN && key.builtin_type == key_builtin_type;
						}
					}

					if (valid && dictionary.is_typed_value()) {
						GDScriptDataType value = get_container_element_type_or_variant(1);
						Variant::Type value_builtin_type = (Variant::Type)dictionary.get_typed_value_builtin();
						StringName value_native_type = dictionary.get_typed_value_class_name();
						Ref<Script> value_script_type_ref = dictionary.get_typed_value_script();

						if (value_script_type_ref.is_valid()) {
							valid = (value.kind == SCRIPT || value.kind == GDSCRIPT) && value.script_type == value_script_type_ref.ptr();
						} else if (value_native_type != StringName()) {
							valid = value.kind == NATIVE && value.native_type == value_native_type;
						} else {
							valid = value.kind == BUILTIN && value.builtin_type == value_builtin_type;
						}
					}
				} else {
					valid = false;
				}
			} else if (!valid && p_allow_implicit_conversion) {
				valid = Variant::can_convert_strict(var_type, builtin_type);
			}
			return valid;
		} break;
		case NATIVE: {
			if (p_variant.get_type() == Variant::NIL) {
				return true;
			}
			if (p_variant.get_type() != Variant::OBJECT) {
				return false;
			}

			bool was_freed = false;
			Object *obj = p_variant.get_validated_object_with_check(was_freed);
			if (!obj) {
				return !was_freed;
			}

			if (!ClassDB::is_parent_class(obj->get_class_name(), native_type)) {
				return false;
			}
			return true;
		} break;
		case SCRIPT:
		case GDSCRIPT: {
			if (p_variant.get_type() == Variant::NIL) {
				return true;
			}
			if (p_variant.get_type() != Variant::OBJECT) {
				return false;
			}

			bool was_freed = false;
			Object *obj = p_variant.get_validated_object_with_check(was_freed);
			if (!obj) {
				return !was_freed;
			}

			Ref<Script> base = obj && obj->get_script_instance() ? obj->get_script_instance()->get_script() : nullptr;
			bool valid = false;
			while (base.is_valid()) {
				if (base == script_type) {
					valid = true;
					break;
				}
				base = base->get_base_script();
			}
			return valid;
		} break;
	}
	return false;
}

/////////////////////

const GDScriptBytecodeFormat::OpcodeLayout &GDScriptFunction::get_opcode_layout(Opcode p_opcode) {
	using namespace GDScriptBytecodeFormat;

#define GDSCRIPT_OPCODE_OPERANDS_UNPACK(...) \
	{ __VA_ARGS__ }
#define GDSCRIPT_OPCODE_FIXED(m_name, m_word_count, m_operands) \
	OpcodeLayout{ false, static_cast<uint8_t>(m_word_count), 0, 0, 0, static_cast<uint8_t>((m_word_count) - 1), GDSCRIPT_OPCODE_OPERANDS_UNPACK m_operands },
#define GDSCRIPT_OPCODE_VARARG(m_name, m_trailing_words, m_count_tail_offset, m_count_multiplier, m_count_addend, m_trailing_operands) \
	OpcodeLayout{ true, static_cast<uint8_t>(m_trailing_words), static_cast<uint8_t>(m_count_tail_offset), static_cast<uint8_t>(m_count_multiplier), static_cast<uint8_t>(m_count_addend), static_cast<uint8_t>(m_trailing_words), GDSCRIPT_OPCODE_OPERANDS_UNPACK m_trailing_operands },

	static constexpr OpcodeLayout opcode_layout_table[] = {
#include "gdscript_opcode_layout.inc"
	};

#undef GDSCRIPT_OPCODE_FIXED
#undef GDSCRIPT_OPCODE_VARARG
#undef GDSCRIPT_OPCODE_OPERANDS_UNPACK

	static_assert(sizeof(opcode_layout_table) / sizeof(opcode_layout_table[0]) == GDScriptFunction::OPCODE_END + 1,
			"Opcode layout table size must match GDScriptFunction::OPCODE_END + 1.");
	static_assert(validate_opcode_layout_table(opcode_layout_table, GDScriptFunction::OPCODE_END + 1),
			"Invalid operand metadata in gdscript_opcode_layout.inc.");

	ERR_FAIL_INDEX_V(p_opcode, GDScriptFunction::OPCODE_END + 1, opcode_layout_table[GDScriptFunction::OPCODE_END]);
	return opcode_layout_table[p_opcode];
}

Variant GDScriptFunction::get_constant(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, constants.size(), "<errconst>");
	return constants[p_idx];
}

StringName GDScriptFunction::get_global_name(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, global_names.size(), "<errgname>");
	return global_names[p_idx];
}

struct _GDFKC {
	int order = 0;
	List<int> pos;
};

struct _GDFKCS {
	int order = 0;
	StringName id;
	int pos = 0;

	bool operator<(const _GDFKCS &p_r) const {
		return order < p_r.order;
	}
};

void GDScriptFunction::debug_get_stack_member_state(int p_line, List<Pair<StringName, int>> *r_stackvars) const {
	int oc = 0;
	HashMap<StringName, _GDFKC> sdmap;
	for (const StackDebug &sd : stack_debug) {
		if (sd.line >= p_line) {
			break;
		}

		if (sd.added) {
			if (!sdmap.has(sd.identifier)) {
				_GDFKC d;
				d.order = oc++;
				d.pos.push_back(sd.pos);
				sdmap[sd.identifier] = d;

			} else {
				sdmap[sd.identifier].pos.push_back(sd.pos);
			}
		} else {
			ERR_CONTINUE(!sdmap.has(sd.identifier));

			sdmap[sd.identifier].pos.pop_back();
			if (sdmap[sd.identifier].pos.is_empty()) {
				sdmap.erase(sd.identifier);
			}
		}
	}

	List<_GDFKCS> stackpositions;
	for (const KeyValue<StringName, _GDFKC> &E : sdmap) {
		_GDFKCS spp;
		spp.id = E.key;
		spp.order = E.value.order;
		spp.pos = E.value.pos.back()->get();
		stackpositions.push_back(spp);
	}

	stackpositions.sort();

	for (_GDFKCS &E : stackpositions) {
		Pair<StringName, int> p;
		p.first = E.id;
		p.second = E.pos;
		r_stackvars->push_back(p);
	}
}

GDScriptFunction::GDScriptFunction() {
	name = "<anonymous>";
#ifdef DEBUG_ENABLED
	{
		MutexLock lock(GDScriptLanguage::get_singleton()->mutex);
		GDScriptLanguage::get_singleton()->function_list.add(&function_list);
	}
#endif
}

GDScriptFunction::~GDScriptFunction() {
	CallInlineCache *cache = _call_cache.load(std::memory_order_relaxed);
	if (cache != nullptr) {
		memdelete_arr(cache);
	}
	MemberInlineCache *member_cache = _member_cache.load(std::memory_order_relaxed);
	if (member_cache != nullptr) {
		memdelete_arr(member_cache);
	}

	get_script()->member_functions.erase(name);

	for (int i = 0; i < lambdas.size(); i++) {
		memdelete(lambdas[i]);
	}

	for (int i = 0; i < argument_types.size(); i++) {
		argument_types.write[i].script_type_ref = Ref<Script>();
	}
	return_type.script_type_ref = Ref<Script>();

#ifdef DEBUG_ENABLED
	MutexLock lock(GDScriptLanguage::get_singleton()->mutex);
	GDScriptLanguage::get_singleton()->function_list.remove(&function_list);
#endif
}

/////////////////////

Variant GDScriptFunctionState::_signal_callback(const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	Variant arg;
	r_error.error = Callable::CallError::CALL_OK;

	if (p_argcount == 0) {
		r_error.error = Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS;
		r_error.expected = 1;
		return Variant();
	} else if (p_argcount == 1) {
		//noooneee
	} else if (p_argcount == 2) {
		arg = *p_args[0];
	} else {
		Array extra_args;
		for (int i = 0; i < p_argcount - 1; i++) {
			extra_args.push_back(*p_args[i]);
		}
		arg = extra_args;
	}

	Ref<GDScriptFunctionState> self = *p_args[p_argcount - 1];

	if (self.is_null()) {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_ARGUMENT;
		r_error.argument = p_argcount - 1;
		r_error.expected = Variant::OBJECT;
		return Variant();
	}

	return resume(arg);
}

bool GDScriptFunctionState::is_valid(bool p_extended_check) const {
	if (function == nullptr) {
		return false;
	}

	if (p_extended_check) {
		MutexLock lock(GDScriptLanguage::get_singleton()->mutex);

		// Script gone?
		if (!scripts_list.in_list()) {
			return false;
		}
		// Class instance gone? (if not static function)
		if (state.instance && !instances_list.in_list()) {
			return false;
		}
	}

	return true;
}

Variant GDScriptFunctionState::resume(const Variant &p_arg) {
	ERR_FAIL_NULL_V(function, Variant());
	{
		MutexLock lock(GDScriptLanguage::singleton->mutex);

		if (!scripts_list.in_list()) {
#ifdef DEBUG_ENABLED
			ERR_FAIL_V_MSG(Variant(), "Resumed function '" + state.function_name + "()' after await, but script is gone. At script: " + state.script_path + ":" + itos(state.line));
#else
			return Variant();
#endif
		}
		if (state.instance && !instances_list.in_list()) {
#ifdef DEBUG_ENABLED
			ERR_FAIL_V_MSG(Variant(), "Resumed function '" + state.function_name + "()' after await, but class instance is gone. At script: " + state.script_path + ":" + itos(state.line));
#else
			return Variant();
#endif
		}
		// Do these now to avoid locking again after the call.
		scripts_list.remove_from_list();
		instances_list.remove_from_list();
	}

	state.result = p_arg;
	Callable::CallError err;
	Variant ret = function->call(nullptr, nullptr, 0, err, &state);

	function = nullptr; // Cleaned up.
	state.result = Variant();

	return ret;
}

void GDScriptFunctionState::_clear_stack() {
	if (state.stack_size) {
		Variant *stack = (Variant *)state.stack.ptr();
		// First `GDScriptFunction::FIXED_ADDRESSES_MAX` stack addresses are special
		// and not copied to the state, so we skip them here.
		for (int i = GDScriptFunction::FIXED_ADDRESSES_MAX; i < state.stack_size; i++) {
			stack[i].~Variant();
		}
		state.stack_size = 0;
	}
}

void GDScriptFunctionState::_clear_connections() {
	List<Object::Connection> conns;
	get_signals_connected_to_this(&conns);

	for (Object::Connection &c : conns) {
		c.signal.disconnect(c.callable);
	}
}

void GDScriptFunctionState::_bind_methods() {
	ClassDB::bind_method(D_METHOD("resume", "arg"), &GDScriptFunctionState::resume, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("is_valid", "extended_check"), &GDScriptFunctionState::is_valid, DEFVAL(false));
	ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "_signal_callback", &GDScriptFunctionState::_signal_callback, MethodInfo("_signal_callback"));

	ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::NIL, "result", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT)));
}

GDScriptFunctionState::GDScriptFunctionState() :
		scripts_list(this),
		instances_list(this) {
}

GDScriptFunctionState::~GDScriptFunctionState() {
	{
		MutexLock lock(GDScriptLanguage::singleton->mutex);
		scripts_list.remove_from_list();
		instances_list.remove_from_list();
		_clear_stack();
	}
}
