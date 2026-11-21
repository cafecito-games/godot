/**************************************************************************/
/*  gdscript_cache.cpp                                                    */
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

#include "gdscript_cache.h"

#include "gdscript.h"
#include "gdscript_bytecode_format.h"
#include "gdscript_bytecode_loader.h"
#ifndef GDSCRIPT_NO_FRONTEND
#include "gdscript_analyzer.h"
#include "gdscript_compiler.h"
#if defined(TOOLS_ENABLED) || defined(TESTS_ENABLED)
#include "gdscript_export_compile_session.h"
#endif
#include "gdscript_parser.h"
#endif // !GDSCRIPT_NO_FRONTEND

#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "core/object/message_queue.h"
#include "core/templates/vector.h"

#ifndef GDSCRIPT_NO_FRONTEND
GDScriptParserRef::Status GDScriptParserRef::get_status() const {
	return status;
}

String GDScriptParserRef::get_path() const {
	return path;
}

uint32_t GDScriptParserRef::get_source_hash() const {
	return source_hash;
}

GDScriptParser *GDScriptParserRef::get_parser() {
	if (parser == nullptr) {
		parser = memnew(GDScriptParser);
	}
	return parser;
}

GDScriptAnalyzer *GDScriptParserRef::get_analyzer() {
	if (analyzer == nullptr) {
		analyzer = memnew(GDScriptAnalyzer(get_parser()));
	}
	return analyzer;
}

Error GDScriptParserRef::raise_status(Status p_new_status) {
	ERR_FAIL_COND_V(clearing, ERR_BUG);
	ERR_FAIL_COND_V(parser == nullptr && status != EMPTY, ERR_BUG);

	if (p_new_status < status) {
		return OK;
	}

	while (result == OK && p_new_status > status) {
		switch (status) {
			case EMPTY: {
				// Calling parse will clear the parser, which can destruct another GDScriptParserRef which can clear the last reference to the script with this path, calling remove_script, which clears this GDScriptParserRef.
				// It's ok if its the first thing done here.
				get_parser()->clear();
				status = PARSED;
				String remapped_path = ResourceLoader::path_remap(path);
				if (remapped_path.has_extension("gdb")) {
					status = EMPTY;
					result = ERR_UNAVAILABLE;
					return result;
				} else if (remapped_path.has_extension("gdc")) {
					Vector<uint8_t> tokens = GDScriptCache::get_binary_tokens(remapped_path);
					source_hash = hash_djb2_buffer(tokens.ptr(), tokens.size());
					result = get_parser()->parse_binary(tokens, path);
				} else {
					String source = GDScriptCache::get_source_code(remapped_path);
					source_hash = source.hash();
					result = get_parser()->parse(source, path, false);
				}
			} break;
			case PARSED: {
				status = INHERITANCE_SOLVED;
				result = get_analyzer()->resolve_inheritance();
			} break;
			case INHERITANCE_SOLVED: {
				status = INTERFACE_SOLVED;
				result = get_analyzer()->resolve_interface();
			} break;
			case INTERFACE_SOLVED: {
				status = FULLY_SOLVED;
				result = get_analyzer()->resolve_body();
			} break;
			case FULLY_SOLVED: {
				return result;
			}
		}
	}

	return result;
}

void GDScriptParserRef::clear() {
	if (clearing) {
		return;
	}
	clearing = true;

	GDScriptParser *lparser = parser;
	GDScriptAnalyzer *lanalyzer = analyzer;

	parser = nullptr;
	analyzer = nullptr;
	status = EMPTY;
	result = OK;
	source_hash = 0;

	clearing = false;

	if (lanalyzer != nullptr) {
		memdelete(lanalyzer);
	}

	if (lparser != nullptr) {
		memdelete(lparser);
	}
}

GDScriptParserRef::~GDScriptParserRef() {
	clear();

	if (!abandoned && !detached) {
		MutexLock lock(GDScriptCache::singleton->mutex);
		GDScriptCache::singleton->parser_map.erase(path);
	}
}
#endif // !GDSCRIPT_NO_FRONTEND

GDScriptCache *GDScriptCache::singleton = nullptr;
thread_local Vector<GDScriptCache::InFlightBytecodeLoad *> GDScriptCache::active_direct_bytecode_loads;
#if defined(TOOLS_ENABLED) || defined(TESTS_ENABLED)
thread_local GDScriptExportCompileSession *GDScriptCache::active_export_session = nullptr;

GDScriptCache::ActiveSessionGuard::ActiveSessionGuard(GDScriptExportCompileSession *p_session) :
		previous_session(GDScriptCache::active_export_session) {
	GDScriptCache::active_export_session = p_session;
}

GDScriptCache::ActiveSessionGuard::~ActiveSessionGuard() {
	GDScriptCache::active_export_session = previous_session;
}

bool GDScriptCache::has_active_export_session() {
	return active_export_session != nullptr;
}

GDScriptExportCompileSession *GDScriptCache::get_active_export_session() {
	return active_export_session;
}
#endif

