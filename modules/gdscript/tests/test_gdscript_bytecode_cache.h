/**************************************************************************/
/*  test_gdscript_bytecode_cache.h                                        */
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
#include "../gdscript_bytecode_export.h"
#include "../gdscript_bytecode_format.h"
#include "../gdscript_bytecode_loader.h"
#include "../gdscript_cache.h"
#include "../gdscript_export_compile_session.h"
#include "gdscript_test_runner_suite.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/templates/hash_map.h"
#include "core/templates/safe_refcount.h"
#include "core/templates/vector.h"
#include "scene/main/node.h"
#include "scene/resources/packed_scene.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

#ifdef GDSCRIPT_BYTECODE_EXPORT_ENABLED

namespace TestGDScriptBytecodeCache {

constexpr uint64_t WAIT_TIMEOUT_USEC = 15 * 1000 * 1000;

static void ensure_gdscript_language_initialized() {
	if (!GDScriptLanguage::get_singleton()->get_global_map().has(SNAME("Node"))) {
		GDScriptLanguage::get_singleton()->init();
	}
}

static bool wait_for_flag(const SafeFlag &p_flag, uint64_t p_timeout_usec = WAIT_TIMEOUT_USEC) {
	const uint64_t deadline = OS::get_singleton()->get_ticks_usec() + p_timeout_usec;
	while (!p_flag.is_set()) {
		if (OS::get_singleton()->get_ticks_usec() >= deadline) {
			return false;
		}
		OS::get_singleton()->delay_usec(1000);
	}
	return true;
}

static void write_text_file(const String &p_path, const String &p_content) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	REQUIRE(file.is_valid());
	file->store_string(p_content);
	file->close();
}

static void write_bytes_file(const String &p_path, const Vector<uint8_t> &p_bytes) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	REQUIRE(file.is_valid());
	if (!p_bytes.is_empty()) {
		file->store_buffer(p_bytes.ptr(), p_bytes.size());
	}
	file->close();
}

static void write_remap_file(const String &p_logical_path, const String &p_target_path) {
	write_text_file(p_logical_path + ".remap", vformat("[remap]\n\npath=\"%s\"\n", p_target_path));
}

static void remove_file_if_exists(const String &p_path) {
	if (FileAccess::exists(p_path)) {
		DirAccess::remove_absolute(p_path);
	}
}

static String make_temp_suite_dir(const String &p_name) {
	const String base_dir = TestUtils::get_temp_path(p_name);
	Ref<DirAccess> directory = DirAccess::create_for_path(base_dir.get_base_dir());
	REQUIRE(directory.is_valid());
	directory->make_dir_recursive(base_dir);
	return base_dir;
}

static void export_scripts_to_bytecode_and_delete_sources(const Vector<String> &p_script_paths, bool p_write_remaps = true) {
	GDScriptExportCompileSession session(false);
	Vector<Vector<uint8_t>> serialized_buffers;
	serialized_buffers.resize(p_script_paths.size());

	for (int index = 0; index < p_script_paths.size(); index++) {
		Error compile_error = OK;
		Ref<GDScript> compiled_script = session.get_or_compile(p_script_paths[index], compile_error);
		INFO("Export compile error: " << session.get_last_error_message().utf8().get_data());
		REQUIRE(compile_error == OK);
		REQUIRE(compiled_script.is_valid());

		GDScriptBytecodeExporter::Options export_options;
		export_options.annotated_static_unload = session.has_annotated_static_unload(p_script_paths[index]);
		export_options.release_profile = false;
		export_options.emit_line_opcodes = true;

		String export_error;
		Error serialize_error = GDScriptBytecodeExporter::serialize(compiled_script, serialized_buffers.write[index], export_options, &export_error);
		INFO("Bytecode export error: " << export_error.utf8().get_data());
		REQUIRE(serialize_error == OK);
		REQUIRE(!serialized_buffers[index].is_empty());
	}

	session.clear();

	for (int index = 0; index < p_script_paths.size(); index++) {
		const String &script_path = p_script_paths[index];
		const String gdb_path = script_path.get_basename() + ".gdb";
		write_bytes_file(gdb_path, serialized_buffers[index]);
		if (p_write_remaps) {
			write_remap_file(script_path, gdb_path);
		}
		remove_file_if_exists(script_path);
		GDScriptCache::remove_script(script_path);
		GDScriptCache::remove_script(gdb_path);
	}
}

static void cleanup_cached_scripts(const Vector<String> &p_paths) {
	for (int index = 0; index < p_paths.size(); index++) {
		Ref<GDScript> cached = GDScriptCache::get_cached_script(p_paths[index]);
		if (cached.is_valid()) {
			const String fqcn = cached->get_fully_qualified_name();
			cached->clear();
			GDScriptCache::remove_static_script(fqcn);
		}
		GDScriptCache::remove_script(p_paths[index]);
	}
}

static bool wait_for_resource_cache_eviction(const String &p_path, uint64_t p_timeout_usec = 2 * 1000 * 1000) {
	const uint64_t deadline = OS::get_singleton()->get_ticks_usec() + p_timeout_usec;
	while (ResourceCache::has(p_path)) {
		if (OS::get_singleton()->get_ticks_usec() >= deadline) {
			return false;
		}
		OS::get_singleton()->delay_usec(1000);
	}
	return true;
}

