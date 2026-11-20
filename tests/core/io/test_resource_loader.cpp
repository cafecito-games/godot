/**************************************************************************/
/*  test_resource_loader.cpp                                              */
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

TEST_FORCE_LINK(test_resource_loader)

#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"

namespace TestResourceLoader {

#ifdef THREADS_ENABLED

// Generous enough that a loaded CI machine won't report a false deadlock, short
// enough that an actual regression fails the test instead of hanging it.
constexpr uint64_t MAIN_THREAD_PUMP_TIMEOUT_USEC = 30 * 1000 * 1000;

struct MainThreadPumpState {
	// Set up before the load starts, read-only afterwards.
	String outer_path;
	String middle_path;
	String pumped_path;
	String override_path;
	String callback_path;

	SafeFlag worker_entered_load;
	SafeFlag main_thread_pumped;
	SafeFlag worker_timed_out;
	SafeFlag pumped_within_load;

	// Only touched on the main thread.
	Ref<Resource> registered_override;
	Ref<Resource> looked_up_override;
};

static MainThreadPumpState *main_thread_pump_state = nullptr;

static void _notify_main_thread_pumped() {
	MainThreadPumpState *state = main_thread_pump_state;
	if (!state) {
		return;
	}

	if (ResourceLoader::is_within_load()) {
		state->pumped_within_load.set();
	}

	// An unrelated load performed by pumped work must not disturb the loads that are
	// suspended in the wait that pumped it.
	ResourceLoader::load(state->callback_path);

	state->main_thread_pumped.set();
}

static bool _wait_for_flag(const SafeFlag &p_flag) {
	const uint64_t deadline = OS::get_singleton()->get_ticks_usec() + MAIN_THREAD_PUMP_TIMEOUT_USEC;
	while (!p_flag.is_set()) {
		if (OS::get_singleton()->get_ticks_usec() >= deadline) {
			return false;
		}
		OS::get_singleton()->delay_usec(1000);
	}
	return true;
}

// Stands in for a resource whose load can only finish once the main thread drives
// a main-thread-only server (the RenderingServer with single-threaded rendering) or
// flushes the main MessageQueue.
class MainThreadPumpLoader : public ResourceFormatLoader {
	GDSOFTCLASS(MainThreadPumpLoader, ResourceFormatLoader);

public:
	static constexpr const char *EXTENSION = "mainpump";

	virtual Ref<Resource> load(const String &p_path, const String &p_original_path = "", Error *r_error = nullptr, bool p_use_sub_threads = false, float *r_progress = nullptr, CacheMode p_cache_mode = CACHE_MODE_REUSE) override {
		if (r_error) {
			*r_error = OK;
		}

		MainThreadPumpState *state = main_thread_pump_state;
		if (state) {
			if (!Thread::is_main_thread()) {
				state->worker_entered_load.set();
				if (!_wait_for_flag(state->main_thread_pumped)) {
					state->worker_timed_out.set();
				}
			} else if (p_path == state->outer_path) {
				ResourceLoader::load(state->middle_path);
				// Whatever ran while the load below was waiting must not have dropped the
				// override registered for this nesting level.
				state->looked_up_override = ResourceLoader::get_resource_ref_override(state->override_path);
			} else if (p_path == state->middle_path) {
				state->registered_override = ResourceLoader::ensure_resource_ref_override_for_outer_load(state->override_path, "Resource");
				// Makes the main thread join the wait for the threaded load from inside
				// nested loads of its own, so that whatever gets pumped from that wait can
				// be told apart from the loads in progress.
				ResourceLoader::load(state->pumped_path);
			}
		}

		Ref<Resource> resource;
		resource.instantiate();
		return resource;
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

TEST_CASE("[ResourceLoader] Main-thread load() of an in-flight threaded load keeps the main thread pumping") {
	if (WorkerThreadPool::get_singleton()->get_thread_count() <= 0) {
		return;
	}

	MainThreadPumpState state;
	state.outer_path = vformat("res://test_resource_loader_pump_outer.%s", MainThreadPumpLoader::EXTENSION);
	state.middle_path = vformat("res://test_resource_loader_pump_middle.%s", MainThreadPumpLoader::EXTENSION);
	state.pumped_path = vformat("res://test_resource_loader_pump_threaded.%s", MainThreadPumpLoader::EXTENSION);
	state.override_path = vformat("res://test_resource_loader_pump_override.%s", MainThreadPumpLoader::EXTENSION);
	state.callback_path = vformat("res://test_resource_loader_pump_callback.%s", MainThreadPumpLoader::EXTENSION);
	main_thread_pump_state = &state;

	Ref<MainThreadPumpLoader> loader;
	loader.instantiate();
	ResourceLoader::add_resource_format_loader(loader);

	REQUIRE(ResourceLoader::load_threaded_request(state.pumped_path) == OK);

	// Nothing is pumped here, so the worker is guaranteed to still be parked inside
	// its load when the main thread joins the wait below.
	REQUIRE(_wait_for_flag(state.worker_entered_load));
	REQUIRE_FALSE(state.main_thread_pumped.is_set());

	// From now on, the worker can only finish if the main thread flushes the main
	// MessageQueue while waiting for the load task, which is what a main thread
	// blocked on a main-thread-only server would need to do as well.
	callable_mp_static(&_notify_main_thread_pumped).call_deferred();

	Error error = FAILED;
	Ref<Resource> resource = ResourceLoader::load(state.outer_path, "", ResourceLoader::CACHE_MODE_REUSE, &error);

	CHECK(error == OK);
	CHECK(resource.is_valid());
	CHECK_FALSE(state.worker_timed_out.is_set());
	CHECK(state.main_thread_pumped.is_set());
	// Whatever is pumped to unblock the wait is not part of the loads in progress, so
	// it must neither inherit nor clobber their load context.
	CHECK_FALSE(state.pumped_within_load.is_set());
	CHECK(state.registered_override.is_valid());
	CHECK(state.looked_up_override == state.registered_override);

	Ref<Resource> pumped_resource = ResourceLoader::load_threaded_get(state.pumped_path);
	CHECK(pumped_resource.is_valid());

	ResourceLoader::remove_resource_format_loader(loader);
	main_thread_pump_state = nullptr;
}

#endif // THREADS_ENABLED

} // namespace TestResourceLoader