SafeBinaryMutex<GDScriptCache::BINARY_MUTEX_TAG> &_get_gdscript_cache_mutex() {
	return GDScriptCache::mutex;
}

template <>
thread_local SafeBinaryMutex<GDScriptCache::BINARY_MUTEX_TAG>::TLSData SafeBinaryMutex<GDScriptCache::BINARY_MUTEX_TAG>::tls_data(_get_gdscript_cache_mutex());
SafeBinaryMutex<GDScriptCache::BINARY_MUTEX_TAG> GDScriptCache::mutex;

void GDScriptCache::move_script(const String &p_from, const String &p_to) {
#if defined(TOOLS_ENABLED) || defined(TESTS_ENABLED)
	if (active_export_session != nullptr) {
		return;
	}
#endif
	if (singleton == nullptr || p_from == p_to || p_from.is_empty()) {
		return;
	}

	MutexLock lock(singleton->mutex);

	if (singleton->cleared) {
		return;
	}

#ifndef GDSCRIPT_NO_FRONTEND
	remove_parser(p_from);
#endif // !GDSCRIPT_NO_FRONTEND

	if (singleton->shallow_gdscript_cache.has(p_from) && !p_from.is_empty()) {
		singleton->shallow_gdscript_cache[p_to] = singleton->shallow_gdscript_cache[p_from];
	}
	singleton->shallow_gdscript_cache.erase(p_from);

	if (singleton->full_gdscript_cache.has(p_from) && !p_from.is_empty()) {
		singleton->full_gdscript_cache[p_to] = singleton->full_gdscript_cache[p_from];
	}
	singleton->full_gdscript_cache.erase(p_from);
}

void GDScriptCache::remove_script(const String &p_path) {
#if defined(TOOLS_ENABLED) || defined(TESTS_ENABLED)
	if (active_export_session != nullptr) {
		return;
	}
#endif
	if (singleton == nullptr) {
		return;
	}

	MutexLock lock(singleton->mutex);

	if (singleton->cleared) {
		return;
	}

#ifndef GDSCRIPT_NO_FRONTEND
	if (HashMap<String, Vector<ObjectID>>::Iterator E = singleton->abandoned_parser_map.find(p_path)) {
		for (ObjectID parser_ref_id : E->value) {
			Ref<GDScriptParserRef> parser_ref = { ObjectDB::get_instance(parser_ref_id) };
			if (parser_ref.is_valid()) {
				parser_ref->clear();
			}
		}
	}

	singleton->abandoned_parser_map.erase(p_path);

	if (singleton->parser_map.has(p_path)) {
		singleton->parser_map[p_path]->clear();
	}

	remove_parser(p_path);
#endif // !GDSCRIPT_NO_FRONTEND

	singleton->dependencies.erase(p_path);
	singleton->shallow_gdscript_cache.erase(p_path);
	singleton->full_gdscript_cache.erase(p_path);
}

#ifndef GDSCRIPT_NO_FRONTEND
Ref<GDScriptParserRef> GDScriptCache::get_parser(const String &p_path, GDScriptParserRef::Status p_status, Error &r_error, const String &p_owner) {
#if defined(TOOLS_ENABLED) || defined(TESTS_ENABLED)
	if (active_export_session != nullptr) {
		return active_export_session->get_parser(p_path, p_status, r_error, p_owner);
	}
#endif
	MutexLock lock(singleton->mutex);
	Ref<GDScriptParserRef> ref;
	if (!p_owner.is_empty() && p_path != p_owner) {
		singleton->dependencies[p_owner].insert(p_path);
		singleton->parser_inverse_dependencies[p_path].insert(p_owner);
	}
	if (singleton->parser_map.has(p_path)) {
		ref = Ref<GDScriptParserRef>(singleton->parser_map[p_path]);
		if (ref.is_null()) {
			r_error = ERR_INVALID_DATA;
			return ref;
		}
	} else {
		String remapped_path = ResourceLoader::path_remap(p_path);
		if (!FileAccess::exists(remapped_path)) {
			r_error = ERR_FILE_NOT_FOUND;
			return ref;
		}
		ref.instantiate();
		ref->path = p_path;
		singleton->parser_map[p_path] = ref.ptr();
	}
	r_error = ref->raise_status(p_status);

	return ref;
}

bool GDScriptCache::has_parser(const String &p_path) {
#if defined(TOOLS_ENABLED) || defined(TESTS_ENABLED)
	if (active_export_session != nullptr) {
		return active_export_session->has_parser(p_path);
	}
#endif
	MutexLock lock(singleton->mutex);
	return singleton->parser_map.has(p_path);
}