static Ref<Resource> load_via_threaded_request(const String &p_path, bool p_use_sub_threads, Error *r_error = nullptr) {
	Error request_error = ResourceLoader::load_threaded_request(p_path, "", p_use_sub_threads);
	if (request_error != OK) {
		if (r_error != nullptr) {
			*r_error = request_error;
		}
		return Ref<Resource>();
	}

	const uint64_t deadline = OS::get_singleton()->get_ticks_usec() + WAIT_TIMEOUT_USEC;
	ResourceLoader::ThreadLoadStatus status = ResourceLoader::load_threaded_get_status(p_path);
	while (status == ResourceLoader::THREAD_LOAD_IN_PROGRESS) {
		if (OS::get_singleton()->get_ticks_usec() >= deadline) {
			if (r_error != nullptr) {
				*r_error = ERR_TIMEOUT;
			}
			return Ref<Resource>();
		}
		OS::get_singleton()->delay_usec(1000);
		status = ResourceLoader::load_threaded_get_status(p_path);
	}

	return ResourceLoader::load_threaded_get(p_path, r_error);
}

#ifdef THREADS_ENABLED
class BytecodeSyncGateResource : public Resource {
	GDSOFTCLASS(BytecodeSyncGateResource, Resource);
};

class BytecodeSyncGateLoader : public ResourceFormatLoader {
	GDSOFTCLASS(BytecodeSyncGateLoader, ResourceFormatLoader);

public:
	static constexpr const char *EXTENSION = "gdbsyncgate";

	SafeFlag gate_enabled;
	SafeFlag gate_entered;
	SafeFlag allow_finish;
	SafeFlag fail_gate;
	SafeFlag saw_uncommitted_caches_empty;

	Mutex paths_mutex;
	Vector<String> cache_check_paths;

	void reset(bool p_enable_gate, bool p_should_fail, const Vector<String> &p_cache_check_paths) {
		if (p_enable_gate) {
			gate_enabled.set();
		} else {
			gate_enabled.clear();
		}
		gate_entered.clear();
		allow_finish.clear();
		if (p_should_fail) {
			fail_gate.set();
		} else {
			fail_gate.clear();
		}
		saw_uncommitted_caches_empty.clear();

		MutexLock lock(paths_mutex);
		cache_check_paths = p_cache_check_paths;
	}

	virtual Ref<Resource> load(const String &p_path, const String &p_original_path = "", Error *r_error = nullptr, bool p_use_sub_threads = false, float *r_progress = nullptr, CacheMode p_cache_mode = CACHE_MODE_REUSE) override {
		if (r_error != nullptr) {
			*r_error = OK;
		}

		if (gate_enabled.is_set()) {
			bool all_absent = true;
			{
				MutexLock lock(paths_mutex);
				for (int index = 0; index < cache_check_paths.size(); index++) {
					if (ResourceCache::has(cache_check_paths[index])) {
						all_absent = false;
						break;
					}
				}
			}
			if (all_absent) {
				saw_uncommitted_caches_empty.set();
			}

			gate_entered.set();

			const uint64_t deadline = OS::get_singleton()->get_ticks_usec() + WAIT_TIMEOUT_USEC;
			while (!allow_finish.is_set()) {
				if (OS::get_singleton()->get_ticks_usec() >= deadline) {
					if (r_error != nullptr) {
						*r_error = ERR_TIMEOUT;
					}
					return Ref<Resource>();
				}
				WorkerThreadPool::thread_delay_usec_unlocked(1000);
			}

			if (fail_gate.is_set()) {
				if (r_error != nullptr) {
					*r_error = ERR_PARSE_ERROR;
				}
				return Ref<Resource>();
			}
		}

		Ref<BytecodeSyncGateResource> resource;
		resource.instantiate();
		return resource;
	}

	virtual bool exists(const String &p_path) const override {
		return FileAccess::exists(p_path);
	}

	virtual void get_recognized_extensions(List<String> *p_extensions) const override {
		p_extensions->push_back(EXTENSION);
	}

	virtual bool recognize_path(const String &p_path, const String &p_for_type = String()) const override {
		return p_path.get_extension().to_lower() == EXTENSION;
	}

	virtual bool handles_type(const String &p_type) const override {
		return p_type.is_empty() || p_type == "Resource";
	}

	virtual String get_resource_type(const String &p_path) const override {
		return recognize_path(p_path) ? "Resource" : String();
	}
};

struct GateLoaderGuard {
	Ref<BytecodeSyncGateLoader> loader;

	GateLoaderGuard() {
		loader.instantiate();
		ResourceLoader::add_resource_format_loader(loader);
	}

	~GateLoaderGuard() {
		ResourceLoader::remove_resource_format_loader(loader);
	}
};

struct ConcurrentResourceLoadTask {
	String path;
	bool use_gdscript_cache_direct = false;
	SafeFlag started;
	SafeFlag finished;
	Error error = OK;
	Ref<Resource> resource;
	Thread thread;

	static void thread_entry(void *p_userdata) {
		ConcurrentResourceLoadTask *task = static_cast<ConcurrentResourceLoadTask *>(p_userdata);
		task->started.set();
		if (task->use_gdscript_cache_direct) {
			task->resource = GDScriptCache::get_full_script(task->path, task->error);
		} else {
			task->resource = ResourceLoader::load(task->path, "", ResourceFormatLoader::CACHE_MODE_REUSE, &task->error);
		}
		task->finished.set();
	}

	void start(const String &p_path, bool p_direct_cache = false) {
		path = p_path;
		use_gdscript_cache_direct = p_direct_cache;
		started.clear();
		finished.clear();
		error = OK;
		resource = Ref<Resource>();
		thread.start(&ConcurrentResourceLoadTask::thread_entry, this);
	}

