/**************************************************************************/
/*  gdscript_function.h                                                   */
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

#include "gdscript_bytecode_format.h"
#include "gdscript_utility_functions.h"

#include "core/object/ref_counted.h"
#include "core/object/script_language.h"
#include "core/os/thread.h"
#include "core/string/string_name.h"
#include "core/templates/pair.h"
#include "core/templates/self_list.h"
#include "core/variant/variant.h"

#include <atomic>

class GDScriptInstance;
class GDScript;

class GDScriptDataType {
public:
	Vector<GDScriptDataType> container_element_types;

	enum Kind {
		VARIANT, // Can be any type.
		BUILTIN,
		NATIVE,
		SCRIPT,
		GDSCRIPT,
	};

	Kind kind = VARIANT;

	Variant::Type builtin_type = Variant::NIL;
	StringName native_type;
	Script *script_type = nullptr;
	Ref<Script> script_type_ref;

	_FORCE_INLINE_ bool has_type() const { return kind != VARIANT; }

	_FORCE_INLINE_ bool is_type_exact_fast(const Variant &p_variant) const {
		if (kind == VARIANT) {
			return true;
		}
		if (kind == BUILTIN && builtin_type != Variant::ARRAY && builtin_type != Variant::DICTIONARY) {
			return p_variant.get_type() == builtin_type;
		}
		return is_type(p_variant, false);
	}

	bool is_type(const Variant &p_variant, bool p_allow_implicit_conversion = false) const;

	bool can_contain_object() const {
		if (kind == BUILTIN) {
			switch (builtin_type) {
				case Variant::ARRAY:
					if (has_container_element_type(0)) {
						return container_element_types[0].can_contain_object();
					}
					return true;
				case Variant::DICTIONARY:
					if (has_container_element_types()) {
						return get_container_element_type_or_variant(0).can_contain_object() || get_container_element_type_or_variant(1).can_contain_object();
					}
					return true;
				case Variant::NIL:
				case Variant::OBJECT:
					return true;
				default:
					return false;
			}
		}
		return true;
	}

	void set_container_element_type(int p_index, const GDScriptDataType &p_element_type) {
		ERR_FAIL_COND(p_index < 0);
		while (p_index >= container_element_types.size()) {
			container_element_types.push_back(GDScriptDataType());
		}
		container_element_types.write[p_index] = GDScriptDataType(p_element_type);
	}

	GDScriptDataType get_container_element_type(int p_index) const {
		ERR_FAIL_INDEX_V(p_index, container_element_types.size(), GDScriptDataType());
		return container_element_types[p_index];
	}

	GDScriptDataType get_container_element_type_or_variant(int p_index) const {
		if (p_index < 0 || p_index >= container_element_types.size()) {
			return GDScriptDataType();
		}
		return container_element_types[p_index];
	}

	bool has_container_element_type(int p_index) const {
		return p_index >= 0 && p_index < container_element_types.size();
	}

	bool has_container_element_types() const {
		return !container_element_types.is_empty();
	}

	GDScriptDataType() = default;

	bool operator==(const GDScriptDataType &p_other) const {
		return kind == p_other.kind &&
				builtin_type == p_other.builtin_type &&
				native_type == p_other.native_type &&
				(script_type == p_other.script_type || script_type_ref == p_other.script_type_ref) &&
				container_element_types == p_other.container_element_types;
	}

	bool operator!=(const GDScriptDataType &p_other) const {
		return !(*this == p_other);
	}

	void operator=(const GDScriptDataType &p_other) {
		kind = p_other.kind;
		builtin_type = p_other.builtin_type;
		native_type = p_other.native_type;
		script_type = p_other.script_type;
		script_type_ref = p_other.script_type_ref;
		container_element_types = p_other.container_element_types;
	}

	GDScriptDataType(const GDScriptDataType &p_other) {
		*this = p_other;
	}

	~GDScriptDataType() {}
};

class GDScriptFunction {
public:
	static constexpr int OPERATOR_INLINE_CACHE_PTR_WORDS = GDScriptBytecodeFormat::OPERATOR_INLINE_CACHE_PTR_WORDS;
	static_assert(sizeof(Variant::ValidatedOperatorEvaluator) <= sizeof(int) * OPERATOR_INLINE_CACHE_PTR_WORDS,
			"OPERATOR_INLINE_CACHE_PTR_WORDS must be large enough to store Variant::ValidatedOperatorEvaluator.");