void GDScriptCache::remove_parser(const String &p_path) {
#if defined(TOOLS_ENABLED) || defined(TESTS_ENABLED)
	if (active_export_session != nullptr) {
		active_export_session->remove_parser(p_path);
		return;
	}
#endif
	MutexLock lock(singleton->mutex);

	if (singleton->parser_map.has(p_path)) {
		GDScriptParserRef *parser_ref = singleton->parser_map[p_path];
		parser_ref->abandoned = true;
		singleton->abandoned_parser_map[p_path].push_back(parser_ref->get_instance_id());
	}

	// Can't clear the parser because some other parser might be currently using it in the chain of calls.
	singleton->parser_map.erase(p_path);

	// Have to copy while iterating, because parser_inverse_dependencies is modified.
	HashSet<String> ideps(singleton->parser_inverse_dependencies[p_path]);
	singleton->parser_inverse_dependencies.erase(p_path);
	for (String idep_path : ideps) {
		remove_parser(idep_path);
	}
}

String GDScriptCache::get_source_code(const String &p_path) {
	Vector<uint8_t> source_file;
	Error err;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);
	ERR_FAIL_COND_V(err, "");

	uint64_t len = f->get_length();
	source_file.resize(len + 1);
	uint64_t r = f->get_buffer(source_file.ptrw(), len);
	ERR_FAIL_COND_V(r != len, "");
	source_file.write[len] = 0;

	String source;
	if (source.append_utf8((const char *)source_file.ptr(), len) != OK) {
		ERR_FAIL_V_MSG("", "Script '" + p_path + "' contains invalid unicode (UTF-8), so it was not loaded. Please ensure that scripts are saved in valid UTF-8 unicode.");
	}
	return source;
}

Vector<uint8_t> GDScriptCache::get_binary_tokens(const String &p_path) {
	Vector<uint8_t> buffer;
	Error err = OK;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);
	ERR_FAIL_COND_V_MSG(err != OK, buffer, "Failed to open binary GDScript file '" + p_path + "'.");

	uint64_t len = f->get_length();
	buffer.resize(len);
	uint64_t read = f->get_buffer(buffer.ptrw(), buffer.size());
	ERR_FAIL_COND_V_MSG(read != len, Vector<uint8_t>(), "Failed to read binary GDScript file '" + p_path + "'.");

	return buffer;
}
#endif // !GDSCRIPT_NO_FRONTEND

Vector<uint8_t> GDScriptCache::get_bytecode_buffer(const String &p_path, Error &r_error) {
	Vector<uint8_t> buffer;
	r_error = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ, &r_error);
	if (r_error != OK || file.is_null()) {
		if (r_error == OK) {
			r_error = ERR_FILE_CANT_OPEN;
		}
		return buffer;
	}

	const uint64_t length = file->get_length();
	buffer.resize(static_cast<int>(length));
	if (length > 0) {
		const uint64_t read_bytes = file->get_buffer(buffer.ptrw(), length);
		if (read_bytes != length) {
			r_error = ERR_FILE_CANT_READ;
			return Vector<uint8_t>();
		}
	}

	return buffer;
}

bool GDScriptCache::_is_same_bytecode_load_chain(const InFlightBytecodeLoad *p_in_flight) {
	if (p_in_flight == nullptr) {
		return false;
	}

	if (!p_in_flight->owner_resource_load_path.is_empty() && ResourceLoader::is_path_ancestor_of_current_load(p_in_flight->owner_resource_load_path)) {
		return true;
	}

	const WorkerThreadPool::TaskID caller_task_id =
			WorkerThreadPool::get_singleton() != nullptr
			? WorkerThreadPool::get_singleton()->get_caller_task_id()
			: WorkerThreadPool::INVALID_TASK_ID;

	if (p_in_flight->owner_wtp_task_id != WorkerThreadPool::INVALID_TASK_ID) {
		return caller_task_id == p_in_flight->owner_wtp_task_id;
	}

	if (caller_task_id != WorkerThreadPool::INVALID_TASK_ID) {
		return false;
	}

	if (p_in_flight->owner_thread_id != Thread::get_caller_id()) {
		return false;
	}

	// If the in-flight load started inside ResourceLoader, do not treat main-thread pumped callbacks
	// (which temporarily clear ResourceLoader::load_nesting to 0) as part of the same load chain.
	if (!p_in_flight->owner_resource_load_path.is_empty() && !ResourceLoader::is_within_load()) {
		return false;
	}

	return true;
}

bool GDScriptCache::is_script_loading_on_current_chain(const String &p_path, const GDScript *p_script) {
	if (singleton == nullptr) {
		return false;
	}

	MutexLock lock(singleton->mutex);
	HashMap<String, InFlightBytecodeLoad *>::ConstIterator entry = singleton->in_flight_bytecode_loads.find(p_path);
	if (entry) {
		const InFlightBytecodeLoad *in_flight = entry->value;
		return (p_script == nullptr || in_flight->script.ptr() == p_script) && _is_same_bytecode_load_chain(in_flight);
	}

	if (p_script != nullptr && p_script->reloading) {
		if (ResourceLoader::is_path_ancestor_of_current_load(p_path)) {
			return true;
		}
		Ref<GDScript> cached = get_cached_script(p_path);
		if (cached.ptr() == p_script && cached->reloading) {
			return true;
		}
	}

	return false;
}

