/**************************************************************************/
/*  test_resource_loader_staged_cycles.cpp                                */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_resource_loader_staged_cycles)

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/templates/hash_map.h"
#include "core/templates/safe_refcount.h"
#include "core/templates/vector.h"
#include "tests/test_utils.h"

namespace TestResourceLoaderStagedCycles {

constexpr uint64_t WAIT_TIMEOUT_USEC = 15 * 1000 * 1000;

static bool _wait_for_flag(const SafeFlag &p_flag, uint64_t p_timeout_usec = WAIT_TIMEOUT_USEC) {
	const uint64_t deadline = OS::get_singleton()->get_ticks_usec() + p_timeout_usec;
	while (!p_flag.is_set()) {
		if (OS::get_singleton()->get_ticks_usec() >= deadline) {
			return false;
		}
		OS::get_singleton()->delay_usec(1000);
	}
	return true;
}

class CycleNodeResource : public Resource {
	GDSOFTCLASS(CycleNodeResource, Resource);

public:
	String label;
	Vector<Ref<Resource>> dependencies;
	bool finalized = false;

	void clear_dependencies() {
		dependencies.clear();
	}
};

struct CycleGraphCleanup {
	Vector<Ref<CycleNodeResource>> nodes;

	void track(const Ref<Resource> &p_resource) {
		Ref<CycleNodeResource> node = p_resource;
		if (node.is_valid()) {
			nodes.push_back(node);
		}
	}

	~CycleGraphCleanup() {
		for (int i = 0; i < nodes.size(); i++) {
			if (nodes[i].is_valid()) {
				nodes[i]->clear_dependencies();
			}
		}
		nodes.clear();
	}
};

class TransactionCallbackTracker : public RefCounted {
	GDSOFTCLASS(TransactionCallbackTracker, RefCounted);

public:
	SafeFlag invoked;
	SafeFlag committed;

	void on_finalize(bool p_committed) {
		if (p_committed) {
			committed.set();
		}
		invoked.set();
	}
};

struct CycleNodeSpec {
	String path;
	String label;
	bool register_partial = false;
	bool fail_after_dependencies = false;
	Error failure_error = ERR_PARSE_ERROR;
	Vector<String> dependency_paths;

	// Optional synchronization hooks for testing concurrent visibility while uncommitted.
	SafeFlag *entered_after_dependencies_flag = nullptr;
	SafeFlag *allow_finish_flag = nullptr;
	SafeFlag *saw_cache_empty_before_commit_flag = nullptr;
	Vector<String> cache_check_paths_before_commit;
	Ref<TransactionCallbackTracker> callback_tracker;
};

class StagedCycleTestLoader : public ResourceFormatLoader {
	GDSOFTCLASS(StagedCycleTestLoader, ResourceFormatLoader);

	Mutex spec_mutex;
	HashMap<String, CycleNodeSpec> specs;

public:
	static constexpr const char *EXTENSION = "stagedcycle";

	void set_spec(const CycleNodeSpec &p_spec) {
		MutexLock lock(spec_mutex);
		specs[p_spec.path] = p_spec;
	}

	void clear_specs() {
		MutexLock lock(spec_mutex);
		specs.clear();
	}

	virtual Ref<Resource> load(const String &p_path, const String &p_original_path = "", Error *r_error = nullptr, bool p_use_sub_threads = false, float *r_progress = nullptr, CacheMode p_cache_mode = CACHE_MODE_REUSE) override {
		if (r_error) {
			*r_error = OK;
		}

		CycleNodeSpec spec;
		{
			MutexLock lock(spec_mutex);
			HashMap<String, CycleNodeSpec>::ConstIterator entry = specs.find(p_path);
			if (!entry) {
				if (r_error) {
					*r_error = ERR_FILE_NOT_FOUND;
				}
				return Ref<Resource>();
			}
			spec = entry->value;
		}

		Ref<CycleNodeResource> node;
		node.instantiate();
		node->label = spec.label;
		node->set_path_cache(p_path);

		if (spec.register_partial) {
			const bool registered = ResourceLoader::register_current_load_partial_resource(node, p_path);
			if (!registered) {
				if (r_error) {
					*r_error = ERR_BUG;
				}
				return Ref<Resource>();
			}
			if (spec.callback_tracker.is_valid()) {
				ResourceLoader::add_current_load_transaction_callback(
						callable_mp(spec.callback_tracker.ptr(), &TransactionCallbackTracker::on_finalize));
			}
		}

		if (p_use_sub_threads && spec.dependency_paths.size() > 1) {
			Vector<Ref<ResourceLoader::LoadToken>> dependency_tokens;
			dependency_tokens.resize(spec.dependency_paths.size());
			for (int i = 0; i < spec.dependency_paths.size(); i++) {
				dependency_tokens.write[i] = ResourceLoader::_load_start(
						spec.dependency_paths[i],
						"",
						ResourceLoader::LOAD_THREAD_DISTRIBUTE,
						p_cache_mode);
			}
			for (int i = 0; i < dependency_tokens.size(); i++) {
				Error dependency_error = OK;
				Ref<Resource> dependency;
				if (dependency_tokens[i].is_valid()) {
					dependency = ResourceLoader::_load_complete(*dependency_tokens[i].ptr(), &dependency_error);
				} else {
					dependency_error = FAILED;
				}
				if (dependency_error != OK || dependency.is_null()) {
					node->clear_dependencies();
					if (r_error) {
						*r_error = dependency_error != OK ? dependency_error : FAILED;
					}
					return Ref<Resource>();
				}
				node->dependencies.push_back(dependency);
			}
		} else {
			for (int i = 0; i < spec.dependency_paths.size(); i++) {
				Error dependency_error = OK;
				Ref<Resource> dependency = ResourceLoader::load(spec.dependency_paths[i], "", p_cache_mode, &dependency_error);
				if (dependency_error != OK || dependency.is_null()) {
					node->clear_dependencies();
					if (r_error) {
						*r_error = dependency_error != OK ? dependency_error : FAILED;
					}
					return Ref<Resource>();
				}
				node->dependencies.push_back(dependency);
			}
		}

		if (spec.saw_cache_empty_before_commit_flag != nullptr) {
			bool all_absent = true;
			for (int i = 0; i < spec.cache_check_paths_before_commit.size(); i++) {
				if (ResourceCache::has(spec.cache_check_paths_before_commit[i])) {
					all_absent = false;
					break;
				}
			}
			if (all_absent) {
				spec.saw_cache_empty_before_commit_flag->set();
			}
		}

		if (spec.entered_after_dependencies_flag != nullptr) {
			spec.entered_after_dependencies_flag->set();
		}

		if (spec.allow_finish_flag != nullptr) {
			if (!_wait_for_flag(*spec.allow_finish_flag)) {
				node->clear_dependencies();
				if (r_error) {
					*r_error = ERR_TIMEOUT;
				}
				return Ref<Resource>();
			}
		}

		if (spec.fail_after_dependencies) {
			node->clear_dependencies();
			if (r_error) {
				*r_error = spec.failure_error;
			}
			return Ref<Resource>();
		}

		node->finalized = true;
		return node;
	}