	enum Opcode {
#define GDSCRIPT_OPCODE_FIXED(m_name, m_word_count, m_operands) m_name,
#define GDSCRIPT_OPCODE_VARARG(m_name, m_trailing_words, m_count_tail_offset, m_count_multiplier, m_count_addend, m_trailing_operands) m_name,
#include "gdscript_opcode_layout.inc"
#undef GDSCRIPT_OPCODE_FIXED
#undef GDSCRIPT_OPCODE_VARARG
	};

	static const GDScriptBytecodeFormat::OpcodeLayout &get_opcode_layout(Opcode p_opcode);

#ifdef GDSCRIPT_BYTECODE_EXPORT_ENABLED
	struct ExportFixups {
		struct OperatorFixup {
			Variant::Operator operation = Variant::OP_EQUAL;
			Variant::Type left_type = Variant::NIL;
			Variant::Type right_type = Variant::NIL;
		};

		struct MemberFixup {
			Variant::Type builtin_type = Variant::NIL;
			StringName member_name;
		};

		struct BuiltinMethodFixup {
			Variant::Type builtin_type = Variant::NIL;
			StringName method_name;
		};

		struct ConstructorFixup {
			Variant::Type builtin_type = Variant::NIL;
			int constructor_index = 0;
		};

		struct MethodBindFixup {
			StringName class_name;
			StringName method_name;
		};

		struct GlobalStoreFixup {
			int code_offset = 0;
			StringName global_name;
		};

		Vector<OperatorFixup> operators;
		Vector<MemberFixup> setters;
		Vector<MemberFixup> getters;
		Vector<Variant::Type> keyed_setters;
		Vector<Variant::Type> keyed_getters;
		Vector<Variant::Type> indexed_setters;
		Vector<Variant::Type> indexed_getters;
		Vector<BuiltinMethodFixup> builtin_methods;
		Vector<ConstructorFixup> constructors;
		Vector<StringName> utilities;
		Vector<StringName> gds_utilities;
		Vector<MethodBindFixup> method_binds;

		Vector<GlobalStoreFixup> global_stores;
		// Bytecode word offsets of OPCODE_OPERATOR instructions whose mutable inline cache
		// occupies [offset + 5 .. offset + 6 + OPERATOR_INLINE_CACHE_PTR_WORDS].
		Vector<int> operator_cache_offsets;
		Vector<StringName> named_globals;
	};
#endif

	enum Address {
		ADDR_BITS = 24,
		ADDR_MASK = ((1 << ADDR_BITS) - 1),
		ADDR_TYPE_MASK = ~ADDR_MASK,
		ADDR_TYPE_STACK = 0,
		ADDR_TYPE_CONSTANT = 1,
		ADDR_TYPE_MEMBER = 2,
		ADDR_TYPE_MAX = 3,
	};

	enum FixedAddresses {
		ADDR_STACK_SELF = 0,
		ADDR_STACK_CLASS = 1,
		ADDR_STACK_NIL = 2,
		FIXED_ADDRESSES_MAX = 3,
		ADDR_SELF = ADDR_STACK_SELF | (ADDR_TYPE_STACK << ADDR_BITS),
		ADDR_CLASS = ADDR_STACK_CLASS | (ADDR_TYPE_STACK << ADDR_BITS),
		ADDR_NIL = ADDR_STACK_NIL | (ADDR_TYPE_STACK << ADDR_BITS),
	};

	struct StackDebug {
		int line;
		int pos;
		bool added;
		StringName identifier;
	};

private:
	friend class GDScript;
	friend class GDScriptCompiler;
	friend class GDScriptByteCodeGenerator;
	friend class GDScriptLanguage;
	friend class GDScriptBytecodeExporter;
	friend class GDScriptBytecodeLoader;
	friend class GDScriptBytecodeVerifier;
#ifdef TESTS_ENABLED
	friend class GDScriptBytecodeAbiTestAccessor;
	friend class GDScriptBytecodeTestAccessor;
#endif

	StringName name;
	StringName source;
	bool _static = false;
	Vector<GDScriptDataType> argument_types;
	GDScriptDataType return_type;
	MethodInfo method_info;
	Variant rpc_config;