Error GDScriptCache::_run_bytecode_static_initializers_unlocked(const Vector<Ref<GDScript>> &p_scripts) {
	Error first_error = OK;
	for (int index = 0; index < p_scripts.size(); index++) {
		const Ref<GDScript> &script = p_scripts[index];
		if (script.is_null() || !script->is_valid()) {
			continue;
		}
		Error init_error = GDScriptBytecodeLoader::initialize_static_data(script);
		if (init_error != OK) {
			ERR_PRINT(vformat("Failed to run static initializer for GDScript bytecode \"%s\" (error %d).", script->get_script_path(), static_cast<int>(init_error)));
			if (first_error == OK) {
				first_error = init_error;
			}
		}
	}
	return first_error;
}

void GDScriptCache::_finalize_bytecode_load_locked(InFlightBytecodeLoad *p_in_flight, bool p_committed, Error p_error, bool p_publish_resource_cache, Vector<Ref<GDScript>> &r_scripts_to_static_init) {
	if (p_in_flight == nullptr || p_in_flight->finalized) {
		return;
	}

	const String script_path = p_in_flight->path;
	Ref<GDScript> script = p_in_flight->script;

	// Hold an extra reference while finalizing so the struct cannot be freed before notify_all().
	p_in_flight->ref_count++;

	Vector<InFlightBytecodeLoad *> direct_dependents;
	SWAP(direct_dependents, p_in_flight->direct_cycle_dependents);

	if (p_committed && p_error == OK && script.is_valid() && script->is_valid()) {
		script->reloading = false;
		singleton->full_gdscript_cache[script_path] = script;
		singleton->shallow_gdscript_cache.erase(script_path);

		if (p_in_flight->has_static_data && !p_in_flight->annotated_static_unload) {
			GDScriptCache::add_static_script(script);
		}

		if (p_publish_resource_cache) {
			script->set_path_cache(String());
			script->set_path(script_path, true);
		}

		if (ScriptServer::is_scripting_enabled() || script->is_tool()) {
			r_scripts_to_static_init.push_back(script);
		}

		p_in_flight->committed = true;
		p_in_flight->result_error = OK;
		p_in_flight->result_script = script;
	} else {
		if (script.is_valid()) {
			script->valid = false;
			script->reloading = false;
			script->clear();
		}

		HashMap<String, Ref<GDScript>>::Iterator shallow_entry = singleton->shallow_gdscript_cache.find(script_path);
		if (shallow_entry && shallow_entry->value == script) {
			singleton->shallow_gdscript_cache.erase(script_path);
		}

		HashMap<String, Ref<GDScript>>::Iterator full_entry = singleton->full_gdscript_cache.find(script_path);
		if (full_entry && full_entry->value == script) {
			singleton->full_gdscript_cache.erase(script_path);
		}

		p_in_flight->committed = false;
		p_in_flight->result_error = p_error != OK ? p_error : FAILED;
		p_in_flight->result_script = Ref<GDScript>();
	}

	p_in_flight->finalized = true;

	HashMap<String, InFlightBytecodeLoad *>::Iterator map_entry = singleton->in_flight_bytecode_loads.find(script_path);
	if (map_entry && map_entry->value == p_in_flight) {
		singleton->in_flight_bytecode_loads.erase(script_path);
		p_in_flight->ref_count--;
	}

	p_in_flight->cond_var.notify_all();

	for (int index = 0; index < direct_dependents.size(); index++) {
		InFlightBytecodeLoad *dependent = direct_dependents[index];
		_finalize_bytecode_load_locked(dependent, p_committed, p_committed ? OK : p_in_flight->result_error, p_publish_resource_cache, r_scripts_to_static_init);
		if (--dependent->ref_count == 0) {
			memdelete(dependent);
		}
	}

	if (--p_in_flight->ref_count == 0) {
		memdelete(p_in_flight);
	}
}

void GDScriptCache::_on_bytecode_transaction_finalize(bool p_committed, const String &p_path, uint64_t p_load_id) {
	if (singleton == nullptr) {
		return;
	}

	Vector<Ref<GDScript>> scripts_to_static_init;
	{
		MutexLock lock(singleton->mutex);
		if (singleton->cleared) {
			return;
		}

		HashMap<String, InFlightBytecodeLoad *>::Iterator entry = singleton->in_flight_bytecode_loads.find(p_path);
		if (!entry) {
			return;
		}

		InFlightBytecodeLoad *in_flight = entry->value;
		if (in_flight->load_id != p_load_id || in_flight->finalized) {
			return;
		}

		const Error final_error = p_committed ? in_flight->result_error : (in_flight->result_error != OK ? in_flight->result_error : FAILED);
		_finalize_bytecode_load_locked(in_flight, p_committed && final_error == OK, final_error, false, scripts_to_static_init);
	}

	if (!scripts_to_static_init.is_empty()) {
		_run_bytecode_static_initializers_unlocked(scripts_to_static_init);
	}
}

