/**************************************************************************/
/*  gdscript_test_runner_suite.h                                          */
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

#include "../gdscript_cache.h"
#include "gdscript_test_runner.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/os/os.h"
#include "scene/resources/packed_scene.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace GDScriptTests {

class TestGDScriptCacheAccessor {
public:
	static bool has_shallow(String p_path) {
		return GDScriptCache::singleton->shallow_gdscript_cache.has(p_path);
	}

	static bool has_full(String p_path) {
		return GDScriptCache::singleton->full_gdscript_cache.has(p_path);
	}

	static bool has_static(String p_fqcn) {
		return GDScriptCache::singleton->static_gdscript_cache.has(p_fqcn);
	}
};

// TODO: Handle some cases failing on release builds. See: https://github.com/godotengine/godot/pull/88452
#ifdef TOOLS_ENABLED
TEST_SUITE("[Modules][GDScript]") {
	TEST_CASE("Script compilation and runtime") {
		bool print_filenames = OS::get_singleton()->get_cmdline_args().find("--print-filenames") != nullptr;
		bool use_binary_tokens = OS::get_singleton()->get_cmdline_args().find("--use-binary-tokens") != nullptr;
		bool use_bytecode = OS::get_singleton()->get_cmdline_args().find("--use-bytecode") != nullptr;
		GDScriptTestRunner runner("modules/gdscript/tests/scripts", true, print_filenames, use_binary_tokens, use_bytecode);
		int fail_count = runner.run_tests();
		INFO("Make sure `*.out` files have expected results.");
		REQUIRE_MESSAGE(fail_count == 0, "All GDScript tests should pass.");
	}
}
#endif // TOOLS_ENABLED

TEST_CASE("[Modules][GDScript] Load source code dynamically and run it") {
	GDScriptLanguage::get_singleton()->init();
	Ref<GDScript> gdscript = memnew(GDScript);
	gdscript->set_source_code(R"(
extends RefCounted

func _init():
	set_meta("result", 42)
)");
	// A spurious `Condition "err" is true` message is printed (despite parsing being successful and returning `OK`).
	// Silence it.
	ERR_PRINT_OFF;
	const Error error = gdscript->reload();
	ERR_PRINT_ON;
	CHECK_MESSAGE(error == OK, "The script should parse successfully.");

	// Run the script by assigning it to a reference-counted object.
	Ref<RefCounted> ref_counted = memnew(RefCounted);
	ref_counted->set_script(gdscript);
	CHECK_MESSAGE(int(ref_counted->get_meta("result")) == 42, "The script should assign object metadata successfully.");
}

TEST_CASE("[Modules][GDScript] Loading keeps ResourceCache and GDScriptCache in sync") {
	const String path = TestUtils::get_temp_path("gdscript_load_test.gd");

	{
		Ref<FileAccess> fa = FileAccess::open(path, FileAccess::ModeFlags::WRITE);
		fa->store_string("extends Node\n");
		fa->close();
	}

	CHECK(!ResourceCache::has(path));
	CHECK(!TestGDScriptCacheAccessor::has_shallow(path));
	CHECK(!TestGDScriptCacheAccessor::has_full(path));

	Ref<GDScript> loaded = ResourceLoader::load(path);

	CHECK(ResourceCache::has(path));
	CHECK(!TestGDScriptCacheAccessor::has_shallow(path));
	CHECK(TestGDScriptCacheAccessor::has_full(path));
}

TEST_CASE("[Modules][GDScript] Validate built-in API") {
	GDScriptLanguage *lang = GDScriptLanguage::get_singleton();

	// Validate methods.
	List<MethodInfo> builtin_methods;
	lang->get_public_functions(&builtin_methods);

	SUBCASE("[Modules][GDScript] Validate built-in methods") {
		for (const MethodInfo &mi : builtin_methods) {
			for (int64_t i = 0; i < mi.arguments.size(); ++i) {
				TEST_COND((mi.arguments[i].name.is_empty() || mi.arguments[i].name.begins_with("_unnamed_arg")),
						vformat("Unnamed argument in position %d of built-in method '%s'.", i, mi.name));
			}
		}
	}

	// Validate annotations.
	List<MethodInfo> builtin_annotations;
	lang->get_public_annotations(&builtin_annotations);

	SUBCASE("[Modules][GDScript] Validate built-in annotations") {
		for (const MethodInfo &ai : builtin_annotations) {
			for (int64_t i = 0; i < ai.arguments.size(); ++i) {
				TEST_COND((ai.arguments[i].name.is_empty() || ai.arguments[i].name.begins_with("_unnamed_arg")),
						vformat("Unnamed argument in position %d of built-in annotation '%s'.", i, ai.name));
			}
		}
	}
}