	GDScript *_script = nullptr;
	int _initial_line = 0;
	int _argument_count = 0;
	int _vararg_index = -1;
	int _stack_size = 0;
	int _instruction_args_size = 0;

	SelfList<GDScriptFunction> function_list{ this };
	mutable Variant nil;
	TightLocalVector<Pair<int, Variant::Type>> temporary_slots;
	List<StackDebug> stack_debug;

	Vector<int> code;
	Vector<int> default_arguments;
	Vector<Variant> constants;
	HashMap<StringName, Variant> constant_map;
	Vector<StringName> global_names;
	Vector<Variant::ValidatedOperatorEvaluator> operator_funcs;
	Vector<Variant::ValidatedSetter> setters;
	Vector<Variant::ValidatedGetter> getters;
	Vector<Variant::ValidatedKeyedSetter> keyed_setters;
	Vector<Variant::ValidatedKeyedGetter> keyed_getters;
	Vector<Variant::ValidatedIndexedSetter> indexed_setters;
	Vector<Variant::ValidatedIndexedGetter> indexed_getters;
	Vector<Variant::ValidatedBuiltInMethod> builtin_methods;
	Vector<Variant::ValidatedConstructor> constructors;
	Vector<Variant::ValidatedUtilityFunction> utilities;
	Vector<GDScriptUtilityFunctions::FunctionPtr> gds_utilities;
	Vector<MethodBind *> methods;
	Vector<GDScriptFunction *> lambdas;
#ifdef GDSCRIPT_BYTECODE_EXPORT_ENABLED
	ExportFixups export_fixups;
#endif

	int _code_size = 0;
	int _default_arg_count = 0;
	int _constant_count = 0;
	int _global_names_count = 0;
	int _operator_funcs_count = 0;
	int _setters_count = 0;
	int _getters_count = 0;
	int _keyed_setters_count = 0;
	int _keyed_getters_count = 0;
	int _indexed_setters_count = 0;
	int _indexed_getters_count = 0;
	int _builtin_methods_count = 0;
	int _constructors_count = 0;
	int _utilities_count = 0;
	int _gds_utilities_count = 0;
	int _methods_count = 0;
	int _lambdas_count = 0;

	int *_code_ptr = nullptr;
	const int *_default_arg_ptr = nullptr;
	mutable Variant *_constants_ptr = nullptr;
	const StringName *_global_names_ptr = nullptr;
	const Variant::ValidatedOperatorEvaluator *_operator_funcs_ptr = nullptr;
	const Variant::ValidatedSetter *_setters_ptr = nullptr;
	const Variant::ValidatedGetter *_getters_ptr = nullptr;
	const Variant::ValidatedKeyedSetter *_keyed_setters_ptr = nullptr;
	const Variant::ValidatedKeyedGetter *_keyed_getters_ptr = nullptr;
	const Variant::ValidatedIndexedSetter *_indexed_setters_ptr = nullptr;
	const Variant::ValidatedIndexedGetter *_indexed_getters_ptr = nullptr;
	const Variant::ValidatedBuiltInMethod *_builtin_methods_ptr = nullptr;
	const Variant::ValidatedConstructor *_constructors_ptr = nullptr;
	const Variant::ValidatedUtilityFunction *_utilities_ptr = nullptr;
	const GDScriptUtilityFunctions::FunctionPtr *_gds_utilities_ptr = nullptr;
	MethodBind **_methods_ptr = nullptr;
	GDScriptFunction **_lambdas_ptr = nullptr;

	struct CallCacheSlot {
		const GDScript *script = nullptr;
		GDScriptFunction *function = nullptr;
	};

	struct CallInlineCache {
		static constexpr uint32_t MAX_SLOTS = 4;
		static constexpr uint32_t LOCKED_STATE = 0xFF;
		std::atomic<uint64_t> epoch_and_count{ 0 };
		CallCacheSlot slots[MAX_SLOTS];
	};

	mutable std::atomic<CallInlineCache *> _call_cache{ nullptr };