	void join() {
		if (thread.is_started()) {
			thread.wait_to_finish();
		}
	}
};
#endif // THREADS_ENABLED

TEST_CASE("[Modules][GDScript][BytecodeCache] Direct and remapped .gdb loading, dependencies, and classes_used") {
	ensure_gdscript_language_initialized();

	const String base_dir = make_temp_suite_dir("gdb_cache_basic");
	const String dep_script_path = base_dir.path_join("dep.gd");
	const String main_script_path = base_dir.path_join("main.gd");
	const String main_gdb_path = base_dir.path_join("main.gdb");

	write_text_file(
			dep_script_path,
			"extends RefCounted\nconst FACTOR := 7\nfunc scale(v: int) -> int:\n\treturn v * FACTOR\n");
	write_text_file(
			main_script_path,
			vformat("extends RefCounted\nconst DEP := preload(\"%s\")\nfunc compute(x: int) -> int:\n\treturn DEP.new().scale(x) + 1\n", dep_script_path));

	Vector<String> scripts;
	scripts.push_back(dep_script_path);
	scripts.push_back(main_script_path);
	export_scripts_to_bytecode_and_delete_sources(scripts, true);

	CHECK(ResourceLoader::get_resource_type(main_gdb_path) == "GDScript");

	List<String> direct_dependencies;
	ResourceLoader::get_dependencies(main_gdb_path, &direct_dependencies, false);
	CHECK(direct_dependencies.find(dep_script_path) != nullptr);

	List<String> remapped_dependencies;
	ResourceLoader::get_dependencies(main_script_path, &remapped_dependencies, false);
	CHECK(remapped_dependencies.find(dep_script_path) != nullptr);

	HashSet<StringName> classes_used;
	ResourceLoader::get_classes_used(main_gdb_path, &classes_used);
	ResourceLoader::get_classes_used(main_script_path, &classes_used);
	CHECK(classes_used.is_empty());

	Error shallow_error = OK;
	Ref<GDScript> shallow = GDScriptCache::get_shallow_script(main_script_path, shallow_error);
	REQUIRE(shallow_error == OK);
	REQUIRE(shallow.is_valid());
	CHECK(shallow->is_bytecode_backed());
	CHECK(GDScriptTests::TestGDScriptCacheAccessor::has_shallow(main_script_path));
	CHECK(!GDScriptTests::TestGDScriptCacheAccessor::has_full(main_script_path));
	CHECK(!ResourceCache::has(main_script_path));

	Error load_error = OK;
	Ref<GDScript> loaded = ResourceLoader::load(main_script_path, "", ResourceFormatLoader::CACHE_MODE_REUSE, &load_error);
	REQUIRE(load_error == OK);
	REQUIRE(loaded.is_valid());
	CHECK(loaded.ptr() == shallow.ptr());
	CHECK(loaded->is_valid());
	CHECK(loaded->is_bytecode_backed());
	CHECK(loaded->get_source_code().is_empty());
	CHECK(ResourceCache::has(main_script_path));
	CHECK(GDScriptTests::TestGDScriptCacheAccessor::has_full(main_script_path));
	CHECK(!GDScriptTests::TestGDScriptCacheAccessor::has_shallow(main_script_path));

	Ref<RefCounted> instance = memnew(RefCounted);
	instance->set_script(loaded);
	CHECK(int(instance->call("compute", 6)) == 43);

	instance->set_script(Variant());
	instance.unref();
	loaded.unref();
	shallow.unref();
	cleanup_cached_scripts(scripts);
	CHECK(!ResourceCache::has(main_script_path));
	CHECK(!ResourceCache::has(dep_script_path));
}

TEST_CASE("[Modules][GDScript][BytecodeCache] GDScript::reload() guard on bytecode-backed script") {
	ensure_gdscript_language_initialized();

	const String base_dir = make_temp_suite_dir("gdb_cache_reload_guard");
	const String script_path = base_dir.path_join("guarded.gd");

	write_text_file(
			script_path,
			"extends RefCounted\nvar offset: int = 5\nfunc answer(x: int) -> int:\n\treturn x + offset\n");

	Vector<String> scripts;
	scripts.push_back(script_path);
	export_scripts_to_bytecode_and_delete_sources(scripts, true);

	Error load_error = OK;
	Ref<GDScript> script = ResourceLoader::load(script_path, "", ResourceFormatLoader::CACHE_MODE_REUSE, &load_error);
	REQUIRE(load_error == OK);
	REQUIRE(script.is_valid());
	CHECK(script->is_bytecode_backed());
	CHECK(script->is_valid());

	// Calling reload() on a valid bytecode-backed script must not parse empty source.
	CHECK(script->reload(true) == OK);
	CHECK(script->reload(false) == OK);
	CHECK(script->is_valid());

	Ref<RefCounted> instance = memnew(RefCounted);
	instance->set_script(script);
	CHECK(int(instance->call("answer", 37)) == 42);

	ERR_PRINT_OFF;
	CHECK(script->reload(false) == ERR_ALREADY_IN_USE);
	ERR_PRINT_ON;

	CHECK(script->reload(true) == OK);
	CHECK(script->is_valid());
	CHECK(int(instance->call("answer", 10)) == 15);

	instance->set_script(Variant());
	instance.unref();
	script.unref();
	cleanup_cached_scripts(scripts);
	CHECK(!ResourceCache::has(script_path));
}