Ref<GDScript> GDScriptCache::get_shallow_script(const String &p_path, Error &r_error, const String &p_owner) {
#if defined(TOOLS_ENABLED) || defined(TESTS_ENABLED)
	if (active_export_session != nullptr) {
		return active_export_session->get_shallow_script(p_path, r_error, p_owner);
	}
#endif
	MutexLock lock(singleton->mutex);
	r_error = OK;

	if (!p_owner.is_empty() && p_path != p_owner) {
		singleton->dependencies[p_owner].insert(p_path);
	}
	if (singleton->full_gdscript_cache.has(p_path)) {
		return singleton->full_gdscript_cache[p_path];
	}
	if (singleton->shallow_gdscript_cache.has(p_path)) {
		return singleton->shallow_gdscript_cache[p_path];
	}

	const String remapped_path = ResourceLoader::path_remap(p_path);

	if (remapped_path.has_extension("gdb")) {
		const Vector<uint8_t> bytecode_buffer = get_bytecode_buffer(remapped_path, r_error);
		if (r_error != OK) {
			return Ref<GDScript>();
		}

		Ref<GDScript> script;
		script.instantiate();
		script->set_path_cache(p_path);

		String skeleton_error;
		r_error = GDScriptBytecodeLoader::load_skeleton(bytecode_buffer, script, p_path, nullptr, &skeleton_error);
		if (r_error != OK) {
			ERR_PRINT(vformat("Failed to load GDScript bytecode skeleton \"%s\": %s", p_path, skeleton_error));
			return Ref<GDScript>();
		}

		singleton->cleared = false;
		singleton->shallow_gdscript_cache[p_path] = script;
		return script;
	}

#ifdef GDSCRIPT_NO_FRONTEND
	r_error = ERR_UNAVAILABLE;
	ERR_FAIL_V_MSG(Ref<GDScript>(), vformat("Cannot load GDScript \"%s\" (resolved \"%s\"): this build was compiled with gdscript_frontend=no and only supports precompiled .gdb bytecode.", p_path, remapped_path));
#else
	Ref<GDScript> script;
	script.instantiate();

	script->set_path_cache(p_path);
	if (remapped_path.has_extension("gdc")) {
		Vector<uint8_t> buffer = get_binary_tokens(remapped_path);
		if (buffer.is_empty()) {
			r_error = ERR_FILE_CANT_READ;
		}
		script->set_binary_tokens_source(buffer);
	} else {
		r_error = script->load_source_code(remapped_path);
	}

	if (r_error) {
		return Ref<GDScript>(); // Returns null and does not cache when the script fails to load.
	}

	Ref<GDScriptParserRef> parser_ref = get_parser(p_path, GDScriptParserRef::PARSED, r_error);
	if (r_error == OK) {
		GDScriptCompiler::make_scripts(script.ptr(), parser_ref->get_parser()->get_tree(), true);
	}

	singleton->cleared = false;
	singleton->shallow_gdscript_cache[p_path] = script;

	return script;
#endif // GDSCRIPT_NO_FRONTEND
}

