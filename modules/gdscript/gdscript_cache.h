/**************************************************************************/
/*  gdscript_cache.h                                                      */
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

#include "gdscript.h"

#include "core/object/ref_counted.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/condition_variable.h"
#include "core/os/safe_binary_mutex.h"
#include "core/os/thread.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"

#ifndef GDSCRIPT_NO_FRONTEND
class GDScriptAnalyzer;
class GDScriptParser;
#endif // !GDSCRIPT_NO_FRONTEND
#if defined(TOOLS_ENABLED) || defined(TESTS_ENABLED)
class GDScriptExportCompileSession;
#endif

#ifndef GDSCRIPT_NO_FRONTEND
class GDScriptParserRef : public RefCounted {
	GDSOFTCLASS(GDScriptParserRef, RefCounted);

public:
	enum Status {
		EMPTY,
		PARSED,
		INHERITANCE_SOLVED,
		INTERFACE_SOLVED,
		FULLY_SOLVED,
	};

private:
	GDScriptParser *parser = nullptr;
	GDScriptAnalyzer *analyzer = nullptr;
	Status status = EMPTY;
	Error result = OK;
	String path;
	uint32_t source_hash = 0;
	bool clearing = false;
	bool abandoned = false;
	bool detached = false;

	friend class GDScriptCache;
	friend class GDScript;
#if defined(TOOLS_ENABLED) || defined(TESTS_ENABLED)
	friend class GDScriptExportCompileSession;
#endif

public:
	Status get_status() const;
	String get_path() const;
	uint32_t get_source_hash() const;
	GDScriptParser *get_parser();
	GDScriptAnalyzer *get_analyzer();
	Error raise_status(Status p_new_status);
	void clear();

	GDScriptParserRef() {}
	~GDScriptParserRef();
};
#endif // !GDSCRIPT_NO_FRONTEND

namespace GDScriptTests {
class TestGDScriptCacheAccessor;
}

class GDScriptCache {
	// String key is full path.
#ifndef GDSCRIPT_NO_FRONTEND
	HashMap<String, GDScriptParserRef *> parser_map;
	HashMap<String, Vector<ObjectID>> abandoned_parser_map;
	HashMap<String, HashSet<String>> parser_inverse_dependencies;
#endif // !GDSCRIPT_NO_FRONTEND
	HashMap<String, Ref<GDScript>> shallow_gdscript_cache;
	HashMap<String, Ref<GDScript>> full_gdscript_cache;
	HashMap<String, Ref<GDScript>> static_gdscript_cache;
	HashMap<String, HashSet<String>> dependencies;

	struct InFlightBytecodeLoad {
		uint32_t ref_count = 1;
		uint64_t load_id = 0;
		String path;
		Ref<GDScript> script;
		Thread::ID owner_thread_id = 0;
		WorkerThreadPool::TaskID owner_wtp_task_id = WorkerThreadPool::INVALID_TASK_ID;
		String owner_resource_load_path;
		bool in_resource_transaction = false;
		bool has_static_data = false;
		bool annotated_static_unload = false;
		bool staged_under_direct_root = false;
		bool finalized = false;
		bool committed = false;
		Error result_error = OK;
		Ref<GDScript> result_script;
		Vector<InFlightBytecodeLoad *> direct_cycle_dependents;
		ConditionVariable cond_var;
	};

	HashMap<String, InFlightBytecodeLoad *> in_flight_bytecode_loads;
	uint64_t next_bytecode_load_id = 1;
	static thread_local Vector<InFlightBytecodeLoad *> active_direct_bytecode_loads;

	static bool _is_same_bytecode_load_chain(const InFlightBytecodeLoad *p_in_flight);
	static void _finalize_bytecode_load_locked(InFlightBytecodeLoad *p_in_flight, bool p_committed, Error p_error, bool p_publish_resource_cache, Vector<Ref<GDScript>> &r_scripts_to_static_init);
	static Error _run_bytecode_static_initializers_unlocked(const Vector<Ref<GDScript>> &p_scripts);
	static void _on_bytecode_transaction_finalize(bool p_committed, const String &p_path, uint64_t p_load_id);

	friend class GDScript;
#ifndef GDSCRIPT_NO_FRONTEND
	friend class GDScriptParserRef;
#endif // !GDSCRIPT_NO_FRONTEND
	friend class GDScriptInstance;
	friend class GDScriptTests::TestGDScriptCacheAccessor;

	static GDScriptCache *singleton;

	bool cleared = false;

public:
	static const int BINARY_MUTEX_TAG = 2;

private:
	static SafeBinaryMutex<BINARY_MUTEX_TAG> mutex;
	friend SafeBinaryMutex<BINARY_MUTEX_TAG> &_get_gdscript_cache_mutex();
#if defined(TOOLS_ENABLED) || defined(TESTS_ENABLED)
	static thread_local GDScriptExportCompileSession *active_export_session;
#endif

public:
#if defined(TOOLS_ENABLED) || defined(TESTS_ENABLED)
	class ActiveSessionGuard {
		GDScriptExportCompileSession *previous_session = nullptr;

	public:
		explicit ActiveSessionGuard(GDScriptExportCompileSession *p_session);
		~ActiveSessionGuard();
		ActiveSessionGuard(const ActiveSessionGuard &) = delete;
		ActiveSessionGuard &operator=(const ActiveSessionGuard &) = delete;
	};

	static bool has_active_export_session();
	static GDScriptExportCompileSession *get_active_export_session();
#endif

	static void move_script(const String &p_from, const String &p_to);
	static void remove_script(const String &p_path);
#ifndef GDSCRIPT_NO_FRONTEND
	static Ref<GDScriptParserRef> get_parser(const String &p_path, GDScriptParserRef::Status status, Error &r_error, const String &p_owner = String());
	static bool has_parser(const String &p_path);
	static void remove_parser(const String &p_path);
	static String get_source_code(const String &p_path);
	static Vector<uint8_t> get_binary_tokens(const String &p_path);
#endif // !GDSCRIPT_NO_FRONTEND
	static Vector<uint8_t> get_bytecode_buffer(const String &p_path, Error &r_error);
	static bool is_script_loading_on_current_chain(const String &p_path, const GDScript *p_script = nullptr);
	static Ref<GDScript> get_shallow_script(const String &p_path, Error &r_error, const String &p_owner = String());
	/**
	 * Returns a fully loaded GDScript using an already cached script if one exists.
	 *
	 * The returned instance is present in GDScriptCache and ResourceCache.
	 */
	static Ref<GDScript> get_full_script(const String &p_path, Error &r_error, const String &p_owner = String(), bool p_update_from_disk = false);
	static Ref<GDScript> get_cached_script(const String &p_path);
	static Error finish_compiling(const String &p_owner);
	static void add_static_script(Ref<GDScript> p_script);
	static void remove_static_script(const String &p_fqcn);

	static void clear();

	GDScriptCache();
	~GDScriptCache();
};