TEST_CASE("[Modules][GDScript][BytecodeCache] CACHE_MODE_IGNORE loads detached copy without mutating live cached script") {
	ensure_gdscript_language_initialized();

	const String base_dir = make_temp_suite_dir("gdb_cache_mode_ignore");
	const String script_path = base_dir.path_join("detached_target.gd");
	const String gdb_path = base_dir.path_join("detached_target.gdb");

	write_text_file(
			script_path,
			"extends RefCounted\nstatic var boot_value: int = 100\nstatic func _static_init() -> void:\n\tboot_value += 23\nvar offset: int = 7\nfunc compute(x: int) -> int:\n\treturn x + offset + boot_value\n");

	Vector<String> scripts;
	scripts.push_back(script_path);
	export_scripts_to_bytecode_and_delete_sources(scripts, true);

	Error initial_error = OK;
	Ref<GDScript> cached_script = ResourceLoader::load(script_path, "", ResourceFormatLoader::CACHE_MODE_REUSE, &initial_error);
	REQUIRE(initial_error == OK);
	REQUIRE(cached_script.is_valid());
	CHECK(cached_script->is_valid());
	CHECK(cached_script->is_bytecode_backed());

	Ref<RefCounted> live_instance = memnew(RefCounted);
	live_instance->set_script(cached_script);
	CHECK(int(live_instance->call("compute", 10)) == 140);

	// CACHE_MODE_IGNORE must return a fresh detached script without mutating or clearing the live cached script.
	Error ignore_error = OK;
	Ref<GDScript> detached_script = ResourceLoader::load(script_path, "", ResourceFormatLoader::CACHE_MODE_IGNORE, &ignore_error);
	REQUIRE(ignore_error == OK);
	REQUIRE(detached_script.is_valid());
	CHECK(detached_script.ptr() != cached_script.ptr());
	CHECK(detached_script->is_valid());
	CHECK(detached_script->is_bytecode_backed());

	CHECK(cached_script->is_valid());
	CHECK(ResourceCache::get_ref(script_path).ptr() == cached_script.ptr());
	CHECK(GDScriptCache::get_cached_script(script_path).ptr() == cached_script.ptr());
	CHECK(int(live_instance->call("compute", 10)) == 140);

	Ref<RefCounted> detached_instance = memnew(RefCounted);
	detached_instance->set_script(detached_script);
	CHECK(int(detached_instance->call("compute", 20)) == 150);

	// Even when the on-disk .gdb becomes corrupt and CACHE_MODE_IGNORE fails, the live cached script must remain intact.
	write_text_file(gdb_path, "corrupt-gdb-payload");
	ERR_PRINT_OFF;
	Error corrupt_error = OK;
	Ref<Resource> failed_detached = ResourceLoader::load(script_path, "", ResourceFormatLoader::CACHE_MODE_IGNORE, &corrupt_error);
	ERR_PRINT_ON;
	CHECK(corrupt_error != OK);
	CHECK(failed_detached.is_null());

	CHECK(cached_script->is_valid());
	CHECK(ResourceCache::get_ref(script_path).ptr() == cached_script.ptr());
	CHECK(GDScriptCache::get_cached_script(script_path).ptr() == cached_script.ptr());
	CHECK(int(live_instance->call("compute", 5)) == 135);

	detached_instance->set_script(Variant());
	detached_instance.unref();
	detached_script.unref();
	live_instance->set_script(Variant());
	live_instance.unref();
	cached_script.unref();
	cleanup_cached_scripts(scripts);
	CHECK(!ResourceCache::has(script_path));
}

TEST_CASE("[Modules][GDScript][BytecodeCache] @static_unload preservation in static_gdscript_cache") {
	ensure_gdscript_language_initialized();

	const String base_dir = make_temp_suite_dir("gdb_cache_static_unload");
	const String retained_path = base_dir.path_join("static_retained.gd");
	const String unloadable_path = base_dir.path_join("static_unloadable.gd");

	write_text_file(
			retained_path,
			"extends RefCounted\nstatic var count: int = 11\nstatic func get_count() -> int:\n\treturn count\n");
	write_text_file(
			unloadable_path,
			"@static_unload\nextends RefCounted\nstatic var count: int = 22\nstatic func get_count() -> int:\n\treturn count\n");

	Vector<String> scripts;
	scripts.push_back(retained_path);
	scripts.push_back(unloadable_path);
	export_scripts_to_bytecode_and_delete_sources(scripts, true);

	Error retained_error = OK;
	Ref<GDScript> retained_script = ResourceLoader::load(retained_path, "", ResourceFormatLoader::CACHE_MODE_REUSE, &retained_error);
	REQUIRE(retained_error == OK);
	REQUIRE(retained_script.is_valid());
	CHECK(int(retained_script->call("get_count")) == 11);
	CHECK(GDScriptTests::TestGDScriptCacheAccessor::has_static(retained_script->get_fully_qualified_name()));

	Error unloadable_error = OK;
	Ref<GDScript> unloadable_script = ResourceLoader::load(unloadable_path, "", ResourceFormatLoader::CACHE_MODE_REUSE, &unloadable_error);
	REQUIRE(unloadable_error == OK);
	REQUIRE(unloadable_script.is_valid());
	CHECK(int(unloadable_script->call("get_count")) == 22);
	CHECK(!GDScriptTests::TestGDScriptCacheAccessor::has_static(unloadable_script->get_fully_qualified_name()));

	retained_script.unref();
	unloadable_script.unref();
	cleanup_cached_scripts(scripts);
	CHECK(!ResourceCache::has(retained_path));
	CHECK(!ResourceCache::has(unloadable_path));
}