Ref<GDScript> GDScriptCache::get_full_script(const String &p_path, Error &r_error, const String &p_owner, bool p_update_from_disk) {
#if defined(TOOLS_ENABLED) || defined(TESTS_ENABLED)
	if (active_export_session != nullptr) {
		return active_export_session->get_full_script(p_path, r_error, p_owner);
	}
#endif
	r_error = OK;
	const String remapped_path = ResourceLoader::path_remap(p_path);

	// For uncached / ignore-cache loads of `.gdb`, load into a fresh detached GDScript without
	// holding GDScriptCache::mutex so a live cached script (and any active instances/callables
	// referencing its functions) is never mutated or freed in place.
	if (p_update_from_disk && remapped_path.has_extension("gdb")) {
		if (!p_owner.is_empty() && p_path != p_owner) {
			MutexLock dep_lock(singleton->mutex);
			singleton->dependencies[p_owner].insert(p_path);
		}

		const Vector<uint8_t> bytecode_buffer = get_bytecode_buffer(remapped_path, r_error);
		if (r_error != OK) {
			return Ref<GDScript>();
		}

		Ref<GDScript> detached_script;
		detached_script.instantiate();
		detached_script->set_path_cache(p_path);

		GDScriptBytecodeLoader::HeaderInfo header_info;
		String load_error_message;
		GDScriptBytecodeCacheResolver resolver(p_path);
		r_error = GDScriptBytecodeLoader::load_full(bytecode_buffer, detached_script, p_path, &resolver, &header_info, &load_error_message);
		if (r_error == OK && (ScriptServer::is_scripting_enabled() || detached_script->is_tool())) {
			Vector<Ref<GDScript>> init_list;
			init_list.push_back(detached_script);
			r_error = _run_bytecode_static_initializers_unlocked(init_list);
		}

		if (r_error != OK) {
			if (!load_error_message.is_empty()) {
				ERR_PRINT(vformat("Failed to load GDScript bytecode \"%s\": %s", p_path, load_error_message));
			}
			detached_script->valid = false;
			detached_script->clear();
			return Ref<GDScript>();
		}

		return detached_script;
	}

	MutexLock lock(singleton->mutex);

	if (!p_owner.is_empty() && p_path != p_owner) {
		singleton->dependencies[p_owner].insert(p_path);
	}

	Ref<GDScript> script;

	if (singleton->full_gdscript_cache.has(p_path)) {
		script = singleton->full_gdscript_cache[p_path];
		if (!p_update_from_disk) {
			return script;
		}
	}

	// Coordinate with any in-flight bytecode load for the same logical path.
	if (HashMap<String, InFlightBytecodeLoad *>::Iterator in_flight_entry = singleton->in_flight_bytecode_loads.find(p_path)) {
		InFlightBytecodeLoad *in_flight = in_flight_entry->value;
		if (_is_same_bytecode_load_chain(in_flight)) {
			// If we are loading directly (outside ResourceLoader) and referencing a true ancestor on the active
			// direct load stack, enroll our current direct load under that ancestor so cycle-dependent scripts
			// commit or roll back atomically with the cycle root.
			if (!in_flight->in_resource_transaction && active_direct_bytecode_loads.size() >= 2) {
				const int64_t ancestor_index = active_direct_bytecode_loads.find(in_flight);
				const int64_t current_index = active_direct_bytecode_loads.size() - 1;
				if (ancestor_index >= 0 && ancestor_index < current_index) {
					InFlightBytecodeLoad *current_direct = active_direct_bytecode_loads[current_index];
					if (current_direct != in_flight && !current_direct->staged_under_direct_root) {
						current_direct->staged_under_direct_root = true;
						current_direct->ref_count++;
						in_flight->direct_cycle_dependents.push_back(current_direct);
					}
				}
			}
			r_error = OK;
			return in_flight->script;
		}

		in_flight->ref_count++;
		while (!in_flight->finalized && !singleton->cleared) {
			if (Thread::is_main_thread()) {
				lock.temp_unlock();
				WorkerThreadPool::thread_delay_usec_unlocked(1000);
				if (MessageQueue::get_singleton() != nullptr) {
					MessageQueue::get_singleton()->flush();
				}
				lock.temp_relock();
			} else {
				in_flight->cond_var.wait(lock);
			}
		}

		const bool committed = in_flight->committed;
		const Error waited_error = in_flight->result_error;
		Ref<GDScript> waited_script = in_flight->result_script;
		if (--in_flight->ref_count == 0) {
			memdelete(in_flight);
		}

		if (committed && waited_script.is_valid()) {
			r_error = OK;
			return waited_script;
		}
		r_error = waited_error != OK ? waited_error : FAILED;
		return Ref<GDScript>();
	}

	if (remapped_path.has_extension("gdb")) {
		const Vector<uint8_t> bytecode_buffer = get_bytecode_buffer(remapped_path, r_error);
		if (r_error != OK) {
			return Ref<GDScript>();
		}

		if (script.is_null()) {
			HashMap<String, Ref<GDScript>>::Iterator shallow_entry = singleton->shallow_gdscript_cache.find(p_path);
			if (shallow_entry) {
				script = shallow_entry->value;
			} else {
				script.instantiate();
				script->set_path_cache(p_path);

				String skeleton_error;
				r_error = GDScriptBytecodeLoader::load_skeleton(bytecode_buffer, script, p_path, nullptr, &skeleton_error);
				if (r_error != OK) {
					ERR_PRINT(vformat("Failed to load GDScript bytecode skeleton \"%s\": %s", p_path, skeleton_error));
					return Ref<GDScript>();
				}

				singleton->shallow_gdscript_cache[p_path] = script;
			}
		}

		script->reloading = true;
		script->valid = false;

		InFlightBytecodeLoad *in_flight = memnew(InFlightBytecodeLoad);
		in_flight->ref_count = 1;
		in_flight->load_id = singleton->next_bytecode_load_id++;
		in_flight->path = p_path;
		in_flight->script = script;
		in_flight->owner_thread_id = Thread::get_caller_id();
		in_flight->owner_wtp_task_id =
				WorkerThreadPool::get_singleton() != nullptr
				? WorkerThreadPool::get_singleton()->get_caller_task_id()
				: WorkerThreadPool::INVALID_TASK_ID;
		singleton->cleared = false;
		singleton->in_flight_bytecode_loads[p_path] = in_flight;

		const bool registered_partial = ResourceLoader::register_current_load_partial_resource(script, p_path);
		if (registered_partial) {
			in_flight->owner_resource_load_path = ResourceLoader::get_current_load_path();
			in_flight->in_resource_transaction = ResourceLoader::add_current_load_transaction_callback(
					callable_mp_static(&GDScriptCache::_on_bytecode_transaction_finalize).bind(p_path, in_flight->load_id));
		}

		if (!in_flight->in_resource_transaction) {
			active_direct_bytecode_loads.push_back(in_flight);
		}

		GDScriptBytecodeLoader::HeaderInfo header_info;
		String load_error_message;
		GDScriptBytecodeCacheResolver resolver(p_path);
		in_flight->ref_count++;
		lock.temp_unlock();
		r_error = GDScriptBytecodeLoader::load_full(bytecode_buffer, script, p_path, &resolver, &header_info, &load_error_message);
		lock.temp_relock();
		if (--in_flight->ref_count == 0) {
			memdelete(in_flight);
			return Ref<GDScript>();
		}

		if (!in_flight->in_resource_transaction && !active_direct_bytecode_loads.is_empty() && active_direct_bytecode_loads[active_direct_bytecode_loads.size() - 1] == in_flight) {
			active_direct_bytecode_loads.resize(active_direct_bytecode_loads.size() - 1);
		}

		if (r_error != OK) {
			ERR_PRINT(vformat("Failed to load GDScript bytecode \"%s\": %s", p_path, load_error_message));
			in_flight->result_error = r_error;

			script->valid = false;
			script->reloading = false;
			script->clear();

			HashMap<String, Ref<GDScript>>::Iterator shallow_entry = singleton->shallow_gdscript_cache.find(p_path);
			if (shallow_entry && shallow_entry->value == script) {
				singleton->shallow_gdscript_cache.erase(p_path);
			}
			HashMap<String, Ref<GDScript>>::Iterator full_entry = singleton->full_gdscript_cache.find(p_path);
			if (full_entry && full_entry->value == script) {
				singleton->full_gdscript_cache.erase(p_path);
			}

			if (!in_flight->in_resource_transaction) {
				Vector<Ref<GDScript>> unused_init_list;
				_finalize_bytecode_load_locked(in_flight, false, r_error, false, unused_init_list);
			}
			return Ref<GDScript>();
		}

		in_flight->has_static_data = (header_info.script_flags & GDScriptBytecodeFormat::SCRIPT_FLAG_HAS_STATIC_DATA) != 0;
		in_flight->annotated_static_unload = (header_info.script_flags & GDScriptBytecodeFormat::SCRIPT_FLAG_ANNOTATED_STATIC_UNLOAD) != 0;

		if (in_flight->in_resource_transaction) {
			// Defer ResourceCache publication, full_gdscript_cache promotion, static_init, and waiter notification
			// to ResourceLoader's atomic CycleTransaction commit/rollback callback.
			return script;
		}

		if (in_flight->staged_under_direct_root) {
			// Direct A <-> B cycle outside ResourceLoader: commit atomically when the cycle root finishes.
			return script;
		}

		Vector<Ref<GDScript>> scripts_to_static_init;
		_finalize_bytecode_load_locked(in_flight, true, OK, true, scripts_to_static_init);
		if (!scripts_to_static_init.is_empty()) {
			lock.temp_unlock();
			r_error = _run_bytecode_static_initializers_unlocked(scripts_to_static_init);
			lock.temp_relock();
		}
		return script;
	}

#ifdef GDSCRIPT_NO_FRONTEND
	r_error = ERR_UNAVAILABLE;
	ERR_FAIL_V_MSG(Ref<GDScript>(), vformat("Cannot load GDScript \"%s\" (resolved \"%s\"): this build was compiled with gdscript_frontend=no and only supports precompiled .gdb bytecode.", p_path, remapped_path));
#else
	if (script.is_null()) {
		script = get_shallow_script(p_path, r_error);
		// Only exit early if script failed to load, otherwise let reload report errors.
		if (script.is_null()) {
			return script;
		}
	}

	if (script->reloading) {
		return script;
	}

	if (p_update_from_disk) {
		if (remapped_path.has_extension("gdc")) {
			Vector<uint8_t> buffer = get_binary_tokens(remapped_path);
			if (buffer.is_empty()) {
				r_error = ERR_FILE_CANT_READ;
				goto finish;
			}
			script->set_binary_tokens_source(buffer);
		} else {
			r_error = script->load_source_code(remapped_path);
			if (r_error) {
				goto finish;
			}
		}
	}

	// Allowing lifting the lock might cause a script to be reloaded multiple times,
	// which, as a last resort deadlock prevention strategy, is a good tradeoff.
	{
		uint32_t allowance_id = WorkerThreadPool::thread_enter_unlock_allowance_zone(singleton->mutex);
		r_error = script->reload(true);
		WorkerThreadPool::thread_exit_unlock_allowance_zone(allowance_id);
	}

finish:
	singleton->full_gdscript_cache[p_path] = script;
	singleton->shallow_gdscript_cache.erase(p_path);

	// Add the script to the resource cache. Usually ResourceLoader would take care of it, but cyclic references can break that sometimes so we do it ourselves.
	// Resources don't know whether they are cached, so using `set_path()` after `set_path_cache()` does not add the resource to the cache if the path is the same.
	// We reset the cached path from `get_shallow_script()` so that the subsequent call to `set_path()` caches everything correctly.
	script->set_path_cache(String());
	script->set_path(p_path, true);

	return script;
#endif // GDSCRIPT_NO_FRONTEND
}