#ifdef THREADS_ENABLED
static void _write_test_text_file(const String &p_path, const String &p_content) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::ModeFlags::WRITE);
	REQUIRE(file.is_valid());
	file->store_string(p_content);
	file->close();
}

TEST_CASE("[Modules][GDScript] Threaded scene load with preloaded scripted scenes and main-thread contention") {
	if (!GDScriptLanguage::get_singleton()->get_global_map().has(SNAME("Node"))) {
		GDScriptLanguage::get_singleton()->init();
	}

	SUBCASE("Worker compiling script closure with preloaded scenes completes without deadlock") {
		const String base_dir = TestUtils::get_temp_path("threaded_preload_closure");
		Ref<DirAccess> directory = DirAccess::create_for_path(base_dir.get_base_dir());
		REQUIRE(directory.is_valid());
		directory->make_dir_recursive(base_dir);

		const int panel_count = 32;
		String root_ext_resources = "[ext_resource type=\"Script\" path=\"" + base_dir.path_join("root_controller.gd") + "\" id=\"root_script\"]\n";
		String root_children;

		for (int index = 0; index < panel_count; index++) {
			const String row_script_path = base_dir.path_join(vformat("row_%d.gd", index));
			const String row_scene_path = base_dir.path_join(vformat("row_%d.tscn", index));
			const String panel_script_path = base_dir.path_join(vformat("panel_%d.gd", index));
			const String panel_scene_path = base_dir.path_join(vformat("panel_%d.tscn", index));

			_write_test_text_file(
					row_script_path,
					vformat("extends Node\nconst ROOT_REF := preload(\"root_controller.gd\")\nfunc get_row_index() -> int:\n\treturn %d\n", index));
			_write_test_text_file(
					row_scene_path,
					vformat("[gd_scene format=3]\n[ext_resource type=\"Script\" path=\"%s\" id=\"1\"]\n[node name=\"Row%d\" type=\"Node\"]\nscript = ExtResource(\"1\")\n", row_script_path, index));
			_write_test_text_file(
					panel_script_path,
					vformat("extends Node\nconst ROW_SCENE := preload(\"row_%d.tscn\")\n", index));
			_write_test_text_file(
					panel_scene_path,
					vformat("[gd_scene format=3]\n[ext_resource type=\"Script\" path=\"%s\" id=\"1\"]\n[node name=\"Panel%d\" type=\"Node\"]\nscript = ExtResource(\"1\")\n", panel_script_path, index));

			root_ext_resources += vformat("[ext_resource type=\"PackedScene\" path=\"%s\" id=\"p_%d\"]\n", panel_scene_path, index);
			root_children += vformat("[node name=\"Panel%d\" parent=\".\" instance=ExtResource(\"p_%d\")]\n", index, index);
		}

		const String root_script_path = base_dir.path_join("root_controller.gd");
		const String root_scene_path = base_dir.path_join("root_scene.tscn");
		_write_test_text_file(
				root_script_path,
				"extends Node\nconst FIRST_ROW := preload(\"row_0.tscn\")\nfunc is_ready_controller() -> bool:\n\treturn FIRST_ROW != null\n");
		_write_test_text_file(
				root_scene_path,
				"[gd_scene format=3]\n" + root_ext_resources + "[node name=\"Root\" type=\"Node\"]\nscript = ExtResource(\"root_script\")\n" + root_children);

		Error request_error = ResourceLoader::load_threaded_request(root_scene_path);
		REQUIRE(request_error == OK);

		float load_progress = 0.0f;
		ResourceLoader::ThreadLoadStatus load_status = ResourceLoader::load_threaded_get_status(root_scene_path, &load_progress);
		while (load_status == ResourceLoader::THREAD_LOAD_IN_PROGRESS) {
			OS::get_singleton()->delay_usec(1000);
			load_status = ResourceLoader::load_threaded_get_status(root_scene_path, &load_progress);
		}

		CHECK(load_status == ResourceLoader::THREAD_LOAD_LOADED);
		Ref<PackedScene> loaded_scene = ResourceLoader::load_threaded_get(root_scene_path);
		REQUIRE(loaded_scene.is_valid());

		Ref<GDScript> root_script = ResourceLoader::load(root_script_path);
		REQUIRE(root_script.is_valid());
		CHECK(root_script->is_valid());
		CHECK(TestGDScriptCacheAccessor::has_full(root_script_path));

		root_script->clear();
		root_script.unref();
		loaded_scene.unref();
		GDScriptCache::remove_script(root_script_path);
		for (int index = 0; index < panel_count; index++) {
			const String panel_path = base_dir.path_join(vformat("panel_%d.gd", index));
			const String row_path = base_dir.path_join(vformat("row_%d.gd", index));
			Ref<GDScript> panel_script = GDScriptCache::get_cached_script(panel_path);
			if (panel_script.is_valid()) {
				panel_script->clear();
			}
			Ref<GDScript> row_script = GDScriptCache::get_cached_script(row_path);
			if (row_script.is_valid()) {
				row_script->clear();
			}
			GDScriptCache::remove_script(panel_path);
			GDScriptCache::remove_script(row_path);
		}
		CHECK(!ResourceCache::has(base_dir.path_join("row_0.tscn")));
		CHECK(!ResourceCache::has(root_scene_path));
	}

	SUBCASE("Worker preload attaching to main-thread synchronous load completes with valid outer script") {
		const String base_dir = TestUtils::get_temp_path("worker_main_condvar_race");
		Ref<DirAccess> directory = DirAccess::create_for_path(base_dir.get_base_dir());
		REQUIRE(directory.is_valid());
		directory->make_dir_recursive(base_dir);

		String shared_ext_resources;
		for (int index = 0; index < 32; index++) {
			const String sub_resource_path = base_dir.path_join(vformat("sub_res_%d.tres", index));
			_write_test_text_file(sub_resource_path, "[gd_resource type=\"Resource\" format=3]\n[resource]\n");
			shared_ext_resources += vformat("[ext_resource type=\"Resource\" path=\"%s\" id=\"r_%d\"]\n", sub_resource_path, index);
		}

		const String worker_script_path = base_dir.path_join("worker_owner.gd");
		const String shared_script_path = base_dir.path_join("shared_target.gd");
		const String shared_scene_path = base_dir.path_join("shared_target.tscn");
		const String worker_scene_path = base_dir.path_join("worker_owner.tscn");

		const int helper_count = 24;
		String helper_preloads;
		for (int index = 0; index < helper_count; index++) {
			const String helper_path = base_dir.path_join(vformat("helper_%d.gd", index));
			_write_test_text_file(helper_path, vformat("extends RefCounted\nfunc value_%d() -> int:\n\treturn %d\n", index, index));
			helper_preloads += vformat("const HELPER_%d := preload(\"helper_%d.gd\")\n", index, index);
		}

		_write_test_text_file(
				shared_script_path,
				"extends Node\nconst WORKER_REF := preload(\"worker_owner.gd\")\nfunc ok() -> bool:\n\treturn WORKER_REF != null\n");
		_write_test_text_file(
				shared_scene_path,
				"[gd_scene format=3]\n" + shared_ext_resources + "[ext_resource type=\"Script\" path=\"" + shared_script_path + "\" id=\"scr\"]\n[node name=\"Shared\" type=\"Node\"]\nscript = ExtResource(\"scr\")\n");
		_write_test_text_file(
				worker_script_path,
				"extends Node\n" + helper_preloads + "const SHARED_SCENE := preload(\"shared_target.tscn\")\n");
		_write_test_text_file(
				worker_scene_path,
				"[gd_scene format=3]\n[ext_resource type=\"Script\" path=\"" + worker_script_path + "\" id=\"1\"]\n[node name=\"Worker\" type=\"Node\"]\nscript = ExtResource(\"1\")\n");

		REQUIRE(ResourceLoader::load_threaded_request(worker_scene_path) == OK);
		Ref<PackedScene> shared_scene = ResourceLoader::load(shared_scene_path);
		Ref<PackedScene> worker_scene = ResourceLoader::load_threaded_get(worker_scene_path);

		CHECK(shared_scene.is_valid());
		CHECK(worker_scene.is_valid());

		Ref<GDScript> worker_script = ResourceLoader::load(worker_script_path);
		REQUIRE(worker_script.is_valid());
		CHECK(worker_script->is_valid());
		CHECK(TestGDScriptCacheAccessor::has_full(worker_script_path));
		CHECK(!TestGDScriptCacheAccessor::has_shallow(worker_script_path));

		Ref<GDScript> shared_script = GDScriptCache::get_cached_script(shared_script_path);
		if (shared_script.is_valid()) {
			shared_script->clear();
			shared_script.unref();
		}
		worker_script->clear();
		worker_script.unref();
		worker_scene.unref();
		shared_scene.unref();
		GDScriptCache::remove_script(worker_script_path);
		GDScriptCache::remove_script(shared_script_path);
		for (int index = 0; index < helper_count; index++) {
			const String helper_path = base_dir.path_join(vformat("helper_%d.gd", index));
			Ref<GDScript> helper_script = GDScriptCache::get_cached_script(helper_path);
			if (helper_script.is_valid()) {
				helper_script->clear();
			}
			GDScriptCache::remove_script(helper_path);
		}
		CHECK(!ResourceCache::has(shared_scene_path));
		CHECK(!ResourceCache::has(worker_scene_path));
	}
}
#endif // THREADS_ENABLED

} // namespace GDScriptTests