TEST_CASE("[Modules][GDScript][BytecodeCache] CacheResolver resolves inner classes and rejects failed non-reloading scripts") {
	ensure_gdscript_language_initialized();

	const String base_dir = make_temp_suite_dir("gdb_cache_resolver");
	const String outer_path = base_dir.path_join("outer.gd");
	const String consumer_path = base_dir.path_join("consumer.gd");
	const String broken_path = base_dir.path_join("broken.gd");

	write_text_file(
			outer_path,
			"extends RefCounted\nclass Inner:\n\tvar value: int = 99\n\tfunc get_value() -> int:\n\t\treturn value\n");
	write_text_file(
			consumer_path,
			vformat("extends RefCounted\nconst OUTER := preload(\"%s\")\nconst INNER := OUTER.Inner\nfunc make_inner_value() -> int:\n\tvar obj: OUTER.Inner = INNER.new()\n\treturn obj.get_value()\n", outer_path));
	write_text_file(
			broken_path,
			"extends RefCounted\nfunc broken(:\n\tpass\n");

	Vector<String> valid_scripts;
	valid_scripts.push_back(outer_path);
	valid_scripts.push_back(consumer_path);
	export_scripts_to_bytecode_and_delete_sources(valid_scripts, true);

	Error load_error = OK;
	Ref<GDScript> consumer_script = ResourceLoader::load(consumer_path, "", ResourceFormatLoader::CACHE_MODE_REUSE, &load_error);
	REQUIRE(load_error == OK);
	REQUIRE(consumer_script.is_valid());

	Ref<RefCounted> consumer_instance = memnew(RefCounted);
	consumer_instance->set_script(consumer_script);
	CHECK(int(consumer_instance->call("make_inner_value")) == 99);

	// Load a syntactically broken .gd script so GDScriptCache holds a failed non-reloading script,
	// then assert GDScriptBytecodeCacheResolver refuses to link it.
	ERR_PRINT_OFF;
	Error broken_error = OK;
	Ref<GDScript> broken_script = GDScriptCache::get_full_script(broken_path, broken_error);
	ERR_PRINT_ON;
	REQUIRE(broken_script.is_valid());
	CHECK(!broken_script->is_valid());

	GDScriptBytecodeCacheResolver resolver(consumer_path);
	bool is_local_class = false;
	Error resolve_error = OK;
	Ref<GDScript> resolved_broken = resolver.resolve_script(broken_path, broken_path, is_local_class, resolve_error);
	CHECK(resolve_error != OK);
	CHECK(resolved_broken.is_null());

	consumer_instance->set_script(Variant());
	consumer_instance.unref();
	consumer_script.unref();
	broken_script.unref();
	valid_scripts.push_back(broken_path);
	cleanup_cached_scripts(valid_scripts);
}

TEST_CASE("[Modules][GDScript][BytecodeCache] Script-to-script cycle (A <-> B) single-threaded and threaded") {
	ensure_gdscript_language_initialized();

	for (int mode = 0; mode < 4; mode++) {
		const String base_dir = make_temp_suite_dir(vformat("gdb_cycle_ab_%d", mode));
		const String path_a = base_dir.path_join("script_a.gd");
		const String path_b = base_dir.path_join("script_b.gd");

		write_text_file(
				path_a,
				vformat("extends RefCounted\nconst B := preload(\"%s\")\nfunc from_a() -> int:\n\treturn 10 + B.new().leaf_b()\nfunc leaf_a() -> int:\n\treturn 3\n", path_b));
		write_text_file(
				path_b,
				vformat("extends RefCounted\nconst A := preload(\"%s\")\nfunc from_b() -> int:\n\treturn 20 + A.new().leaf_a()\nfunc leaf_b() -> int:\n\treturn 4\n", path_a));

		Vector<String> scripts;
		scripts.push_back(path_a);
		scripts.push_back(path_b);
		export_scripts_to_bytecode_and_delete_sources(scripts, true);

		Error load_error = OK;
		Ref<GDScript> script_a;
		if (mode == 0) {
			script_a = ResourceLoader::load(path_a, "", ResourceFormatLoader::CACHE_MODE_REUSE, &load_error);
		} else if (mode == 1) {
			script_a = GDScriptCache::get_full_script(path_a, load_error);
		} else if (mode == 2) {
			script_a = load_via_threaded_request(path_a, false, &load_error);
		} else {
			script_a = load_via_threaded_request(path_a, true, &load_error);
		}

		REQUIRE(load_error == OK);
		REQUIRE(script_a.is_valid());
		CHECK(script_a->is_valid());
		CHECK(script_a->is_bytecode_backed());
		CHECK(ResourceCache::has(path_a));
		CHECK(ResourceCache::has(path_b));
		CHECK(GDScriptTests::TestGDScriptCacheAccessor::has_full(path_a));
		CHECK(GDScriptTests::TestGDScriptCacheAccessor::has_full(path_b));

		Ref<GDScript> script_b = ResourceLoader::load(path_b);
		REQUIRE(script_b.is_valid());
		CHECK(script_b->is_valid());

		Ref<RefCounted> inst_a = memnew(RefCounted);
		inst_a->set_script(script_a);
		CHECK(int(inst_a->call("from_a")) == 14);

		Ref<RefCounted> inst_b = memnew(RefCounted);
		inst_b->set_script(script_b);
		CHECK(int(inst_b->call("from_b")) == 23);

		inst_a->set_script(Variant());
		inst_b->set_script(Variant());
		inst_a.unref();
		inst_b.unref();
		script_a.unref();
		script_b.unref();
		cleanup_cached_scripts(scripts);
		CHECK(wait_for_resource_cache_eviction(path_a));
		CHECK(wait_for_resource_cache_eviction(path_b));
	}
}