Ref<GDScript> GDScriptCache::get_cached_script(const String &p_path) {
#if defined(TOOLS_ENABLED) || defined(TESTS_ENABLED)
	if (active_export_session != nullptr) {
		return active_export_session->get_cached_script(p_path);
	}
#endif
	MutexLock lock(singleton->mutex);

	if (singleton->full_gdscript_cache.has(p_path)) {
		return singleton->full_gdscript_cache[p_path];
	}

	if (singleton->shallow_gdscript_cache.has(p_path)) {
		return singleton->shallow_gdscript_cache[p_path];
	}

	return Ref<GDScript>();
}

Error GDScriptCache::finish_compiling(const String &p_owner) {
#if defined(TOOLS_ENABLED) || defined(TESTS_ENABLED)
	if (active_export_session != nullptr) {
		return active_export_session->finish_compiling(p_owner);
	}
#endif
	MutexLock lock(singleton->mutex);

	// Mark this as compiled.
	Ref<GDScript> script = get_cached_script(p_owner);
	singleton->full_gdscript_cache[p_owner] = script;
	singleton->shallow_gdscript_cache.erase(p_owner);

	HashSet<String> depends(singleton->dependencies[p_owner]);

	Error err = OK;
	for (const String &E : depends) {
		Error this_err = OK;
		// No need to save the script. We assume it's already referenced in the owner.
		get_full_script(E, this_err);

		if (this_err != OK) {
			err = this_err;
		}
	}

	singleton->dependencies.erase(p_owner);

	return err;
}