	virtual bool exists(const String &p_path) const override {
		return recognize_path(p_path);
	}

	virtual void get_recognized_extensions(List<String> *p_extensions) const override {
		p_extensions->push_back(EXTENSION);
	}

	virtual bool recognize_path(const String &p_path, const String &p_for_type = String()) const override {
		return p_path.get_extension() == EXTENSION;
	}

	virtual bool handles_type(const String &p_type) const override {
		return p_type.is_empty() || p_type == "Resource";
	}

	virtual String get_resource_type(const String &p_path) const override {
		return recognize_path(p_path) ? "Resource" : String();
	}
};

struct LoaderRegistrationGuard {
	Ref<StagedCycleTestLoader> loader;

	LoaderRegistrationGuard() {
		loader.instantiate();
		ResourceLoader::add_resource_format_loader(loader);
	}

	~LoaderRegistrationGuard() {
		ResourceLoader::remove_resource_format_loader(loader);
	}
};

#ifdef THREADS_ENABLED
struct ConcurrentLoadWorkerData {
	String path;
	SafeFlag started;
	SafeFlag finished;
	Error error = FAILED;
	Ref<Resource> resource;
};

static void _concurrent_load_worker(void *p_userdata) {
	ConcurrentLoadWorkerData *worker_data = static_cast<ConcurrentLoadWorkerData *>(p_userdata);
	worker_data->started.set();
	worker_data->resource = ResourceLoader::load(worker_data->path, "", ResourceLoader::CACHE_MODE_REUSE, &worker_data->error);
	worker_data->finished.set();
}
#endif // THREADS_ENABLED

TEST_CASE("[ResourceLoader] Staged single-threaded cycles commit cleanly and hide uncommitted descendants") {
	LoaderRegistrationGuard guard;

	SUBCASE("Two-node cycle A -> B -> A") {
		const String path_a = vformat("res://staged_st_ab_a.%s", StagedCycleTestLoader::EXTENSION);
		const String path_b = vformat("res://staged_st_ab_b.%s", StagedCycleTestLoader::EXTENSION);

		SafeFlag cache_empty_before_commit;
		CycleNodeSpec spec_a;
		spec_a.path = path_a;
		spec_a.label = "A";
		spec_a.register_partial = true;
		spec_a.dependency_paths.push_back(path_b);
		spec_a.saw_cache_empty_before_commit_flag = &cache_empty_before_commit;
		spec_a.cache_check_paths_before_commit.push_back(path_a);
		spec_a.cache_check_paths_before_commit.push_back(path_b);

		CycleNodeSpec spec_b;
		spec_b.path = path_b;
		spec_b.label = "B";
		spec_b.dependency_paths.push_back(path_a);

		guard.loader->set_spec(spec_a);
		guard.loader->set_spec(spec_b);

		Error load_error = FAILED;
		Ref<Resource> resource_a = ResourceLoader::load(path_a, "", ResourceLoader::CACHE_MODE_REUSE, &load_error);
		REQUIRE(load_error == OK);
		REQUIRE(resource_a.is_valid());
		CHECK(cache_empty_before_commit.is_set());

		Ref<CycleNodeResource> node_a = resource_a;
		REQUIRE(node_a.is_valid());
		CHECK(node_a->finalized);
		CHECK(node_a->get_path() == path_a);
		REQUIRE(node_a->dependencies.size() == 1);

		Ref<CycleNodeResource> node_b = node_a->dependencies[0];
		REQUIRE(node_b.is_valid());
		CycleGraphCleanup cleanup;
		cleanup.track(node_a);
		cleanup.track(node_b);
		CHECK(node_b->finalized);
		CHECK(node_b->get_path() == path_b);
		REQUIRE(node_b->dependencies.size() == 1);
		CHECK(node_b->dependencies[0] == node_a);

		CHECK(ResourceCache::has(path_a));
		CHECK(ResourceCache::has(path_b));
		CHECK(ResourceCache::get_ref(path_a) == node_a);
		CHECK(ResourceCache::get_ref(path_b) == node_b);
	}

	SUBCASE("Three-node cycle A -> B -> C -> A") {
		const String path_a = vformat("res://staged_st_abc_a.%s", StagedCycleTestLoader::EXTENSION);
		const String path_b = vformat("res://staged_st_abc_b.%s", StagedCycleTestLoader::EXTENSION);
		const String path_c = vformat("res://staged_st_abc_c.%s", StagedCycleTestLoader::EXTENSION);

		SafeFlag cache_empty_before_commit;
		CycleNodeSpec spec_a;
		spec_a.path = path_a;
		spec_a.label = "A";
		spec_a.register_partial = true;
		spec_a.dependency_paths.push_back(path_b);
		spec_a.saw_cache_empty_before_commit_flag = &cache_empty_before_commit;
		spec_a.cache_check_paths_before_commit.push_back(path_a);
		spec_a.cache_check_paths_before_commit.push_back(path_b);
		spec_a.cache_check_paths_before_commit.push_back(path_c);

		CycleNodeSpec spec_b;
		spec_b.path = path_b;
		spec_b.label = "B";
		spec_b.dependency_paths.push_back(path_c);

		CycleNodeSpec spec_c;
		spec_c.path = path_c;
		spec_c.label = "C";
		spec_c.dependency_paths.push_back(path_a);

		guard.loader->set_spec(spec_a);
		guard.loader->set_spec(spec_b);
		guard.loader->set_spec(spec_c);

		Error load_error = FAILED;
		Ref<Resource> resource_a = ResourceLoader::load(path_a, "", ResourceLoader::CACHE_MODE_REUSE, &load_error);
		REQUIRE(load_error == OK);
		REQUIRE(resource_a.is_valid());
		CHECK(cache_empty_before_commit.is_set());

		Ref<CycleNodeResource> node_a = resource_a;
		REQUIRE(node_a.is_valid());
		REQUIRE(node_a->dependencies.size() == 1);
		Ref<CycleNodeResource> node_b = node_a->dependencies[0];
		REQUIRE(node_b.is_valid());
		REQUIRE(node_b->dependencies.size() == 1);
		Ref<CycleNodeResource> node_c = node_b->dependencies[0];
		REQUIRE(node_c.is_valid());
		CycleGraphCleanup cleanup;
		cleanup.track(node_a);
		cleanup.track(node_b);
		cleanup.track(node_c);
		REQUIRE(node_c->dependencies.size() == 1);
		CHECK(node_c->dependencies[0] == node_a);

		CHECK(ResourceCache::get_ref(path_a) == node_a);
		CHECK(ResourceCache::get_ref(path_b) == node_b);
		CHECK(ResourceCache::get_ref(path_c) == node_c);
	}

	SUBCASE("Mutual partial roots A <-> B merge transactions cleanly") {
		const String path_a = vformat("res://staged_st_mutual_a.%s", StagedCycleTestLoader::EXTENSION);
		const String path_b = vformat("res://staged_st_mutual_b.%s", StagedCycleTestLoader::EXTENSION);

		SafeFlag cache_empty_before_commit;
		CycleNodeSpec spec_a;
		spec_a.path = path_a;
		spec_a.label = "A";
		spec_a.register_partial = true;
		spec_a.dependency_paths.push_back(path_b);
		spec_a.saw_cache_empty_before_commit_flag = &cache_empty_before_commit;
		spec_a.cache_check_paths_before_commit.push_back(path_a);
		spec_a.cache_check_paths_before_commit.push_back(path_b);

		CycleNodeSpec spec_b;
		spec_b.path = path_b;
		spec_b.label = "B";
		spec_b.register_partial = true;
		spec_b.dependency_paths.push_back(path_a);

		guard.loader->set_spec(spec_a);
		guard.loader->set_spec(spec_b);

		Error load_error = FAILED;
		Ref<Resource> resource_a = ResourceLoader::load(path_a, "", ResourceLoader::CACHE_MODE_REUSE, &load_error);
		REQUIRE(load_error == OK);
		REQUIRE(resource_a.is_valid());
		CHECK(cache_empty_before_commit.is_set());

		Ref<CycleNodeResource> node_a = resource_a;
		REQUIRE(node_a.is_valid());
		REQUIRE(node_a->dependencies.size() == 1);
		Ref<CycleNodeResource> node_b = node_a->dependencies[0];
		REQUIRE(node_b.is_valid());
		CycleGraphCleanup cleanup;
		cleanup.track(node_a);
		cleanup.track(node_b);
		REQUIRE(node_b->dependencies.size() == 1);
		CHECK(node_b->dependencies[0] == node_a);

		CHECK(ResourceCache::get_ref(path_a) == node_a);
		CHECK(ResourceCache::get_ref(path_b) == node_b);
	}

	SUBCASE("Diamond plus cycle A -> B -> D -> A and A -> C -> D stages C without deadlocking") {
		const String path_a = vformat("res://staged_st_diamond_a.%s", StagedCycleTestLoader::EXTENSION);
		const String path_b = vformat("res://staged_st_diamond_b.%s", StagedCycleTestLoader::EXTENSION);
		const String path_c = vformat("res://staged_st_diamond_c.%s", StagedCycleTestLoader::EXTENSION);
		const String path_d = vformat("res://staged_st_diamond_d.%s", StagedCycleTestLoader::EXTENSION);

		SafeFlag cache_empty_before_commit;
		CycleNodeSpec spec_a;
		spec_a.path = path_a;
		spec_a.label = "A";
		spec_a.register_partial = true;
		spec_a.dependency_paths.push_back(path_b);
		spec_a.dependency_paths.push_back(path_c);
		spec_a.saw_cache_empty_before_commit_flag = &cache_empty_before_commit;
		spec_a.cache_check_paths_before_commit.push_back(path_a);
		spec_a.cache_check_paths_before_commit.push_back(path_b);
		spec_a.cache_check_paths_before_commit.push_back(path_c);
		spec_a.cache_check_paths_before_commit.push_back(path_d);

		CycleNodeSpec spec_b;
		spec_b.path = path_b;
		spec_b.label = "B";
		spec_b.dependency_paths.push_back(path_d);

		CycleNodeSpec spec_c;
		spec_c.path = path_c;
		spec_c.label = "C";
		spec_c.dependency_paths.push_back(path_d);

		CycleNodeSpec spec_d;
		spec_d.path = path_d;
		spec_d.label = "D";
		spec_d.dependency_paths.push_back(path_a);

		guard.loader->set_spec(spec_a);
		guard.loader->set_spec(spec_b);
		guard.loader->set_spec(spec_c);
		guard.loader->set_spec(spec_d);

		Error load_error = FAILED;
		Ref<Resource> resource_a = ResourceLoader::load(path_a, "", ResourceLoader::CACHE_MODE_REUSE, &load_error);
		REQUIRE(load_error == OK);
		REQUIRE(resource_a.is_valid());
		CHECK(cache_empty_before_commit.is_set());

		Ref<CycleNodeResource> node_a = resource_a;
		REQUIRE(node_a.is_valid());
		REQUIRE(node_a->dependencies.size() == 2);
		Ref<CycleNodeResource> node_b = node_a->dependencies[0];
		Ref<CycleNodeResource> node_c = node_a->dependencies[1];
		REQUIRE(node_b.is_valid());
		REQUIRE(node_c.is_valid());
		REQUIRE(node_b->dependencies.size() == 1);
		REQUIRE(node_c->dependencies.size() == 1);
		CHECK(node_b->dependencies[0] == node_c->dependencies[0]);

		Ref<CycleNodeResource> node_d = node_b->dependencies[0];
		REQUIRE(node_d.is_valid());
		CycleGraphCleanup cleanup;
		cleanup.track(node_a);
		cleanup.track(node_b);
		cleanup.track(node_c);
		cleanup.track(node_d);
		REQUIRE(node_d->dependencies.size() == 1);
		CHECK(node_d->dependencies[0] == node_a);

		CHECK(ResourceCache::get_ref(path_a) == node_a);
		CHECK(ResourceCache::get_ref(path_b) == node_b);
		CHECK(ResourceCache::get_ref(path_c) == node_c);
		CHECK(ResourceCache::get_ref(path_d) == node_d);
	}

	SUBCASE("Non-partial cycle preserves existing ERR_BUSY behavior") {
		const String path_a = vformat("res://staged_st_nonpartial_a.%s", StagedCycleTestLoader::EXTENSION);
		const String path_b = vformat("res://staged_st_nonpartial_b.%s", StagedCycleTestLoader::EXTENSION);

		CycleNodeSpec spec_a;
		spec_a.path = path_a;
		spec_a.label = "A_no_partial";
		spec_a.register_partial = false;
		spec_a.dependency_paths.push_back(path_b);

		CycleNodeSpec spec_b;
		spec_b.path = path_b;
		spec_b.label = "B_no_partial";
		spec_b.register_partial = false;
		spec_b.dependency_paths.push_back(path_a);

		guard.loader->set_spec(spec_a);
		guard.loader->set_spec(spec_b);

		Error load_error = OK;
		ERR_PRINT_OFF;
		Ref<Resource> resource_a = ResourceLoader::load(path_a, "", ResourceLoader::CACHE_MODE_REUSE, &load_error);
		ERR_PRINT_ON;
		CHECK(load_error == ERR_BUSY);
		CHECK(resource_a.is_null());
		CHECK_FALSE(ResourceCache::has(path_a));
		CHECK_FALSE(ResourceCache::has(path_b));
	}
}

#ifdef THREADS_ENABLED

TEST_CASE("[ResourceLoader] Staged threaded cycles commit cleanly on WorkerThreadPool") {
	if (WorkerThreadPool::get_singleton()->get_thread_count() <= 0) {
		return;
	}

	LoaderRegistrationGuard guard;

	SUBCASE("Distributed WorkerThreadPool cycle A -> B -> C -> A") {
		const String path_a = vformat("res://staged_wtp_abc_a.%s", StagedCycleTestLoader::EXTENSION);
		const String path_b = vformat("res://staged_wtp_abc_b.%s", StagedCycleTestLoader::EXTENSION);
		const String path_c = vformat("res://staged_wtp_abc_c.%s", StagedCycleTestLoader::EXTENSION);

		SafeFlag cache_empty_before_commit;
		CycleNodeSpec spec_a;
		spec_a.path = path_a;
		spec_a.label = "A";
		spec_a.register_partial = true;
		spec_a.dependency_paths.push_back(path_b);
		spec_a.saw_cache_empty_before_commit_flag = &cache_empty_before_commit;
		spec_a.cache_check_paths_before_commit.push_back(path_a);
		spec_a.cache_check_paths_before_commit.push_back(path_b);
		spec_a.cache_check_paths_before_commit.push_back(path_c);

		CycleNodeSpec spec_b;
		spec_b.path = path_b;
		spec_b.label = "B";
		spec_b.dependency_paths.push_back(path_c);

		CycleNodeSpec spec_c;
		spec_c.path = path_c;
		spec_c.label = "C";
		spec_c.dependency_paths.push_back(path_a);

		guard.loader->set_spec(spec_a);
		guard.loader->set_spec(spec_b);
		guard.loader->set_spec(spec_c);

		REQUIRE(ResourceLoader::load_threaded_request(path_a, "", true) == OK);

		Error load_error = FAILED;
		Ref<Resource> resource_a = ResourceLoader::load_threaded_get(path_a, &load_error);
		REQUIRE(load_error == OK);
		REQUIRE(resource_a.is_valid());
		CHECK(cache_empty_before_commit.is_set());

		Ref<CycleNodeResource> node_a = resource_a;
		REQUIRE(node_a.is_valid());
		REQUIRE(node_a->dependencies.size() == 1);
		Ref<CycleNodeResource> node_b = node_a->dependencies[0];
		REQUIRE(node_b.is_valid());
		REQUIRE(node_b->dependencies.size() == 1);
		Ref<CycleNodeResource> node_c = node_b->dependencies[0];
		REQUIRE(node_c.is_valid());
		CycleGraphCleanup cleanup;
		cleanup.track(node_a);
		cleanup.track(node_b);
		cleanup.track(node_c);
		REQUIRE(node_c->dependencies.size() == 1);
		CHECK(node_c->dependencies[0] == node_a);

		CHECK(ResourceCache::get_ref(path_a) == node_a);
		CHECK(ResourceCache::get_ref(path_b) == node_b);
		CHECK(ResourceCache::get_ref(path_c) == node_c);
	}

	SUBCASE("Distributed WorkerThreadPool diamond plus cycle A -> {B, C} -> D -> A") {
		const String path_a = vformat("res://staged_wtp_diamond_a.%s", StagedCycleTestLoader::EXTENSION);
		const String path_b = vformat("res://staged_wtp_diamond_b.%s", StagedCycleTestLoader::EXTENSION);
		const String path_c = vformat("res://staged_wtp_diamond_c.%s", StagedCycleTestLoader::EXTENSION);
		const String path_d = vformat("res://staged_wtp_diamond_d.%s", StagedCycleTestLoader::EXTENSION);

		CycleNodeSpec spec_a;
		spec_a.path = path_a;
		spec_a.label = "A";
		spec_a.register_partial = true;
		spec_a.dependency_paths.push_back(path_b);
		spec_a.dependency_paths.push_back(path_c);

		CycleNodeSpec spec_b;
		spec_b.path = path_b;
		spec_b.label = "B";
		spec_b.dependency_paths.push_back(path_d);

		CycleNodeSpec spec_c;
		spec_c.path = path_c;
		spec_c.label = "C";
		spec_c.dependency_paths.push_back(path_d);

		CycleNodeSpec spec_d;
		spec_d.path = path_d;
		spec_d.label = "D";
		spec_d.dependency_paths.push_back(path_a);

		guard.loader->set_spec(spec_a);
		guard.loader->set_spec(spec_b);
		guard.loader->set_spec(spec_c);
		guard.loader->set_spec(spec_d);

		REQUIRE(ResourceLoader::load_threaded_request(path_a, "", true) == OK);

		Error load_error = FAILED;
		Ref<Resource> resource_a = ResourceLoader::load_threaded_get(path_a, &load_error);
		REQUIRE(load_error == OK);
		REQUIRE(resource_a.is_valid());

		Ref<CycleNodeResource> node_a = resource_a;
		REQUIRE(node_a.is_valid());
		REQUIRE(node_a->dependencies.size() == 2);
		Ref<CycleNodeResource> node_b = node_a->dependencies[0];
		Ref<CycleNodeResource> node_c = node_a->dependencies[1];
		REQUIRE(node_b.is_valid());
		REQUIRE(node_c.is_valid());
		CHECK(node_b->dependencies[0] == node_c->dependencies[0]);
		Ref<CycleNodeResource> node_d = node_b->dependencies[0];
		REQUIRE(node_d.is_valid());
		CycleGraphCleanup cleanup;
		cleanup.track(node_a);
		cleanup.track(node_b);
		cleanup.track(node_c);
		cleanup.track(node_d);
		CHECK(node_d->dependencies[0] == node_a);

		CHECK(ResourceCache::get_ref(path_a) == node_a);
		CHECK(ResourceCache::get_ref(path_b) == node_b);
		CHECK(ResourceCache::get_ref(path_c) == node_c);
		CHECK(ResourceCache::get_ref(path_d) == node_d);
	}
}

TEST_CASE("[ResourceLoader] Staged cycle blocks unrelated threads on both root and staged descendant until commit") {
	if (WorkerThreadPool::get_singleton()->get_thread_count() <= 0) {
		return;
	}

	LoaderRegistrationGuard guard;

	for (int mode = 0; mode < 2; mode++) {
		const bool use_worker_pool_root = (mode == 1);
		const String suffix = use_worker_pool_root ? "wtp" : "user";
		const String path_a = vformat("res://staged_block_commit_%s_a.%s", suffix, StagedCycleTestLoader::EXTENSION);
		const String path_b = vformat("res://staged_block_commit_%s_b.%s", suffix, StagedCycleTestLoader::EXTENSION);

		SafeFlag root_paused_after_b;
		SafeFlag allow_root_commit;
		Ref<TransactionCallbackTracker> callback_tracker;
		callback_tracker.instantiate();

		CycleNodeSpec spec_a;
		spec_a.path = path_a;
		spec_a.label = "A";
		spec_a.register_partial = true;
		spec_a.dependency_paths.push_back(path_b);
		spec_a.entered_after_dependencies_flag = &root_paused_after_b;
		spec_a.allow_finish_flag = &allow_root_commit;
		spec_a.callback_tracker = callback_tracker;

		CycleNodeSpec spec_b;
		spec_b.path = path_b;
		spec_b.label = "B";
		spec_b.dependency_paths.push_back(path_a);

		guard.loader->set_spec(spec_a);
		guard.loader->set_spec(spec_b);

		ConcurrentLoadWorkerData root_worker_data;
		Thread root_thread;
		if (use_worker_pool_root) {
			REQUIRE(ResourceLoader::load_threaded_request(path_a, "", true) == OK);
		} else {
			root_worker_data.path = path_a;
			root_thread.start(_concurrent_load_worker, &root_worker_data);
		}

		// Wait until B has finished deserializing and A is paused before commit.
		REQUIRE(_wait_for_flag(root_paused_after_b));
		CHECK_FALSE(ResourceCache::has(path_a));
		CHECK_FALSE(ResourceCache::has(path_b));
		CHECK_FALSE(callback_tracker->invoked.is_set());

		// Threaded status query for staged descendant B must report IN_PROGRESS, never LOADED.
		REQUIRE(ResourceLoader::load_threaded_request(path_b) == OK);
		CHECK(ResourceLoader::load_threaded_get_status(path_b) == ResourceLoader::THREAD_LOAD_IN_PROGRESS);

		ConcurrentLoadWorkerData unrelated_a_data;
		unrelated_a_data.path = path_a;
		ConcurrentLoadWorkerData unrelated_b_data;
		unrelated_b_data.path = path_b;
		ConcurrentLoadWorkerData unrelated_pool_b_data;
		unrelated_pool_b_data.path = path_b;

		Thread unrelated_a_thread;
		Thread unrelated_b_thread;
		unrelated_a_thread.start(_concurrent_load_worker, &unrelated_a_data);
		unrelated_b_thread.start(_concurrent_load_worker, &unrelated_b_data);
		WorkerThreadPool::TaskID unrelated_pool_task = WorkerThreadPool::get_singleton()->add_native_task(&_concurrent_load_worker, &unrelated_pool_b_data);

		REQUIRE(_wait_for_flag(unrelated_a_data.started));
		REQUIRE(_wait_for_flag(unrelated_b_data.started));
		REQUIRE(_wait_for_flag(unrelated_pool_b_data.started));

		// Give all unrelated callers ample time to hit ResourceLoader and prove all stay blocked.
		OS::get_singleton()->delay_usec(40 * 1000);
		CHECK_FALSE(unrelated_a_data.finished.is_set());
		CHECK_FALSE(unrelated_b_data.finished.is_set());
		CHECK_FALSE(unrelated_pool_b_data.finished.is_set());
		CHECK_FALSE(ResourceCache::has(path_a));
		CHECK_FALSE(ResourceCache::has(path_b));

		// Allow A to commit.
		allow_root_commit.set();

		Error root_error = FAILED;
		Ref<Resource> root_resource;
		if (use_worker_pool_root) {
			root_resource = ResourceLoader::load_threaded_get(path_a, &root_error);
		} else {
			root_thread.wait_to_finish();
			root_error = root_worker_data.error;
			root_resource = root_worker_data.resource;
		}

		unrelated_a_thread.wait_to_finish();
		unrelated_b_thread.wait_to_finish();
		WorkerThreadPool::get_singleton()->wait_for_task_completion(unrelated_pool_task);

		Error threaded_b_error = FAILED;
		Ref<Resource> threaded_b_resource = ResourceLoader::load_threaded_get(path_b, &threaded_b_error);

		REQUIRE(root_error == OK);
		REQUIRE(root_resource.is_valid());
		CHECK(callback_tracker->invoked.is_set());
		CHECK(callback_tracker->committed.is_set());

		CHECK(unrelated_a_data.error == OK);
		CHECK(unrelated_a_data.resource == root_resource);

		Ref<CycleNodeResource> node_a = root_resource;
		REQUIRE(node_a.is_valid());
		REQUIRE(node_a->dependencies.size() == 1);
		Ref<CycleNodeResource> node_b = node_a->dependencies[0];
		REQUIRE(node_b.is_valid());
		CycleGraphCleanup cleanup;
		cleanup.track(node_a);
		cleanup.track(node_b);

		CHECK(unrelated_b_data.error == OK);
		CHECK(unrelated_b_data.resource == node_b);
		CHECK(unrelated_pool_b_data.error == OK);
		CHECK(unrelated_pool_b_data.resource == node_b);
		CHECK(threaded_b_error == OK);
		CHECK(threaded_b_resource == node_b);

		CHECK(node_a->finalized);
		CHECK(node_b->finalized);
		CHECK(node_b->dependencies.size() == 1);
		CHECK(node_b->dependencies[0] == node_a);

		CHECK(ResourceCache::has(path_a));
		CHECK(ResourceCache::has(path_b));
		CHECK(ResourceCache::get_ref(path_a) == node_a);
		CHECK(ResourceCache::get_ref(path_b) == node_b);
	}
}

TEST_CASE("[ResourceLoader] Staged cycle failure discards uncommitted resources and allows clean subsequent loads") {
	if (WorkerThreadPool::get_singleton()->get_thread_count() <= 0) {
		return;
	}

	LoaderRegistrationGuard guard;

	for (int mode = 0; mode < 2; mode++) {
		const bool use_worker_pool_root = (mode == 1);
		const String suffix = use_worker_pool_root ? "wtp" : "user";
		const String path_a = vformat("res://staged_fail_discard_%s_a.%s", suffix, StagedCycleTestLoader::EXTENSION);
		const String path_b = vformat("res://staged_fail_discard_%s_b.%s", suffix, StagedCycleTestLoader::EXTENSION);

		SafeFlag root_paused_after_b;
		SafeFlag allow_root_fail;
		Ref<TransactionCallbackTracker> callback_tracker;
		callback_tracker.instantiate();

		CycleNodeSpec spec_a;
		spec_a.path = path_a;
		spec_a.label = "A_fail";
		spec_a.register_partial = true;
		spec_a.fail_after_dependencies = true;
		spec_a.failure_error = ERR_PARSE_ERROR;
		spec_a.dependency_paths.push_back(path_b);
		spec_a.entered_after_dependencies_flag = &root_paused_after_b;
		spec_a.allow_finish_flag = &allow_root_fail;
		spec_a.callback_tracker = callback_tracker;

		CycleNodeSpec spec_b;
		spec_b.path = path_b;
		spec_b.label = "B_fail";
		spec_b.dependency_paths.push_back(path_a);

		guard.loader->set_spec(spec_a);
		guard.loader->set_spec(spec_b);

		ConcurrentLoadWorkerData root_worker_data;
		Thread root_thread;
		if (use_worker_pool_root) {
			REQUIRE(ResourceLoader::load_threaded_request(path_a, "", true) == OK);
		} else {
			root_worker_data.path = path_a;
			root_thread.start(_concurrent_load_worker, &root_worker_data);
		}

		// Pause inside A after B has finished deserializing.
		REQUIRE(_wait_for_flag(root_paused_after_b));
		CHECK_FALSE(ResourceCache::has(path_a));
		CHECK_FALSE(ResourceCache::has(path_b));

		ConcurrentLoadWorkerData unrelated_a_data;
		unrelated_a_data.path = path_a;
		ConcurrentLoadWorkerData unrelated_b_data;
		unrelated_b_data.path = path_b;

		Thread unrelated_a_thread;
		Thread unrelated_b_thread;
		unrelated_a_thread.start(_concurrent_load_worker, &unrelated_a_data);
		unrelated_b_thread.start(_concurrent_load_worker, &unrelated_b_data);

		REQUIRE(_wait_for_flag(unrelated_a_data.started));
		REQUIRE(_wait_for_flag(unrelated_b_data.started));

		OS::get_singleton()->delay_usec(40 * 1000);
		CHECK_FALSE(unrelated_a_data.finished.is_set());
		CHECK_FALSE(unrelated_b_data.finished.is_set());

		// Trigger failure of root A.
		ERR_PRINT_OFF;
		allow_root_fail.set();

		Error root_error = OK;
		Ref<Resource> root_resource;
		if (use_worker_pool_root) {
			root_resource = ResourceLoader::load_threaded_get(path_a, &root_error);
		} else {
			root_thread.wait_to_finish();
			root_error = root_worker_data.error;
			root_resource = root_worker_data.resource;
		}

		unrelated_a_thread.wait_to_finish();
		unrelated_b_thread.wait_to_finish();
		ERR_PRINT_ON;

		CHECK(root_error == ERR_PARSE_ERROR);
		CHECK(root_resource.is_null());
		CHECK(callback_tracker->invoked.is_set());
		CHECK_FALSE(callback_tracker->committed.is_set());

		CHECK(unrelated_a_data.error == ERR_PARSE_ERROR);
		CHECK(unrelated_a_data.resource.is_null());
		CHECK(unrelated_b_data.error == ERR_PARSE_ERROR);
		CHECK(unrelated_b_data.resource.is_null());

		// Neither A nor B may ever have entered ResourceCache.
		CHECK_FALSE(ResourceCache::has(path_a));
		CHECK_FALSE(ResourceCache::has(path_b));

		// Subsequent fresh loads of A and B must succeed cleanly with zero poisoned cache entries.
		CycleNodeSpec fresh_spec_a;
		fresh_spec_a.path = path_a;
		fresh_spec_a.label = "A_fresh";
		fresh_spec_a.register_partial = true;
		fresh_spec_a.dependency_paths.push_back(path_b);

		CycleNodeSpec fresh_spec_b;
		fresh_spec_b.path = path_b;
		fresh_spec_b.label = "B_fresh";
		fresh_spec_b.dependency_paths.push_back(path_a);

		guard.loader->set_spec(fresh_spec_a);
		guard.loader->set_spec(fresh_spec_b);

		Error fresh_error_a = FAILED;
		Ref<Resource> fresh_a = ResourceLoader::load(path_a, "", ResourceLoader::CACHE_MODE_REUSE, &fresh_error_a);
		REQUIRE(fresh_error_a == OK);
		REQUIRE(fresh_a.is_valid());

		Error fresh_error_b = FAILED;
		Ref<Resource> fresh_b = ResourceLoader::load(path_b, "", ResourceLoader::CACHE_MODE_REUSE, &fresh_error_b);
		REQUIRE(fresh_error_b == OK);
		REQUIRE(fresh_b.is_valid());

		Ref<CycleNodeResource> fresh_node_a = fresh_a;
		Ref<CycleNodeResource> fresh_node_b = fresh_b;
		REQUIRE(fresh_node_a.is_valid());
		REQUIRE(fresh_node_b.is_valid());
		CycleGraphCleanup cleanup;
		cleanup.track(fresh_node_a);
		cleanup.track(fresh_node_b);
		CHECK(fresh_node_a->label == "A_fresh");
		CHECK(fresh_node_b->label == "B_fresh");
		CHECK(fresh_node_a->finalized);
		CHECK(fresh_node_b->finalized);
		CHECK(fresh_node_a->dependencies.size() == 1);
		CHECK(fresh_node_a->dependencies[0] == fresh_node_b);
		CHECK(fresh_node_b->dependencies.size() == 1);
		CHECK(fresh_node_b->dependencies[0] == fresh_node_a);
		CHECK(ResourceCache::get_ref(path_a) == fresh_node_a);
		CHECK(ResourceCache::get_ref(path_b) == fresh_node_b);
	}
}

#endif // THREADS_ENABLED

TEST_CASE("[ResourceLoader] Staged cycle commit preserves pre-existing ResourceCache entry without clobbering") {
	Ref<CycleNodeResource> pre_existing_b;
	pre_existing_b.instantiate();
	pre_existing_b->label = "PreExistingB";

	class PreExistingInjectLoader : public ResourceFormatLoader {
		GDSOFTCLASS(PreExistingInjectLoader, ResourceFormatLoader);

	public:
		String path_a;
		String path_b;
		Ref<CycleNodeResource> pre_existing_b;

		virtual Ref<Resource> load(const String &p_path, const String &p_original_path = "", Error *r_error = nullptr, bool p_use_sub_threads = false, float *r_progress = nullptr, CacheMode p_cache_mode = CACHE_MODE_REUSE) override {
			if (r_error) {
				*r_error = OK;
			}
			Ref<CycleNodeResource> node;
			node.instantiate();
			node->set_path_cache(p_path);
			if (p_path == path_a) {
				node->label = "RootA";
				ResourceLoader::register_current_load_partial_resource(node, p_path);
				Error dep_err = OK;
				Ref<Resource> dep_b = ResourceLoader::load(path_b, "", p_cache_mode, &dep_err);
				if (dep_err != OK || dep_b.is_null()) {
					if (r_error) {
						*r_error = FAILED;
					}
					return Ref<Resource>();
				}
				node->dependencies.push_back(dep_b);
				// Publish pre_existing_b directly into ResourceCache before A commits.
				pre_existing_b->set_path(path_b);
			} else if (p_path == path_b) {
				node->label = "LoadedB";
				Error dep_err = OK;
				Ref<Resource> dep_a = ResourceLoader::load(path_a, "", p_cache_mode, &dep_err);
				if (dep_err != OK || dep_a.is_null()) {
					if (r_error) {
						*r_error = FAILED;
					}
					return Ref<Resource>();
				}
				node->dependencies.push_back(dep_a);
			}
			node->finalized = true;
			return node;
		}

		virtual bool exists(const String &p_path) const override {
			return recognize_path(p_path);
		}
		virtual void get_recognized_extensions(List<String> *p_extensions) const override {
			p_extensions->push_back("stagednocl");
		}
		virtual bool recognize_path(const String &p_path, const String &p_for_type = String()) const override {
			return p_path.get_extension() == "stagednocl";
		}
		virtual bool handles_type(const String &p_type) const override {
			return p_type.is_empty() || p_type == "Resource";
		}
		virtual String get_resource_type(const String &p_path) const override {
			return recognize_path(p_path) ? "Resource" : String();
		}
	};

	Ref<PreExistingInjectLoader> inject_loader;
	inject_loader.instantiate();
	inject_loader->path_a = "res://staged_nocl_a.stagednocl";
	inject_loader->path_b = "res://staged_nocl_b.stagednocl";
	inject_loader->pre_existing_b = pre_existing_b;
	ResourceLoader::add_resource_format_loader(inject_loader);

	Error load_error = FAILED;
	Ref<Resource> loaded_a = ResourceLoader::load(inject_loader->path_a, "", ResourceLoader::CACHE_MODE_REUSE, &load_error);
	ResourceLoader::remove_resource_format_loader(inject_loader);

	REQUIRE(load_error == OK);
	REQUIRE(loaded_a.is_valid());
	Ref<CycleNodeResource> node_a = loaded_a;
	CycleGraphCleanup cleanup;
	cleanup.track(node_a);
	if (node_a.is_valid() && node_a->dependencies.size() > 0) {
		cleanup.track(node_a->dependencies[0]);
	}
	CHECK(ResourceCache::get_ref(inject_loader->path_a) == loaded_a);
	CHECK(ResourceCache::get_ref(inject_loader->path_b) == pre_existing_b);
}

TEST_CASE("[ResourceLoader] list_directory maps .gdb to .gd alongside .gdc") {
	const String test_dir = TestUtils::get_temp_path("resource_loader_list_dir_gdb");
	Ref<DirAccess> dir_access = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(dir_access.is_valid());
	REQUIRE(dir_access->make_dir_recursive(test_dir) == OK);

	auto touch_file = [&](const String &p_name) {
		Ref<FileAccess> file = FileAccess::open(test_dir.path_join(p_name), FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string("dummy");
	};

	touch_file("compiled_only.gdb");
	touch_file("remapped_script.gdb");
	touch_file("remapped_script.gd.remap");
	touch_file("legacy_tokens.gdc");
	touch_file("plain_source.gd");

	Vector<String> listed_files = ResourceLoader::list_directory(test_dir);

	CHECK(listed_files.has("compiled_only.gd"));
	CHECK(listed_files.has("remapped_script.gd"));
	CHECK(listed_files.has("legacy_tokens.gd"));
	CHECK(listed_files.has("plain_source.gd"));
	CHECK_FALSE(listed_files.has("compiled_only.gdb"));
	CHECK_FALSE(listed_files.has("remapped_script.gdb"));
	CHECK_FALSE(listed_files.has("remapped_script.gd.remap"));
	CHECK_FALSE(listed_files.has("legacy_tokens.gdc"));

	int remapped_occurrences = 0;
	for (int i = 0; i < listed_files.size(); i++) {
		if (listed_files[i] == "remapped_script.gd") {
			remapped_occurrences++;
		}
	}
	CHECK(remapped_occurrences == 1);
}

} // namespace TestResourceLoaderStagedCycles