TEST_CASE("[Modules][GDScript][BytecodeCache] Mixed cycles (A -> scene.tscn -> A and A -> scene.tscn -> B -> A)") {
	ensure_gdscript_language_initialized();

	SUBCASE("A -> scene.tscn -> A (single-threaded and threaded)") {
		for (int mode = 0; mode < 3; mode++) {
			const String base_dir = make_temp_suite_dir(vformat("gdb_mixed_asa_%d", mode));
			const String path_a = base_dir.path_join("actor_a.gd");
			const String scene_path = base_dir.path_join("actor_scene.tscn");

			write_text_file(
					path_a,
					vformat("extends Node\nconst SCENE := preload(\"%s\")\nfunc ping() -> int:\n\treturn 77\nfunc has_scene() -> bool:\n\treturn SCENE != null\n", scene_path));
			write_text_file(
					scene_path,
					vformat("[gd_scene format=3]\n[ext_resource type=\"Script\" path=\"%s\" id=\"1\"]\n[node name=\"Actor\" type=\"Node\"]\nscript = ExtResource(\"1\")\n", path_a));

			Vector<String> scripts;
			scripts.push_back(path_a);
			export_scripts_to_bytecode_and_delete_sources(scripts, true);

			Error load_error = OK;
			Ref<GDScript> script_a;
			if (mode == 0) {
				script_a = ResourceLoader::load(path_a, "", ResourceFormatLoader::CACHE_MODE_REUSE, &load_error);
			} else if (mode == 1) {
				script_a = load_via_threaded_request(path_a, false, &load_error);
			} else {
				script_a = load_via_threaded_request(path_a, true, &load_error);
			}

			REQUIRE(load_error == OK);
			REQUIRE(script_a.is_valid());
			CHECK(script_a->is_valid());
			CHECK(ResourceCache::has(path_a));
			CHECK(ResourceCache::has(scene_path));
			CHECK(GDScriptTests::TestGDScriptCacheAccessor::has_full(path_a));

			Ref<PackedScene> scene = ResourceLoader::load(scene_path);
			REQUIRE(scene.is_valid());

			Node *node = scene->instantiate();
			REQUIRE(node != nullptr);
			CHECK(int(node->call("ping")) == 77);
			CHECK(bool(node->call("has_scene")));
			memdelete(node);

			scene.unref();
			script_a.unref();
			cleanup_cached_scripts(scripts);
			CHECK(wait_for_resource_cache_eviction(path_a));
			CHECK(wait_for_resource_cache_eviction(scene_path));
		}
	}

	SUBCASE("A -> scene.tscn -> B -> A (single-threaded and threaded)") {
		for (int mode = 0; mode < 3; mode++) {
			const String base_dir = make_temp_suite_dir(vformat("gdb_mixed_asba_%d", mode));
			const String path_a = base_dir.path_join("root_a.gd");
			const String path_b = base_dir.path_join("child_b.gd");
			const String scene_path = base_dir.path_join("mid_scene.tscn");

			write_text_file(
					path_a,
					vformat("extends Node\nconst MID_SCENE := preload(\"%s\")\nfunc root_value() -> int:\n\treturn 123\n", scene_path));
			write_text_file(
					path_b,
					vformat("extends Node\nconst ROOT_A := preload(\"%s\")\nfunc read_root() -> int:\n\tvar node = ROOT_A.new()\n\tvar value: int = node.root_value()\n\tnode.free()\n\treturn value\n", path_a));
			write_text_file(
					scene_path,
					vformat("[gd_scene format=3]\n[ext_resource type=\"Script\" path=\"%s\" id=\"1\"]\n[node name=\"Mid\" type=\"Node\"]\nscript = ExtResource(\"1\")\n", path_b));

			Vector<String> scripts;
			scripts.push_back(path_a);
			scripts.push_back(path_b);
			export_scripts_to_bytecode_and_delete_sources(scripts, true);

			Error load_error = OK;
			Ref<GDScript> script_a;
			if (mode == 0) {
				script_a = ResourceLoader::load(path_a, "", ResourceFormatLoader::CACHE_MODE_REUSE, &load_error);
			} else if (mode == 1) {
				script_a = load_via_threaded_request(path_a, false, &load_error);
			} else {
				script_a = load_via_threaded_request(path_a, true, &load_error);
			}

			REQUIRE(load_error == OK);
			REQUIRE(script_a.is_valid());
			CHECK(script_a->is_valid());
			CHECK(ResourceCache::has(path_a));
			CHECK(ResourceCache::has(path_b));
			CHECK(ResourceCache::has(scene_path));
			CHECK(GDScriptTests::TestGDScriptCacheAccessor::has_full(path_a));
			CHECK(GDScriptTests::TestGDScriptCacheAccessor::has_full(path_b));

			Ref<PackedScene> scene = ResourceLoader::load(scene_path);
			REQUIRE(scene.is_valid());

			Node *mid_node = scene->instantiate();
			REQUIRE(mid_node != nullptr);
			CHECK(int(mid_node->call("read_root")) == 123);
			memdelete(mid_node);

			scene.unref();
			script_a.unref();
			cleanup_cached_scripts(scripts);
			CHECK(wait_for_resource_cache_eviction(path_a));
			CHECK(wait_for_resource_cache_eviction(path_b));
			CHECK(wait_for_resource_cache_eviction(scene_path));
		}
	}
}