	struct MemberCacheSlot {
		enum Kind : uint8_t {
			NONE = 0,
			GDSCRIPT_DIRECT,
			GDSCRIPT_ACCESSOR,
			NATIVE_PROPERTY,
		};
		Kind kind = NONE;
		Variant::Type getter_return_type = Variant::NIL;
		Variant::Type setter_arg_type = Variant::NIL;
		int member_index = -1;
		const void *key = nullptr;
		const GDScriptDataType *member_data_type = nullptr;
		GDScriptFunction *getter_func = nullptr;
		GDScriptFunction *setter_func = nullptr;
		MethodBind *native_getter = nullptr;
		MethodBind *native_setter = nullptr;
	};

	struct MemberInlineCache {
		static constexpr uint32_t MAX_SLOTS = 4;
		static constexpr uint32_t LOCKED_STATE = 0xFF;
		std::atomic<uint64_t> epoch_and_count{ 0 };
		MemberCacheSlot slots[MAX_SLOTS];
	};

	mutable std::atomic<MemberInlineCache *> _member_cache{ nullptr };
	static std::atomic<uint64_t> _script_clear_epoch;

	static GDScriptFunction *_find_gdscript_function_in_chain(const GDScript *p_script, const StringName &p_name);
	GDScriptFunction *_populate_call_cache(int p_methodname_idx, const GDScript *p_target_script) const;
	const MemberCacheSlot *_populate_gdscript_member_cache(int p_name_idx, const GDScript *p_target_script) const;
	const MemberCacheSlot *_populate_native_member_cache(int p_name_idx, const Object *p_object) const;

	_FORCE_INLINE_ const MemberCacheSlot *_lookup_member_cache(int p_name_idx, const void *p_key) const {
		MemberInlineCache *cache_array = _member_cache.load(std::memory_order_acquire);
		if (likely(cache_array != nullptr)) {
			const MemberInlineCache &entry = cache_array[p_name_idx];
			const uint64_t current_epoch = _script_clear_epoch.load(std::memory_order_relaxed);
			const uint64_t meta = entry.epoch_and_count.load(std::memory_order_acquire);
			if (likely((meta >> 8) == current_epoch)) {
				const uint32_t count = static_cast<uint32_t>(meta & 0xFF);
				if (likely(count <= MemberInlineCache::MAX_SLOTS)) {
					for (uint32_t slot_index = 0; slot_index < count; slot_index++) {
						if (likely(entry.slots[slot_index].key == p_key)) {
							return &entry.slots[slot_index];
						}
					}
				}
			}
		}
		return nullptr;
	}

	_FORCE_INLINE_ GDScriptFunction *_resolve_gdscript_call_cached(int p_methodname_idx, const GDScript *p_target_script) const {
		CallInlineCache *cache_array = _call_cache.load(std::memory_order_acquire);
		if (likely(cache_array != nullptr)) {
			const CallInlineCache &entry = cache_array[p_methodname_idx];
			const uint64_t current_epoch = _script_clear_epoch.load(std::memory_order_relaxed);
			const uint64_t meta = entry.epoch_and_count.load(std::memory_order_acquire);
			if (likely((meta >> 8) == current_epoch)) {
				const uint32_t count = static_cast<uint32_t>(meta & 0xFF);
				if (likely(count <= CallInlineCache::MAX_SLOTS)) {
					for (uint32_t slot_index = 0; slot_index < count; slot_index++) {
						if (likely(entry.slots[slot_index].script == p_target_script)) {
							return entry.slots[slot_index].function;
						}
					}
				}
			}
		}
		return _populate_call_cache(p_methodname_idx, p_target_script);
	}

#ifdef DEBUG_ENABLED
	CharString func_cname;
	const char *_func_cname = nullptr;

	Vector<String> operator_names;
	Vector<String> setter_names;
	Vector<String> getter_names;
	Vector<String> builtin_methods_names;
	Vector<String> constructors_names;
	Vector<String> utilities_names;
	Vector<String> gds_utilities_names;

	struct Profile {
		StringName signature;
		SafeNumeric<uint64_t> call_count;
		SafeNumeric<uint64_t> self_time;
		SafeNumeric<uint64_t> total_time;
		SafeNumeric<uint64_t> frame_call_count;
		SafeNumeric<uint64_t> frame_self_time;
		SafeNumeric<uint64_t> frame_total_time;
		uint64_t last_frame_call_count = 0;
		uint64_t last_frame_self_time = 0;
		uint64_t last_frame_total_time = 0;
		typedef struct NativeProfile {
			uint64_t call_count;
			uint64_t total_time;
			String signature;
		} NativeProfile;
		HashMap<String, NativeProfile> native_calls;
		HashMap<String, NativeProfile> last_native_calls;
	} profile;
#endif

