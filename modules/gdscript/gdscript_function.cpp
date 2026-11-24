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

#include "core/core_string_names.h"
#include "core/object/class_db.h"
#include "scene/scene_string_names.h"

std::atomic<uint64_t> GDScriptFunction::_script_clear_epoch{ 1 };

GDScriptFunction *GDScriptFunction::_populate_call_cache(int p_methodname_idx, const GDScript *p_target_script) const {
	const StringName &method_name = _global_names_ptr[p_methodname_idx];
	// `_ready` requires recursive implicit-ready dispatch in GDScriptInstance::callp,
	// and `free` is intercepted unconditionally by Object::callp before script dispatch.
	if (unlikely(method_name == SceneStringName(_ready) || method_name == CoreStringName(free_))) {
		return nullptr;
	}

	const GDScript *script_cursor = p_target_script;
	GDScriptFunction *found_function = nullptr;
	bool all_scripts_valid = true;
	while (script_cursor) {
		if (likely(script_cursor->valid)) {
			HashMap<StringName, GDScriptFunction *>::ConstIterator entry = script_cursor->member_functions.find(method_name);
			if (entry) {
				found_function = entry->value;
				break;
			}
		} else {
			all_scripts_valid = false;
		}
		script_cursor = script_cursor->base.ptr();
	}

	if (found_function == nullptr && !all_scripts_valid) {
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

#ifndef DEBUG_ENABLED
const GDScriptFunction::NativeCallCacheSlot *GDScriptFunction::_populate_native_call_cache(int p_methodname_idx, const Object *p_object, const GDScript *p_script_key) const {
	const StringName &method_name = _global_names_ptr[p_methodname_idx];
	if (unlikely(method_name == SceneStringName(_ready) || method_name == CoreStringName(free_))) {
		return nullptr;
	}

	const StringName &class_name = p_object->get_class_name();
	if (ClassDB::get_api_type(class_name) != ClassDB::API_CORE) {
		return nullptr;
	}

	MethodBind *method = ClassDB::get_method(class_name, method_name);
	if (method == nullptr || method->is_static() || method->is_vararg()) {
		return nullptr;
	}

	const void *gdtype_key = &p_object->get_gdtype();
	NativeCallCacheSlot populated;
	populated.gdtype_key = gdtype_key;
	populated.script_key = p_script_key;
	populated.method = method;
	populated.has_return = method->has_return();

	const int argument_count = method->get_argument_count();
	bool can_validate = argument_count >= 0 && argument_count <= NativeCallCacheSlot::MAX_VALIDATED_ARGS;
	if (can_validate && populated.has_return) {
		const PropertyInfo return_info = method->get_return_info();
		if (return_info.type == Variant::NIL) {
			can_validate = false;
		} else {
			populated.return_type = return_info.type;
		}
	}
	if (can_validate) {
		populated.arg_count = static_cast<uint8_t>(argument_count);
		for (int arg_index = 0; arg_index < argument_count; arg_index++) {
			const PropertyInfo arg_info = method->get_argument_info(arg_index);
			if (arg_info.type == Variant::NIL ||
					arg_info.type == Variant::OBJECT ||
					arg_info.type == Variant::ARRAY ||
					arg_info.type == Variant::DICTIONARY ||
					arg_info.hint == PROPERTY_HINT_RESOURCE_TYPE) {
				can_validate = false;
				break;
			}
			populated.arg_types[arg_index] = arg_info.type;
		}
	}
	populated.can_validated_call = can_validate;

	NativeCallInlineCache *cache_array = _native_call_cache.load(std::memory_order_acquire);
	if (unlikely(cache_array == nullptr)) {
		if (_global_names_count <= 0) {
			return nullptr;
		}
		NativeCallInlineCache *allocated = memnew_arr(NativeCallInlineCache, _global_names_count);
		NativeCallInlineCache *expected = nullptr;
		if (_native_call_cache.compare_exchange_strong(expected, allocated, std::memory_order_acq_rel, std::memory_order_acquire)) {
			cache_array = allocated;
		} else {
			memdelete_arr(allocated);
			cache_array = expected;
		}
	}

	NativeCallInlineCache &cache_entry = cache_array[p_methodname_idx];
	const uint64_t current_epoch = _script_clear_epoch.load(std::memory_order_acquire);
	uint64_t expected_meta = cache_entry.epoch_and_count.load(std::memory_order_acquire);
	if ((expected_meta & 0xFF) == NativeCallInlineCache::LOCKED_STATE) {
		return nullptr;
	}
	const uint64_t locked_meta = (expected_meta & ~uint64_t(0xFF)) | NativeCallInlineCache::LOCKED_STATE;
	if (!cache_entry.epoch_and_count.compare_exchange_strong(expected_meta, locked_meta, std::memory_order_acq_rel, std::memory_order_acquire)) {
		return nullptr;
	}

	uint32_t count = ((expected_meta >> 8) == current_epoch) ? static_cast<uint32_t>(expected_meta & 0xFF) : 0;
	const NativeCallCacheSlot *result_slot = nullptr;
	for (uint32_t slot_index = 0; slot_index < count; slot_index++) {
		if (cache_entry.slots[slot_index].gdtype_key == gdtype_key && cache_entry.slots[slot_index].script_key == p_script_key) {
			cache_entry.slots[slot_index] = populated;
			result_slot = &cache_entry.slots[slot_index];
			break;
		}
	}
	if (result_slot == nullptr && count < NativeCallInlineCache::MAX_SLOTS) {
		cache_entry.slots[count] = populated;
		result_slot = &cache_entry.slots[count];
		count++;
	}

	cache_entry.epoch_and_count.store((current_epoch << 8) | count, std::memory_order_release);
	return result_slot;
}

bool GDScriptFunction::_try_call_native_cached(int p_methodname_idx, Object *p_target_obj, const GDScript *p_script_key, const Variant *p_base, Variant **p_args, int p_argc, Variant *p_ret_slot, Callable::CallError &r_err) const {
	const void *gdtype_key = &p_target_obj->get_gdtype();
	const NativeCallCacheSlot *slot = _lookup_native_call_cache(p_methodname_idx, gdtype_key, p_script_key);
	if (unlikely(slot == nullptr)) {
		slot = _populate_native_call_cache(p_methodname_idx, p_target_obj, p_script_key);
		if (slot == nullptr) {
			return false;
		}
	}

	if (likely(slot->can_validated_call && p_argc == slot->arg_count)) {
		bool types_match = true;
		for (int arg_index = 0; arg_index < p_argc; arg_index++) {
			if (unlikely(p_args[arg_index]->get_type() != slot->arg_types[arg_index])) {
				types_match = false;
				break;
			}
		}
		if (likely(types_match)) {
			if (p_ret_slot != nullptr) {
				if (likely(slot->has_return)) {
					bool aliased = (p_ret_slot == p_base);
					for (int arg_index = 0; !aliased && arg_index < p_argc; arg_index++) {
						if (p_ret_slot == p_args[arg_index]) {
							aliased = true;
						}
					}
					if (likely(!aliased)) {
						if (unlikely(p_ret_slot->get_type() != slot->return_type)) {
							VariantInternal::initialize(p_ret_slot, slot->return_type);
						}
						slot->method->validated_call(p_target_obj, const_cast<const Variant **>(p_args), p_ret_slot);
					} else {
						Variant validated_result;
						VariantInternal::initialize(&validated_result, slot->return_type);
						slot->method->validated_call(p_target_obj, const_cast<const Variant **>(p_args), &validated_result);
						VariantInternal::assign_move_fast(p_ret_slot, validated_result);
					}
				} else {
					*p_ret_slot = Variant();
					slot->method->validated_call(p_target_obj, const_cast<const Variant **>(p_args), nullptr);
				}
			} else {
				if (unlikely(slot->has_return)) {
					Variant discarded_return;
					VariantInternal::initialize(&discarded_return, slot->return_type);
					slot->method->validated_call(p_target_obj, const_cast<const Variant **>(p_args), &discarded_return);
				} else {
					slot->method->validated_call(p_target_obj, const_cast<const Variant **>(p_args), nullptr);
				}
			}
			r_err.error = Callable::CallError::CALL_OK;
			return true;
		}
	}

	if (p_ret_slot != nullptr) {
		Variant call_result = slot->method->call(p_target_obj, const_cast<const Variant **>(p_args), p_argc, r_err);
		VariantInternal::assign_move_fast(p_ret_slot, call_result);
	} else {
		slot->method->call(p_target_obj, const_cast<const Variant **>(p_args), p_argc, r_err);
	}
	return true;
}
#endif // !DEBUG_ENABLED

bool GDScriptFunction::_populate_type_test_cache(int p_native_type_idx, const Object *p_object, const StringName &p_native_type) const {
	const StringName &class_name = p_object->get_class_name();
	const bool is_match = ClassDB::is_parent_class(class_name, p_native_type);
	if (ClassDB::get_api_type(class_name) != ClassDB::API_CORE) {
		return is_match;
	}

	TypeTestInlineCache *cache_array = _type_test_cache.load(std::memory_order_acquire);
	if (unlikely(cache_array == nullptr)) {
		if (_global_names_count <= 0) {
			return is_match;
		}
		TypeTestInlineCache *allocated = memnew_arr(TypeTestInlineCache, _global_names_count);
		TypeTestInlineCache *expected = nullptr;
		if (_type_test_cache.compare_exchange_strong(expected, allocated, std::memory_order_acq_rel, std::memory_order_acquire)) {
			cache_array = allocated;
		} else {
			memdelete_arr(allocated);
			cache_array = expected;
		}
	}

	TypeTestInlineCache &cache_entry = cache_array[p_native_type_idx];
	const uint64_t current_epoch = _script_clear_epoch.load(std::memory_order_acquire);
	uint64_t expected_meta = cache_entry.epoch_and_count.load(std::memory_order_acquire);
	if ((expected_meta & 0xFF) == TypeTestInlineCache::LOCKED_STATE) {
		return is_match;
	}
	const uint64_t locked_meta = (expected_meta & ~uint64_t(0xFF)) | TypeTestInlineCache::LOCKED_STATE;
	if (!cache_entry.epoch_and_count.compare_exchange_strong(expected_meta, locked_meta, std::memory_order_acq_rel, std::memory_order_acquire)) {
		return is_match;
	}

	const void *gdtype_key = &p_object->get_gdtype();
	uint32_t count = ((expected_meta >> 8) == current_epoch) ? static_cast<uint32_t>(expected_meta & 0xFF) : 0;
	bool already_cached = false;
	for (uint32_t slot_index = 0; slot_index < count; slot_index++) {
		if (cache_entry.slots[slot_index].gdtype_key == gdtype_key) {
			cache_entry.slots[slot_index].is_match = is_match;
			already_cached = true;
			break;
		}
	}
	if (!already_cached && count < TypeTestInlineCache::MAX_SLOTS) {
		cache_entry.slots[count].gdtype_key = gdtype_key;
		cache_entry.slots[count].is_match = is_match;
		count++;
	}

	cache_entry.epoch_and_count.store((current_epoch << 8) | count, std::memory_order_release);
	return is_match;
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

static void _resolve_native_property_binds(
		const Object *p_object,
		const StringName &p_property_name,
		MethodBind *&r_native_getter,
		Variant::Type &r_getter_return_type,
		MethodBind *&r_native_setter,
		Variant::Type &r_setter_arg_type) {
	r_native_getter = nullptr;
	r_getter_return_type = Variant::NIL;
	r_native_setter = nullptr;
	r_setter_arg_type = Variant::NIL;

	const StringName &class_name = p_object->get_class_name();
	if (ClassDB::get_api_type(class_name) != ClassDB::API_CORE) {
		return;
	}

	bool valid_index = false;
	const int property_index = ClassDB::get_property_index(class_name, p_property_name, &valid_index);
	if (!valid_index || property_index >= 0) {
		return;
	}

	const StringName getter_name = ClassDB::get_property_getter(class_name, p_property_name);
	if (getter_name != StringName()) {
		MethodBind *candidate = ClassDB::get_method(class_name, getter_name);
		if (candidate && !candidate->is_static() && !candidate->is_vararg() && candidate->has_return() && candidate->get_argument_count() == 0) {
			const Variant::Type return_type = candidate->get_return_info().type;
			if (return_type != Variant::NIL) {
				r_native_getter = candidate;
				r_getter_return_type = return_type;
			}
		}
	}

	const StringName setter_name = ClassDB::get_property_setter(class_name, p_property_name);
	if (setter_name != StringName()) {
		MethodBind *candidate = ClassDB::get_method(class_name, setter_name);
		if (candidate && !candidate->is_static() && !candidate->is_vararg() && !candidate->has_return() && candidate->get_argument_count() == 1) {
			const Variant::Type argument_type = candidate->get_argument_info(0).type;
			if (argument_type != Variant::NIL && argument_type != Variant::OBJECT && argument_type != Variant::ARRAY && argument_type != Variant::DICTIONARY) {
				r_native_setter = candidate;
				r_setter_arg_type = argument_type;
			}
		}
	}
}

const GDScriptFunction::MemberCacheSlot *GDScriptFunction::_insert_member_cache_slot(int p_name_idx, const MemberCacheSlot &p_populated) const {
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
		if (cache_entry.slots[slot_index].key == p_populated.key && cache_entry.slots[slot_index].native_gdtype_key == p_populated.native_gdtype_key) {
			cache_entry.slots[slot_index] = p_populated;
			result_slot = &cache_entry.slots[slot_index];
			break;
		}
	}
	if (result_slot == nullptr && count < MemberInlineCache::MAX_SLOTS) {
		cache_entry.slots[count] = p_populated;
		result_slot = &cache_entry.slots[count];
		count++;
	}

	cache_entry.epoch_and_count.store((current_epoch << 8) | count, std::memory_order_release);
	return result_slot;
}

const GDScriptFunction::MemberCacheSlot *GDScriptFunction::_populate_gdscript_member_cache(int p_name_idx, const GDScript *p_target_script, const Object *p_object) const {
	const StringName &member_name = _global_names_ptr[p_name_idx];
	HashMap<StringName, GDScript::MemberInfo>::ConstIterator member_it = p_target_script->member_indices.find(member_name);
	if (member_it) {
		const GDScript::MemberInfo &member_info = member_it->value;
		MemberCacheSlot populated;
		populated.key = p_target_script;
		populated.native_gdtype_key = nullptr;
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
		return _insert_member_cache_slot(p_name_idx, populated);
	}

	const StringName &custom_get_name = GDScriptLanguage::get_singleton()->strings._get;
	const StringName &custom_set_name = GDScriptLanguage::get_singleton()->strings._set;
	bool script_has_custom_get = false;
	bool script_has_custom_set = false;
	bool script_shadows_get = false;
	bool found_script_signal = false;

	const GDScript *script_cursor = p_target_script;
	while (script_cursor != nullptr) {
		if (unlikely(!script_cursor->valid)) {
			return nullptr;
		}
		if (script_cursor->static_variables_indices.has(member_name)) {
			return nullptr;
		}
		if (!script_shadows_get && !found_script_signal) {
			if (script_cursor->constants.has(member_name)) {
				script_shadows_get = true;
			} else if (script_cursor->_signals.has(member_name)) {
				found_script_signal = true;
			} else if (script_cursor->member_functions.has(member_name) || script_cursor->subclasses.has(member_name)) {
				script_shadows_get = true;
			}
		}
		if (script_cursor->member_functions.has(custom_get_name)) {
			script_has_custom_get = true;
			if (!found_script_signal) {
				script_shadows_get = true;
			}
		}
		if (script_cursor->member_functions.has(custom_set_name)) {
			script_has_custom_set = true;
		}
		script_cursor = script_cursor->base.ptr();
	}

	if (found_script_signal) {
		MemberCacheSlot populated;
		populated.kind = MemberCacheSlot::GDSCRIPT_SIGNAL;
		populated.key = p_target_script;
		populated.native_gdtype_key = nullptr;
		return _insert_member_cache_slot(p_name_idx, populated);
	}

	MethodBind *native_getter = nullptr;
	Variant::Type getter_return_type = Variant::NIL;
	MethodBind *native_setter = nullptr;
	Variant::Type setter_arg_type = Variant::NIL;
	_resolve_native_property_binds(p_object, member_name, native_getter, getter_return_type, native_setter, setter_arg_type);

	if (script_has_custom_get || script_shadows_get) {
		native_getter = nullptr;
		getter_return_type = Variant::NIL;
	}
	if (script_has_custom_set) {
		native_setter = nullptr;
		setter_arg_type = Variant::NIL;
	}
	if (native_getter == nullptr && native_setter == nullptr) {
		return nullptr;
	}

	MemberCacheSlot populated;
	populated.kind = MemberCacheSlot::NATIVE_PROPERTY;
	populated.key = p_target_script;
	populated.native_gdtype_key = &p_object->get_gdtype();
	populated.native_getter = native_getter;
	populated.getter_return_type = getter_return_type;
	populated.native_setter = native_setter;
	populated.setter_arg_type = setter_arg_type;
	return _insert_member_cache_slot(p_name_idx, populated);
}

const GDScriptFunction::MemberCacheSlot *GDScriptFunction::_populate_native_member_cache(int p_name_idx, const Object *p_object) const {
	const StringName &property_name = _global_names_ptr[p_name_idx];
	MethodBind *native_getter = nullptr;
	Variant::Type getter_return_type = Variant::NIL;
	MethodBind *native_setter = nullptr;
	Variant::Type setter_arg_type = Variant::NIL;
	_resolve_native_property_binds(p_object, property_name, native_getter, getter_return_type, native_setter, setter_arg_type);

	if (native_getter == nullptr && native_setter == nullptr) {
		return nullptr;
	}

	const void *gdtype_key = &p_object->get_gdtype();
	MemberCacheSlot populated;
	populated.kind = MemberCacheSlot::NATIVE_PROPERTY;
	populated.key = gdtype_key;
	populated.native_gdtype_key = gdtype_key;
	populated.native_getter = native_getter;
	populated.getter_return_type = getter_return_type;
	populated.native_setter = native_setter;
	populated.setter_arg_type = setter_arg_type;
	return _insert_member_cache_slot(p_name_idx, populated);
}

const GDScriptFunction::MemberCacheSlot *GDScriptFunction::_populate_builtin_member_cache(int p_name_idx, Variant::Type p_type) const {
	const StringName &member_name = _global_names_ptr[p_name_idx];
	const Variant::ValidatedGetter builtin_getter = Variant::get_member_validated_getter(p_type, member_name);
	const Variant::ValidatedSetter builtin_setter = Variant::get_member_validated_setter(p_type, member_name);
	if (builtin_getter == nullptr && builtin_setter == nullptr) {
		return nullptr;
	}
	const Variant::Type member_type = Variant::get_member_type(p_type, member_name);
	if (member_type == Variant::NIL || member_type == Variant::VARIANT_MAX) {
		return nullptr;
	}

	MemberCacheSlot populated;
	populated.kind = MemberCacheSlot::BUILTIN_MEMBER;
	populated.key = reinterpret_cast<const void *>(static_cast<uintptr_t>(p_type));
	populated.native_gdtype_key = nullptr;
	populated.builtin_getter = builtin_getter;
	populated.builtin_setter = builtin_setter;
	populated.getter_return_type = member_type;
	populated.setter_arg_type = member_type;
	return _insert_member_cache_slot(p_name_idx, populated);
}

bool GDScriptFunction::_set_named_cached_fallback(int p_name_idx, Variant *p_dst, const Variant *p_value, Object *p_target_obj, const GDScript *p_target_script, const MemberCacheSlot *p_slot) const {
	if (p_target_obj != nullptr) {
#ifndef TOOLS_ENABLED
		const MemberCacheSlot *slot = p_slot;
		if (slot == nullptr) {
			if (p_target_script != nullptr) {
				slot = _lookup_script_member_cache(p_name_idx, p_target_script, p_target_obj);
				if (unlikely(slot == nullptr)) {
					slot = _populate_gdscript_member_cache(p_name_idx, p_target_script, p_target_obj);
				}
			} else {
				const void *gdtype_key = &p_target_obj->get_gdtype();
				slot = _lookup_member_cache(p_name_idx, gdtype_key);
				if (unlikely(slot == nullptr)) {
					slot = _populate_native_member_cache(p_name_idx, p_target_obj);
				}
			}
		}
		if (likely(slot != nullptr && slot->kind == MemberCacheSlot::NATIVE_PROPERTY && slot->native_setter != nullptr && p_value->get_type() == slot->setter_arg_type)) {
			const Variant *arg_ptr = p_value;
			slot->native_setter->validated_call(p_target_obj, &arg_ptr, nullptr);
			return true;
		}
#endif
		return false;
	}

	const Variant::Type dst_type = p_dst->get_type();
	if (dst_type != Variant::NIL && dst_type != Variant::DICTIONARY) {
		const void *type_key = reinterpret_cast<const void *>(static_cast<uintptr_t>(dst_type));
		const MemberCacheSlot *slot = _lookup_member_cache(p_name_idx, type_key);
		if (unlikely(slot == nullptr)) {
			slot = _populate_builtin_member_cache(p_name_idx, dst_type);
		}
		if (likely(slot != nullptr && slot->kind == MemberCacheSlot::BUILTIN_MEMBER && slot->builtin_setter != nullptr && p_value->get_type() == slot->setter_arg_type)) {
			slot->builtin_setter(p_dst, p_value);
			return true;
		}
	}
	return false;
}

bool GDScriptFunction::_get_named_cached_fallback(int p_name_idx, const Variant *p_src, Variant *p_dst, Object *p_target_obj, const GDScript *p_target_script, const MemberCacheSlot *p_slot) const {
	if (p_target_obj != nullptr) {
		const MemberCacheSlot *slot = p_slot;
		if (slot == nullptr) {
			if (p_target_script != nullptr) {
				slot = _lookup_script_member_cache(p_name_idx, p_target_script, p_target_obj);
				if (unlikely(slot == nullptr)) {
					slot = _populate_gdscript_member_cache(p_name_idx, p_target_script, p_target_obj);
				}
			} else {
				const void *gdtype_key = &p_target_obj->get_gdtype();
				slot = _lookup_member_cache(p_name_idx, gdtype_key);
				if (unlikely(slot == nullptr)) {
					slot = _populate_native_member_cache(p_name_idx, p_target_obj);
				}
			}
		}
		if (likely(slot != nullptr)) {
			if (slot->kind == MemberCacheSlot::NATIVE_PROPERTY) {
				if (likely(slot->native_getter != nullptr)) {
					if (likely(p_src != p_dst)) {
						if (unlikely(p_dst->get_type() != slot->getter_return_type)) {
							VariantInternal::initialize(p_dst, slot->getter_return_type);
						}
						slot->native_getter->validated_call(p_target_obj, nullptr, p_dst);
					} else {
						Variant temp_ret;
						VariantInternal::initialize(&temp_ret, slot->getter_return_type);
						slot->native_getter->validated_call(p_target_obj, nullptr, &temp_ret);
						VariantInternal::assign_move_fast(p_dst, temp_ret);
					}
					return true;
				}
			} else if (slot->kind == MemberCacheSlot::GDSCRIPT_SIGNAL) {
				const ObjectID target_id = p_target_obj->get_instance_id();
				if (unlikely(p_dst->get_type() != Variant::SIGNAL)) {
					VariantInternal::initialize(p_dst, Variant::SIGNAL);
				}
				*VariantInternal::get_signal(p_dst) = Signal(target_id, _global_names_ptr[p_name_idx]);
				return true;
			}
		}
		return false;
	}

	const Variant::Type src_type = p_src->get_type();
	if (src_type != Variant::NIL && src_type != Variant::DICTIONARY) {
		const void *type_key = reinterpret_cast<const void *>(static_cast<uintptr_t>(src_type));
		const MemberCacheSlot *slot = _lookup_member_cache(p_name_idx, type_key);
		if (unlikely(slot == nullptr)) {
			slot = _populate_builtin_member_cache(p_name_idx, src_type);
		}
		if (likely(slot != nullptr && slot->kind == MemberCacheSlot::BUILTIN_MEMBER && slot->builtin_getter != nullptr)) {
			if (likely(p_src != p_dst)) {
				if (unlikely(p_dst->get_type() != slot->getter_return_type)) {
					VariantInternal::initialize(p_dst, slot->getter_return_type);
				}
				slot->builtin_getter(p_src, p_dst);
			} else {
				Variant temp_ret;
				VariantInternal::initialize(&temp_ret, slot->getter_return_type);
				slot->builtin_getter(p_src, &temp_ret);
				VariantInternal::assign_move_fast(p_dst, temp_ret);
			}
			return true;
		}
	}
	return false;
}

bool GDScriptFunction::_set_member_cached(int p_name_idx, Object *p_owner, const Variant *p_src) const {
	const void *gdtype_key = &p_owner->get_gdtype();
	const MemberCacheSlot *slot = _lookup_member_cache(p_name_idx, gdtype_key);
	if (unlikely(slot == nullptr)) {
		slot = _populate_native_member_cache(p_name_idx, p_owner);
	}
	if (likely(slot != nullptr && slot->kind == MemberCacheSlot::NATIVE_PROPERTY && slot->native_setter != nullptr && p_src->get_type() == slot->setter_arg_type)) {
		const Variant *arg_ptr = p_src;
		slot->native_setter->validated_call(p_owner, &arg_ptr, nullptr);
		return true;
	}
	return false;
}

bool GDScriptFunction::_get_member_cached(int p_name_idx, Object *p_owner, Variant *p_dst) const {
	const void *gdtype_key = &p_owner->get_gdtype();
	const MemberCacheSlot *slot = _lookup_member_cache(p_name_idx, gdtype_key);
	if (unlikely(slot == nullptr)) {
		slot = _populate_native_member_cache(p_name_idx, p_owner);
	}
	if (likely(slot != nullptr && slot->kind == MemberCacheSlot::NATIVE_PROPERTY && slot->native_getter != nullptr)) {
		if (unlikely(p_dst->get_type() != slot->getter_return_type)) {
			VariantInternal::initialize(p_dst, slot->getter_return_type);
		}
		slot->native_getter->validated_call(p_owner, nullptr, p_dst);
		return true;
	}
	return false;
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

static bool _is_zero_bit_default_variant_type(Variant::Type p_type) {
	switch (p_type) {
		case Variant::BOOL:
		case Variant::INT:
		case Variant::FLOAT:
		case Variant::VECTOR2:
		case Variant::VECTOR2I:
		case Variant::VECTOR3:
		case Variant::VECTOR3I:
		case Variant::RECT2:
		case Variant::RECT2I:
		case Variant::VECTOR4:
		case Variant::VECTOR4I:
		case Variant::RID:
			return true;
		default:
			return false;
	}
}

void GDScriptFunction::prepare_vm_frame_metadata() {
	_zero_init_temp_slots.clear();
	_complex_temp_slots.clear();
	_deinit_slot_mask = UINT64_MAX;
	_has_deinit_slot_mask = false;
	_all_args_fast_check = false;
	_frame_bytes = 0;

	if (_stack_size > 0 && _instruction_args_size >= 0) {
		const size_t total_frame_bytes = sizeof(Variant) * static_cast<size_t>(_stack_size) + sizeof(Variant *) * static_cast<size_t>(_instruction_args_size);
		if (total_frame_bytes <= UINT32_MAX) {
			_frame_bytes = static_cast<uint32_t>(total_frame_bytes);
		}
	}

	for (const Pair<int, Variant::Type> &entry : temporary_slots) {
		if (entry.first >= FIXED_ADDRESSES_MAX && entry.first <= UINT16_MAX && _is_zero_bit_default_variant_type(entry.second)) {
			_zero_init_temp_slots.push_back(ZeroInitTempSlot{ static_cast<uint16_t>(entry.first), static_cast<uint8_t>(entry.second) });
		} else {
			_complex_temp_slots.push_back(entry);
		}
	}

	if (_argument_count >= 0 && _argument_count <= FAST_ARG_TYPE_SLOTS && argument_types.size() == _argument_count) {
		bool all_fast = true;
		for (int arg_index = 0; arg_index < _argument_count; arg_index++) {
			const GDScriptDataType &arg_type = argument_types[arg_index];
			if (arg_type.kind == GDScriptDataType::VARIANT) {
				_fast_arg_types[arg_index] = Variant::NIL;
			} else if (arg_type.kind == GDScriptDataType::BUILTIN && arg_type.builtin_type != Variant::NIL && arg_type.builtin_type != Variant::ARRAY && arg_type.builtin_type != Variant::DICTIONARY) {
				_fast_arg_types[arg_index] = static_cast<uint8_t>(arg_type.builtin_type);
			} else {
				all_fast = false;
				break;
			}
		}
		_all_args_fast_check = all_fast;
	}

	if (_stack_size < FIXED_ADDRESSES_MAX || _stack_size > 64 || _code_ptr == nullptr || _code_size <= 0 || argument_types.size() < _argument_count) {
		return;
	}

	uint64_t mask = 0;
	auto mark_stack_slot = [&](int p_slot) {
		if (p_slot >= FIXED_ADDRESSES_MAX && p_slot < _stack_size) {
			mask |= (uint64_t(1) << p_slot);
		}
	};
	auto mark_stack_address = [&](int p_address) {
		const int address_type = (p_address & ADDR_TYPE_MASK) >> ADDR_BITS;
		if (address_type == ADDR_TYPE_STACK) {
			mark_stack_slot(p_address & ADDR_MASK);
		}
	};

	for (int arg_index = 0; arg_index < _argument_count; arg_index++) {
		const GDScriptDataType &arg_type = argument_types[arg_index];
		const bool is_trivial_builtin = arg_type.kind == GDScriptDataType::BUILTIN && arg_type.builtin_type >= 0 && arg_type.builtin_type < Variant::VARIANT_MAX && !VariantInternal::type_needs_deinit(arg_type.builtin_type);
		if (!is_trivial_builtin) {
			mark_stack_slot(FIXED_ADDRESSES_MAX + arg_index);
		}
	}
	if (is_vararg()) {
		mark_stack_slot(_vararg_index);
	}
	for (const Pair<int, Variant::Type> &entry : temporary_slots) {
		if (entry.second < 0 || entry.second >= Variant::VARIANT_MAX || VariantInternal::type_needs_deinit(entry.second)) {
			mark_stack_slot(entry.first);
		}
	}

	LocalVector<Pair<uint8_t, uint8_t>> stack_copy_edges;
	int ip = 0;
	while (ip < _code_size) {
		const int raw_opcode = _code_ptr[ip];
		if (raw_opcode < 0 || raw_opcode > OPCODE_END) {
			return;
		}
		const Opcode opcode = static_cast<Opcode>(raw_opcode);
		const GDScriptBytecodeFormat::OpcodeLayout &layout = get_opcode_layout(opcode);

		int instr_arg_count = 0;
		int instruction_length = layout.word_count;
		if (layout.is_vararg) {
			if (ip + 1 >= _code_size) {
				return;
			}
			instr_arg_count = _code_ptr[ip + 1];
			if (instr_arg_count < 0 || ip + 2 + instr_arg_count + layout.word_count > _code_size) {
				return;
			}
			instruction_length = 2 + instr_arg_count + layout.word_count;
		} else if (ip + instruction_length > _code_size) {
			return;
		}

		switch (opcode) {
			// Guaranteed non-deinit destination writes and pure read/control-flow opcodes.
			case OPCODE_INT_ADD:
			case OPCODE_INT_SUB:
			case OPCODE_INT_MUL:
			case OPCODE_INT_BIT_AND:
			case OPCODE_INT_BIT_OR:
			case OPCODE_INT_BIT_XOR:
			case OPCODE_INT_EQ:
			case OPCODE_INT_NE:
			case OPCODE_INT_LT:
			case OPCODE_INT_LE:
			case OPCODE_INT_GT:
			case OPCODE_INT_GE:
			case OPCODE_FLOAT_ADD:
			case OPCODE_FLOAT_SUB:
			case OPCODE_FLOAT_MUL:
			case OPCODE_FLOAT_DIV:
			case OPCODE_FLOAT_LT:
			case OPCODE_FLOAT_LE:
			case OPCODE_FLOAT_GT:
			case OPCODE_FLOAT_GE:
			case OPCODE_VECTOR2_ADD:
			case OPCODE_VECTOR2_SUB:
			case OPCODE_VECTOR2_MUL_FLOAT:
			case OPCODE_VECTOR2_MUL:
			case OPCODE_INT_SHIFT_LEFT:
			case OPCODE_INT_SHIFT_RIGHT:
			case OPCODE_FLOAT_EQ:
			case OPCODE_FLOAT_NE:
			case OPCODE_VECTOR2I_ADD:
			case OPCODE_VECTOR2I_SUB:
			case OPCODE_VECTOR2I_EQ:
			case OPCODE_VECTOR2I_NE:
			case OPCODE_INT_NEGATE:
			case OPCODE_FLOAT_NEGATE:
			case OPCODE_VECTOR2_GET_X:
			case OPCODE_VECTOR2_GET_Y:
			case OPCODE_VECTOR2_SET_X:
			case OPCODE_VECTOR2_SET_Y:
			case OPCODE_VECTOR2I_GET_X:
			case OPCODE_VECTOR2I_GET_Y:
			case OPCODE_VECTOR2I_SET_X:
			case OPCODE_VECTOR2I_SET_Y:
			case OPCODE_ARRAY_SIZE:
			case OPCODE_ARRAY_IS_EMPTY:
			case OPCODE_ARRAY_APPEND:
			case OPCODE_VECTOR2_LENGTH_SQUARED:
			case OPCODE_VECTOR2_DOT:
			case OPCODE_UTILITY_ABSI:
			case OPCODE_UTILITY_ABSF:
			case OPCODE_UTILITY_MINI:
			case OPCODE_UTILITY_MAXI:
			case OPCODE_UTILITY_MINF:
			case OPCODE_UTILITY_MAXF:
			case OPCODE_UTILITY_CLAMPI:
			case OPCODE_UTILITY_CLAMPF:
			case OPCODE_UTILITY_LERPF:
			case OPCODE_TYPE_TEST_BUILTIN:
			case OPCODE_TYPE_TEST_ARRAY:
			case OPCODE_TYPE_TEST_DICTIONARY:
			case OPCODE_TYPE_TEST_NATIVE:
			case OPCODE_TYPE_TEST_SCRIPT:
			case OPCODE_SET_KEYED:
			case OPCODE_SET_KEYED_VALIDATED:
			case OPCODE_SET_INDEXED_VALIDATED:
			case OPCODE_SET_NAMED:
			case OPCODE_SET_NAMED_VALIDATED:
			case OPCODE_SET_MEMBER:
			case OPCODE_SET_STATIC_VARIABLE:
			case OPCODE_ASSIGN_NULL:
			case OPCODE_ASSIGN_TRUE:
			case OPCODE_ASSIGN_FALSE:
			case OPCODE_CALL:
			case OPCODE_CALL_METHOD_BIND:
			case OPCODE_CALL_METHOD_BIND_VALIDATED_NO_RETURN:
			case OPCODE_CALL_NATIVE_STATIC_VALIDATED_NO_RETURN:
			case OPCODE_AWAIT:
			case OPCODE_JUMP:
			case OPCODE_JUMP_IF:
			case OPCODE_JUMP_IF_NOT:
			case OPCODE_JUMP_IF_NOT_INT_EQ:
			case OPCODE_JUMP_IF_NOT_INT_NE:
			case OPCODE_JUMP_IF_NOT_INT_LT:
			case OPCODE_JUMP_IF_NOT_INT_LE:
			case OPCODE_JUMP_IF_NOT_INT_GT:
			case OPCODE_JUMP_IF_NOT_INT_GE:
			case OPCODE_JUMP_IF_NOT_FLOAT_LT:
			case OPCODE_JUMP_IF_NOT_FLOAT_LE:
			case OPCODE_JUMP_IF_NOT_FLOAT_GT:
			case OPCODE_JUMP_IF_NOT_FLOAT_GE:
			case OPCODE_JUMP_TO_DEF_ARGUMENT:
			case OPCODE_JUMP_IF_SHARED:
			case OPCODE_RETURN:
			case OPCODE_RETURN_TYPED_BUILTIN:
			case OPCODE_RETURN_TYPED_ARRAY:
			case OPCODE_RETURN_TYPED_DICTIONARY:
			case OPCODE_RETURN_TYPED_NATIVE:
			case OPCODE_RETURN_TYPED_SCRIPT:
			case OPCODE_ITERATE_BEGIN_INT:
			case OPCODE_ITERATE_BEGIN_FLOAT:
			case OPCODE_ITERATE_BEGIN_VECTOR2:
			case OPCODE_ITERATE_BEGIN_VECTOR2I:
			case OPCODE_ITERATE_BEGIN_VECTOR3:
			case OPCODE_ITERATE_BEGIN_VECTOR3I:
			case OPCODE_ITERATE_BEGIN_PACKED_BYTE_ARRAY:
			case OPCODE_ITERATE_BEGIN_PACKED_INT32_ARRAY:
			case OPCODE_ITERATE_BEGIN_PACKED_INT64_ARRAY:
			case OPCODE_ITERATE_BEGIN_PACKED_FLOAT32_ARRAY:
			case OPCODE_ITERATE_BEGIN_PACKED_FLOAT64_ARRAY:
			case OPCODE_ITERATE_BEGIN_PACKED_VECTOR2_ARRAY:
			case OPCODE_ITERATE_BEGIN_PACKED_VECTOR3_ARRAY:
			case OPCODE_ITERATE_BEGIN_PACKED_COLOR_ARRAY:
			case OPCODE_ITERATE_BEGIN_PACKED_VECTOR4_ARRAY:
			case OPCODE_ITERATE_BEGIN_RANGE:
			case OPCODE_ITERATE_INT:
			case OPCODE_ITERATE_FLOAT:
			case OPCODE_ITERATE_VECTOR2:
			case OPCODE_ITERATE_VECTOR2I:
			case OPCODE_ITERATE_VECTOR3:
			case OPCODE_ITERATE_VECTOR3I:
			case OPCODE_ITERATE_PACKED_BYTE_ARRAY:
			case OPCODE_ITERATE_PACKED_INT32_ARRAY:
			case OPCODE_ITERATE_PACKED_INT64_ARRAY:
			case OPCODE_ITERATE_PACKED_FLOAT32_ARRAY:
			case OPCODE_ITERATE_PACKED_FLOAT64_ARRAY:
			case OPCODE_ITERATE_PACKED_VECTOR2_ARRAY:
			case OPCODE_ITERATE_PACKED_VECTOR3_ARRAY:
			case OPCODE_ITERATE_PACKED_COLOR_ARRAY:
			case OPCODE_ITERATE_PACKED_VECTOR4_ARRAY:
			case OPCODE_ITERATE_RANGE:
			case OPCODE_TYPE_ADJUST_BOOL:
			case OPCODE_TYPE_ADJUST_INT:
			case OPCODE_TYPE_ADJUST_FLOAT:
			case OPCODE_TYPE_ADJUST_VECTOR2:
			case OPCODE_TYPE_ADJUST_VECTOR2I:
			case OPCODE_TYPE_ADJUST_RECT2:
			case OPCODE_TYPE_ADJUST_RECT2I:
			case OPCODE_TYPE_ADJUST_VECTOR3:
			case OPCODE_TYPE_ADJUST_VECTOR3I:
			case OPCODE_TYPE_ADJUST_VECTOR4:
			case OPCODE_TYPE_ADJUST_VECTOR4I:
			case OPCODE_TYPE_ADJUST_PLANE:
			case OPCODE_TYPE_ADJUST_QUATERNION:
			case OPCODE_TYPE_ADJUST_COLOR:
			case OPCODE_TYPE_ADJUST_RID:
			case OPCODE_ASSERT:
			case OPCODE_BREAKPOINT:
			case OPCODE_LINE:
			case OPCODE_END:
				break;

			case OPCODE_ASSIGN: {
				const int destination_address = _code_ptr[ip + 1];
				const int source_address = _code_ptr[ip + 2];
				const int destination_type = (destination_address & ADDR_TYPE_MASK) >> ADDR_BITS;
				const int destination_slot = destination_address & ADDR_MASK;
				if (destination_type == ADDR_TYPE_STACK && destination_slot >= FIXED_ADDRESSES_MAX && destination_slot < _stack_size) {
					const int source_type = (source_address & ADDR_TYPE_MASK) >> ADDR_BITS;
					const int source_index = source_address & ADDR_MASK;
					if (source_type == ADDR_TYPE_CONSTANT && source_index >= 0 && source_index < _constant_count && _constants_ptr != nullptr) {
						const Variant::Type constant_type = _constants_ptr[source_index].get_type();
						if (VariantInternal::type_needs_deinit(constant_type)) {
							mark_stack_slot(destination_slot);
						}
					} else if (source_type == ADDR_TYPE_STACK) {
						if (source_index == ADDR_STACK_NIL) {
							// NIL never requires deinit.
						} else if (source_index == ADDR_STACK_SELF || source_index == ADDR_STACK_CLASS) {
							mark_stack_slot(destination_slot);
						} else if (source_index >= FIXED_ADDRESSES_MAX && source_index < _stack_size) {
							stack_copy_edges.push_back(Pair<uint8_t, uint8_t>(static_cast<uint8_t>(destination_slot), static_cast<uint8_t>(source_index)));
						} else {
							mark_stack_slot(destination_slot);
						}
					} else {
						mark_stack_slot(destination_slot);
					}
				}
			} break;

			case OPCODE_ASSIGN_TYPED_BUILTIN: {
				const int builtin_type = _code_ptr[ip + 3];
				if (builtin_type < 0 || builtin_type >= Variant::VARIANT_MAX || VariantInternal::type_needs_deinit(static_cast<Variant::Type>(builtin_type))) {
					mark_stack_address(_code_ptr[ip + 1]);
				}
			} break;

			case OPCODE_CAST_TO_BUILTIN: {
				const int builtin_type = _code_ptr[ip + 3];
				if (builtin_type < 0 || builtin_type >= Variant::VARIANT_MAX || VariantInternal::type_needs_deinit(static_cast<Variant::Type>(builtin_type))) {
					mark_stack_address(_code_ptr[ip + 2]);
				}
			} break;

			case OPCODE_GET_MEMBER:
			case OPCODE_GET_STATIC_VARIABLE:
			case OPCODE_AWAIT_RESUME:
			case OPCODE_STORE_GLOBAL:
			case OPCODE_STORE_NAMED_GLOBAL:
			case OPCODE_ASSIGN_TYPED_ARRAY:
			case OPCODE_ASSIGN_TYPED_DICTIONARY:
			case OPCODE_ASSIGN_TYPED_NATIVE:
			case OPCODE_ASSIGN_TYPED_SCRIPT:
			case OPCODE_TYPE_ADJUST_STRING:
			case OPCODE_TYPE_ADJUST_TRANSFORM2D:
			case OPCODE_TYPE_ADJUST_AABB:
			case OPCODE_TYPE_ADJUST_BASIS:
			case OPCODE_TYPE_ADJUST_TRANSFORM3D:
			case OPCODE_TYPE_ADJUST_PROJECTION:
			case OPCODE_TYPE_ADJUST_STRING_NAME:
			case OPCODE_TYPE_ADJUST_NODE_PATH:
			case OPCODE_TYPE_ADJUST_OBJECT:
			case OPCODE_TYPE_ADJUST_CALLABLE:
			case OPCODE_TYPE_ADJUST_SIGNAL:
			case OPCODE_TYPE_ADJUST_DICTIONARY:
			case OPCODE_TYPE_ADJUST_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_BYTE_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_INT32_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_INT64_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_FLOAT32_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_FLOAT64_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_STRING_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_VECTOR2_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_VECTOR3_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_COLOR_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_VECTOR4_ARRAY:
				mark_stack_address(_code_ptr[ip + 1]);
				break;

			case OPCODE_GET_NAMED:
			case OPCODE_GET_NAMED_VALIDATED:
			case OPCODE_CAST_TO_NATIVE:
			case OPCODE_CAST_TO_SCRIPT:
				mark_stack_address(_code_ptr[ip + 2]);
				break;

			case OPCODE_OPERATOR:
			case OPCODE_OPERATOR_VALIDATED:
			case OPCODE_GET_KEYED:
			case OPCODE_GET_KEYED_VALIDATED:
			case OPCODE_GET_INDEXED_VALIDATED:
				mark_stack_address(_code_ptr[ip + 3]);
				break;

			case OPCODE_CALL_RETURN:
			case OPCODE_CALL_ASYNC:
			case OPCODE_CALL_METHOD_BIND_RET:
			case OPCODE_CALL_METHOD_BIND_VALIDATED_RETURN:
			case OPCODE_CALL_UTILITY:
			case OPCODE_CALL_UTILITY_VALIDATED:
			case OPCODE_CALL_GDSCRIPT_UTILITY:
			case OPCODE_CALL_BUILTIN_TYPE_VALIDATED:
			case OPCODE_CALL_SELF_BASE:
			case OPCODE_CALL_BUILTIN_STATIC:
			case OPCODE_CALL_NATIVE_STATIC:
			case OPCODE_CALL_NATIVE_STATIC_VALIDATED_RETURN:
			case OPCODE_CONSTRUCT:
			case OPCODE_CONSTRUCT_VALIDATED:
			case OPCODE_CONSTRUCT_ARRAY:
			case OPCODE_CONSTRUCT_DICTIONARY:
			case OPCODE_CREATE_LAMBDA:
			case OPCODE_CREATE_SELF_LAMBDA:
				if (instr_arg_count > 0) {
					mark_stack_address(_code_ptr[ip + 2 + instr_arg_count - 1]);
				}
				break;

			default: {
				if (layout.is_vararg) {
					for (int arg_index = 0; arg_index < instr_arg_count; arg_index++) {
						mark_stack_address(_code_ptr[ip + 2 + arg_index]);
					}
				} else {
					for (int operand_index = 0; operand_index < layout.operand_count; operand_index++) {
						if (layout.operands[operand_index] == GDScriptBytecodeFormat::OPERAND_ADDRESS) {
							mark_stack_address(_code_ptr[ip + 1 + operand_index]);
						}
					}
				}
			} break;
		}

		ip += instruction_length;
	}

	bool changed = true;
	while (changed) {
		changed = false;
		for (const Pair<uint8_t, uint8_t> &edge : stack_copy_edges) {
			if ((mask & (uint64_t(1) << edge.second)) != 0 && (mask & (uint64_t(1) << edge.first)) == 0) {
				mask |= (uint64_t(1) << edge.first);
				changed = true;
			}
		}
	}

	mask &= ~((uint64_t(1) << FIXED_ADDRESSES_MAX) - 1);
	_deinit_slot_mask = mask;
	_has_deinit_slot_mask = true;
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
#ifndef DEBUG_ENABLED
	NativeCallInlineCache *native_call_cache = _native_call_cache.load(std::memory_order_relaxed);
	if (native_call_cache != nullptr) {
		memdelete_arr(native_call_cache);
	}
#endif
	TypeTestInlineCache *type_test_cache = _type_test_cache.load(std::memory_order_relaxed);
	if (type_test_cache != nullptr) {
		memdelete_arr(type_test_cache);
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