void GDScriptCache::add_static_script(Ref<GDScript> p_script) {
#if defined(TOOLS_ENABLED) || defined(TESTS_ENABLED)
	if (active_export_session != nullptr) {
		return;
	}
#endif
	ERR_FAIL_COND_MSG(p_script.is_null(), "Trying to cache empty script as static.");
	ERR_FAIL_COND_MSG(!p_script->is_valid(), "Trying to cache non-compiled script as static.");
	singleton->cleared = false;
	singleton->static_gdscript_cache[p_script->get_fully_qualified_name()] = p_script;
}

void GDScriptCache::remove_static_script(const String &p_fqcn) {
#if defined(TOOLS_ENABLED) || defined(TESTS_ENABLED)
	if (active_export_session != nullptr) {
		return;
	}
#endif
	singleton->static_gdscript_cache.erase(p_fqcn);
}

void GDScriptCache::clear() {
	if (singleton == nullptr) {
		return;
	}

	MutexLock lock(singleton->mutex);

	if (singleton->cleared) {
		return;
	}
	singleton->cleared = true;

#ifndef GDSCRIPT_NO_FRONTEND
	singleton->parser_inverse_dependencies.clear();

	for (const KeyValue<String, Vector<ObjectID>> &KV : singleton->abandoned_parser_map) {
		for (ObjectID parser_ref_id : KV.value) {
			Ref<GDScriptParserRef> parser_ref = { ObjectDB::get_instance(parser_ref_id) };
			if (parser_ref.is_valid()) {
				parser_ref->clear();
			}
		}
	}

	singleton->abandoned_parser_map.clear();

	RBSet<Ref<GDScriptParserRef>> parser_map_refs;
	for (KeyValue<String, GDScriptParserRef *> &E : singleton->parser_map) {
		parser_map_refs.insert(E.value);
	}

	singleton->parser_map.clear();

	for (Ref<GDScriptParserRef> &E : parser_map_refs) {
		if (E.is_valid()) {
			E->clear();
		}
	}

	parser_map_refs.clear();
#endif // !GDSCRIPT_NO_FRONTEND

	Vector<InFlightBytecodeLoad *> remaining_in_flight;
	remaining_in_flight.reserve(singleton->in_flight_bytecode_loads.size());
	for (const KeyValue<String, InFlightBytecodeLoad *> &entry : singleton->in_flight_bytecode_loads) {
		remaining_in_flight.push_back(entry.value);
	}
	singleton->in_flight_bytecode_loads.clear();
	for (int index = 0; index < remaining_in_flight.size(); index++) {
		InFlightBytecodeLoad *in_flight = remaining_in_flight[index];
		in_flight->committed = false;
		in_flight->result_error = FAILED;
		in_flight->result_script = Ref<GDScript>();
		in_flight->script = Ref<GDScript>();
		for (int dep_index = 0; dep_index < in_flight->direct_cycle_dependents.size(); dep_index++) {
			InFlightBytecodeLoad *dependent = in_flight->direct_cycle_dependents[dep_index];
			if (--dependent->ref_count == 0) {
				memdelete(dependent);
			}
		}
		in_flight->direct_cycle_dependents.clear();
		in_flight->finalized = true;
		in_flight->cond_var.notify_all();
		if (--in_flight->ref_count == 0) {
			memdelete(in_flight);
		}
	}

	singleton->shallow_gdscript_cache.clear();
	singleton->full_gdscript_cache.clear();
	singleton->static_gdscript_cache.clear();
}

void GDScriptCache::reset_cleared() {
	if (singleton == nullptr) {
		return;
	}
	MutexLock lock(singleton->mutex);
	singleton->cleared = false;
}

GDScriptCache::GDScriptCache() {
	singleton = this;
}

GDScriptCache::~GDScriptCache() {
	if (!cleared) {
		clear();
	}
	singleton = nullptr;
}