	String _get_call_error(const String &p_where, const Variant **p_argptrs, int p_argcount, const Variant &p_ret, const Callable::CallError &p_err) const;
	String _get_callable_call_error(const String &p_where, const Callable &p_callable, const Variant **p_argptrs, int p_argcount, const Variant &p_ret, const Callable::CallError &p_err) const;
	Variant _get_default_variant_for_data_type(const GDScriptDataType &p_data_type);

public:
	static constexpr int MAX_CALL_DEPTH = 2048; // Limit to try to avoid crash because of a stack overflow.

	struct CallState {
		Signal completed;
		GDScript *script = nullptr;
		GDScriptInstance *instance = nullptr;
#ifdef DEBUG_ENABLED
		StringName function_name;
		String script_path;
#endif
		Vector<uint8_t> stack;
		int stack_size = 0;
		int ip = 0;
		int line = 0;
		int defarg = 0;
		Variant result;
	};

	_FORCE_INLINE_ StringName get_name() const { return name; }
	_FORCE_INLINE_ StringName get_source() const { return source; }
	_FORCE_INLINE_ GDScript *get_script() const { return _script; }
	_FORCE_INLINE_ bool is_static() const { return _static; }
	_FORCE_INLINE_ bool is_vararg() const { return _vararg_index >= 0; }
	_FORCE_INLINE_ MethodInfo get_method_info() const { return method_info; }
	_FORCE_INLINE_ int get_argument_count() const { return _argument_count; }
	_FORCE_INLINE_ Variant get_rpc_config() const { return rpc_config; }
	_FORCE_INLINE_ int get_max_stack_size() const { return _stack_size; }
#ifdef GDSCRIPT_BYTECODE_EXPORT_ENABLED
	_FORCE_INLINE_ const ExportFixups &get_export_fixups() const { return export_fixups; }
#endif
#ifdef TESTS_ENABLED
	struct DebugVMAllocaStats {
		uint32_t bytes_in_use = 0;
		uint32_t peak_bytes = 0;
		uint32_t heap_spill_count = 0;
	};
	static DebugVMAllocaStats debug_get_vm_alloca_stats();
	static void debug_reset_vm_alloca_stats();
	static void debug_set_vm_alloca_bytes_in_use_for_test(uint32_t p_bytes);
#endif

	Variant get_constant(int p_idx) const;
	StringName get_global_name(int p_idx) const;

	Variant call(GDScriptInstance *p_instance, const Variant **p_args, int p_argcount, Callable::CallError &r_err, CallState *p_state = nullptr);
	static void invalidate_call_caches() { _script_clear_epoch.fetch_add(1, std::memory_order_acq_rel); }
	void debug_get_stack_member_state(int p_line, List<Pair<StringName, int>> *r_stackvars) const;

#ifdef DEBUG_ENABLED
	void _profile_native_call(uint64_t p_t_taken, const String &p_function_name, const String &p_instance_class_name = String());
	void disassemble(const Vector<String> &p_code_lines) const;
#endif

	GDScriptFunction();
	~GDScriptFunction();
};

class GDScriptFunctionState : public RefCounted {
	GDCLASS(GDScriptFunctionState, RefCounted);
	friend class GDScriptFunction;
	GDScriptFunction *function = nullptr;
	GDScriptFunction::CallState state;
	Variant _signal_callback(const Variant **p_args, int p_argcount, Callable::CallError &r_error);

	SelfList<GDScriptFunctionState> scripts_list;
	SelfList<GDScriptFunctionState> instances_list;

protected:
	static void _bind_methods();

public:
	bool is_valid(bool p_extended_check = false) const;
	Variant resume(const Variant &p_arg = Variant());

#ifdef DEBUG_ENABLED
	// Returns a human-readable representation of the function.
	String get_readable_function() {
		return state.function_name;
	}
#endif

	void _clear_stack();
	void _clear_connections();

	GDScriptFunctionState();
	~GDScriptFunctionState();
};