#ifdef THREADS_ENABLED
TEST_CASE("[Modules][GDScript][BytecodeCache] Concurrent unrelated loads of cycle root and staged descendant (commit and failure discard)") {
	ensure_gdscript_language_initialized();
	GateLoaderGuard gate_guard;

	SUBCASE("Commit path blocks unrelated root, scene, and direct GDScriptCache callers until transaction commits") {
		const String base_dir = make_temp_suite_dir("gdb_concurrent_staged_commit");
		const String path_a = base_dir.path_join("staged_root.gd");
		const String scene_path = base_dir.path_join("staged_descendant.tscn");
		const String gate_path = base_dir.path_join("sync_gate.gdbsyncgate");

		write_text_file(gate_path, "gate\n");
		// Constants are serialized in declaration order: A_SCENE resolves before Z_GATE.
		write_text_file(
				path_a,
				vformat("extends Node\nconst A_SCENE := preload(\"%s\")\nconst Z_GATE := preload(\"%s\")\nfunc check_ready() -> int:\n\treturn 555 if (A_SCENE != null and Z_GATE != null) else 0\n", scene_path, gate_path));
		write_text_file(
				scene_path,
				vformat("[gd_scene format=3]\n[ext_resource type=\"Script\" path=\"%s\" id=\"1\"]\n[node name=\"StagedNode\" type=\"Node\"]\nscript = ExtResource(\"1\")\n", path_a));

		gate_guard.loader->reset(false, false, Vector<String>());
		Vector<String> scripts;
		scripts.push_back(path_a);
		export_scripts_to_bytecode_and_delete_sources(scripts, true);

		Vector<String> uncommitted_paths;
		uncommitted_paths.push_back(path_a);
		uncommitted_paths.push_back(scene_path);
		gate_guard.loader->reset(true, false, uncommitted_paths);

		REQUIRE(ResourceLoader::load_threaded_request(path_a, "", true) == OK);
		REQUIRE(wait_for_flag(gate_guard.loader->gate_entered));

		// At this point, scene_path has finished deserializing and is THREAD_LOAD_STAGED inside A's transaction,
		// while A has not committed yet. Neither resource may be globally visible, and an unrelated thread
		// must not be considered part of A's in-flight load chain.
		CHECK(gate_guard.loader->saw_uncommitted_caches_empty.is_set());
		CHECK(!ResourceCache::has(path_a));
		CHECK(!ResourceCache::has(scene_path));
		CHECK(!GDScriptTests::TestGDScriptCacheAccessor::has_full(path_a));
		CHECK(!GDScriptCache::is_script_loading_on_current_chain(path_a, GDScriptCache::get_cached_script(path_a).ptr()));
		CHECK(!GDScriptCache::is_script_loading_on_current_chain(path_a, nullptr));

		ConcurrentResourceLoadTask root_waiter;
		ConcurrentResourceLoadTask scene_waiter;
		ConcurrentResourceLoadTask direct_cache_waiter;
		root_waiter.start(path_a, false);
		scene_waiter.start(scene_path, false);
		direct_cache_waiter.start(path_a, true);

		REQUIRE(wait_for_flag(root_waiter.started));
		REQUIRE(wait_for_flag(scene_waiter.started));
		REQUIRE(wait_for_flag(direct_cache_waiter.started));

		// Verify all unrelated threads stay blocked while the transaction is uncommitted.
		OS::get_singleton()->delay_usec(40 * 1000);
		CHECK(!root_waiter.finished.is_set());
		CHECK(!scene_waiter.finished.is_set());
		CHECK(!direct_cache_waiter.finished.is_set());

		// Release the gate to commit the transaction.
		gate_guard.loader->allow_finish.set();

		Error root_error = OK;
		Ref<GDScript> root_script = ResourceLoader::load_threaded_get(path_a, &root_error);
		root_waiter.join();
		scene_waiter.join();
		direct_cache_waiter.join();

		REQUIRE(root_error == OK);
		REQUIRE(root_script.is_valid());
		CHECK(root_script->is_valid());

		REQUIRE(root_waiter.error == OK);
		REQUIRE(root_waiter.resource.is_valid());
		CHECK(root_waiter.resource.ptr() == root_script.ptr());

		REQUIRE(direct_cache_waiter.error == OK);
		REQUIRE(direct_cache_waiter.resource.is_valid());
		CHECK(direct_cache_waiter.resource.ptr() == root_script.ptr());

		REQUIRE(scene_waiter.error == OK);
		Ref<PackedScene> waited_scene = scene_waiter.resource;
		REQUIRE(waited_scene.is_valid());
		CHECK(ResourceCache::has(path_a));
		CHECK(ResourceCache::has(scene_path));
		CHECK(GDScriptTests::TestGDScriptCacheAccessor::has_full(path_a));

		Node *node = waited_scene->instantiate();
		REQUIRE(node != nullptr);
		CHECK(int(node->call("check_ready")) == 555);
		memdelete(node);

		waited_scene.unref();
		root_waiter.resource.unref();
		scene_waiter.resource.unref();
		direct_cache_waiter.resource.unref();
		root_script.unref();
		cleanup_cached_scripts(scripts);
		CHECK(wait_for_resource_cache_eviction(path_a));
		CHECK(wait_for_resource_cache_eviction(scene_path));
	}

	SUBCASE("Failure discard leaves zero poisoned cache entries and allows a clean subsequent load") {
		const String base_dir = make_temp_suite_dir("gdb_concurrent_staged_fail");
		const String path_a = base_dir.path_join("failing_root.gd");
		const String scene_path = base_dir.path_join("failing_descendant.tscn");
		const String gate_path = base_dir.path_join("failing_gate.gdbsyncgate");

		write_text_file(gate_path, "gate\n");
		write_text_file(
				path_a,
				vformat("extends Node\nconst A_SCENE := preload(\"%s\")\nconst Z_GATE := preload(\"%s\")\nfunc check_ready() -> int:\n\treturn 888 if (A_SCENE != null and Z_GATE != null) else 0\n", scene_path, gate_path));
		write_text_file(
				scene_path,
				vformat("[gd_scene format=3]\n[ext_resource type=\"Script\" path=\"%s\" id=\"1\"]\n[node name=\"FailNode\" type=\"Node\"]\nscript = ExtResource(\"1\")\n", path_a));

		gate_guard.loader->reset(false, false, Vector<String>());
		Vector<String> scripts;
		scripts.push_back(path_a);
		export_scripts_to_bytecode_and_delete_sources(scripts, true);

		Vector<String> uncommitted_paths;
		uncommitted_paths.push_back(path_a);
		uncommitted_paths.push_back(scene_path);
		gate_guard.loader->reset(true, true, uncommitted_paths);

		ERR_PRINT_OFF;
		REQUIRE(ResourceLoader::load_threaded_request(path_a, "", true) == OK);
		REQUIRE(wait_for_flag(gate_guard.loader->gate_entered));

		CHECK(gate_guard.loader->saw_uncommitted_caches_empty.is_set());
		CHECK(!ResourceCache::has(path_a));
		CHECK(!ResourceCache::has(scene_path));

		ConcurrentResourceLoadTask root_waiter;
		ConcurrentResourceLoadTask scene_waiter;
		ConcurrentResourceLoadTask direct_cache_waiter;
		root_waiter.start(path_a, false);
		scene_waiter.start(scene_path, false);
		direct_cache_waiter.start(path_a, true);

		REQUIRE(wait_for_flag(root_waiter.started));
		REQUIRE(wait_for_flag(scene_waiter.started));
		REQUIRE(wait_for_flag(direct_cache_waiter.started));

		OS::get_singleton()->delay_usec(40 * 1000);
		CHECK(!root_waiter.finished.is_set());
		CHECK(!scene_waiter.finished.is_set());
		CHECK(!direct_cache_waiter.finished.is_set());

		// Trigger failure inside A's transaction after scene_path was already staged.
		gate_guard.loader->allow_finish.set();

		Error root_error = OK;
		Ref<Resource> failed_root = ResourceLoader::load_threaded_get(path_a, &root_error);
		root_waiter.join();
		scene_waiter.join();
		direct_cache_waiter.join();
		ERR_PRINT_ON;

		CHECK(root_error != OK);
		CHECK(failed_root.is_null());
		CHECK(root_waiter.error != OK);
		CHECK(root_waiter.resource.is_null());
		CHECK(scene_waiter.error != OK);
		CHECK(scene_waiter.resource.is_null());
		CHECK(direct_cache_waiter.error != OK);
		CHECK(direct_cache_waiter.resource.is_null());

		// Zero poisoned entries may remain in ResourceCache or GDScriptCache.
		CHECK(!ResourceCache::has(path_a));
		CHECK(!ResourceCache::has(scene_path));
		CHECK(!ResourceCache::has(gate_path));
		CHECK(!GDScriptTests::TestGDScriptCacheAccessor::has_shallow(path_a));
		CHECK(!GDScriptTests::TestGDScriptCacheAccessor::has_full(path_a));

		// Subsequent fresh load with gate succeeding must succeed cleanly.
		gate_guard.loader->reset(false, false, Vector<String>());
		Error retry_error = OK;
		Ref<GDScript> fresh_script = ResourceLoader::load(path_a, "", ResourceFormatLoader::CACHE_MODE_REUSE, &retry_error);
		REQUIRE(retry_error == OK);
		REQUIRE(fresh_script.is_valid());
		CHECK(fresh_script->is_valid());
		CHECK(ResourceCache::has(path_a));
		CHECK(ResourceCache::has(scene_path));
		CHECK(GDScriptTests::TestGDScriptCacheAccessor::has_full(path_a));

		Ref<PackedScene> fresh_scene = ResourceLoader::load(scene_path);
		REQUIRE(fresh_scene.is_valid());
		Node *node = fresh_scene->instantiate();
		REQUIRE(node != nullptr);
		CHECK(int(node->call("check_ready")) == 888);
		memdelete(node);

		fresh_scene.unref();
		fresh_script.unref();
		cleanup_cached_scripts(scripts);
		CHECK(wait_for_resource_cache_eviction(path_a));
		CHECK(wait_for_resource_cache_eviction(scene_path));
	}
}
#endif // THREADS_ENABLED

} // namespace TestGDScriptBytecodeCache

#endif // GDSCRIPT_BYTECODE_EXPORT_ENABLED
