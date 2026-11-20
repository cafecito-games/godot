/**************************************************************************/
/*  test_gdscript_export_session.h                                        */
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
#include "../gdscript_cache.h"
#include "../gdscript_export_compile_session.h"
#include "../gdscript_function.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/string/print_string.h"
#include "scene/resources/packed_scene.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestGDScriptExportSession {

class ScopedResourcePath {
	String previous_resource_path;

public:
	explicit ScopedResourcePath(const String &p_new_resource_path) :
			previous_resource_path(TestProjectSettingsInternalsAccessor::resource_path()) {
		TestProjectSettingsInternalsAccessor::resource_path() = p_new_resource_path;
	}

	~ScopedResourcePath() {
		TestProjectSettingsInternalsAccessor::resource_path() = previous_resource_path;
	}
};

static void write_file_contents(const String &p_path, const String &p_contents) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	REQUIRE(file.is_valid());
	file->store_string(p_contents);
	file->close();
}

static void ensure_gdscript_language_initialized() {
	if (!GDScriptLanguage::get_singleton()->get_global_map().has(SNAME("Node"))) {
		GDScriptLanguage::get_singleton()->init();
	}
}

static bool function_has_constant(const GDScriptFunction *p_function, const Variant &p_expected) {
	REQUIRE(p_function != nullptr);
	ERR_PRINT_OFF;
	for (int index = 0; index < 128; index++) {
		Variant constant_value = p_function->get_constant(index);
		if (constant_value.get_type() == Variant::STRING && String(constant_value) == "<errconst>") {
			ERR_PRINT_ON;
			return false;
		}
		if (constant_value == p_expected) {
			ERR_PRINT_ON;
			return true;
		}
	}
	ERR_PRINT_ON;
	return false;
}

static bool function_has_global_name(const GDScriptFunction *p_function, const StringName &p_expected) {
	REQUIRE(p_function != nullptr);
	ERR_PRINT_OFF;
	for (int index = 0; index < 128; index++) {
		StringName global_name = p_function->get_global_name(index);
		if (global_name == SNAME("<errgname>")) {
			ERR_PRINT_ON;
			return false;
		}
		if (global_name == p_expected) {
			ERR_PRINT_ON;
			return true;
		}
	}
	ERR_PRINT_ON;
	return false;
}

#ifdef DEBUG_ENABLED
static void capture_print_line(void *p_userdata, const String &p_string, bool p_error, bool p_rich) {
	if (!p_error) {
		Vector<String> *captured_lines = static_cast<Vector<String> *>(p_userdata);
		captured_lines->push_back(p_string);
	}
}

static Vector<String> disassemble_function(const GDScriptFunction *p_function, const String &p_source_code) {
	REQUIRE(p_function != nullptr);
	Vector<String> captured_lines;
	PrintHandlerList handler;
	handler.printfunc = capture_print_line;
	handler.userdata = &captured_lines;
	add_print_handler(&handler);
	p_function->disassemble(p_source_code.split("\n"));
	remove_print_handler(&handler);
	return captured_lines;
}

static bool disassembly_has_instruction(const Vector<String> &p_lines, const String &p_instruction_prefix) {
	for (const String &line : p_lines) {
		const int colon_index = line.find(": ");
		if (colon_index != -1 && line.substr(colon_index + 2).begins_with(p_instruction_prefix)) {
			return true;
		}
	}
	return false;
}
#endif

TEST_SUITE("[Modules][GDScript][ExportSession]") {
	TEST_CASE("Live @tool script, instance, static state, and suspended await are untouched") {
		ensure_gdscript_language_initialized();

		const String project_dir = TestUtils::get_temp_path("gdscript_export_session_live_tool");
		Ref<DirAccess> directory = DirAccess::create_for_path(project_dir.get_base_dir());
		REQUIRE(directory.is_valid());
		directory->make_dir_recursive(project_dir);

		ScopedResourcePath resource_path_guard(project_dir);
		const String script_path = "res://live_tool_script.gd";
		write_file_contents(
				project_dir.path_join("live_tool_script.gd"),
				"@tool\n"
				"extends RefCounted\n"
				"signal resume_now\n"
				"static var shared_counter: int = 10\n"
				"var instance_value: int = 100\n"
				"func wait_and_compute(delta: int) -> int:\n"
				"\tvar captured := instance_value\n"
				"\tawait resume_now\n"
				"\treturn captured + delta + shared_counter\n");

		Ref<GDScript> live_script = ResourceLoader::load(script_path);
		REQUIRE(live_script.is_valid());
		REQUIRE(live_script->is_valid());

		// Mutate static state on the live @tool script.
		live_script->set(SNAME("shared_counter"), 77);
		CHECK_EQ(int(live_script->get(SNAME("shared_counter"))), 77);

		// Create an active instance and mutate its state.
		Ref<RefCounted> live_instance;
		live_instance.instantiate();
		live_instance->set_script(live_script);
		live_instance->set(SNAME("instance_value"), 250);
		CHECK_EQ(int(live_instance->get(SNAME("instance_value"))), 250);

		// Start an async method so a GDScriptFunctionState is suspended on `await`.
		Variant call_result = live_instance->call(SNAME("wait_and_compute"), 5);
		Ref<GDScriptFunctionState> suspended_state = call_result;
		REQUIRE(suspended_state.is_valid());
		CHECK(suspended_state->is_valid());

		// Capture live function pointer before detached export compilation.
		const GDScriptFunction *live_function_before = live_script->get_member_functions().get(SNAME("wait_and_compute"));
		REQUIRE(live_function_before != nullptr);

		ObjectID detached_script_id;
		{
			GDScriptExportCompileSession session(true);
			Error compile_error = OK;
			Ref<GDScript> compiled_script = session.get_or_compile(script_path, compile_error);
			CHECK(!GDScriptCache::has_active_export_session());
			REQUIRE_EQ(compile_error, OK);
			REQUIRE(compiled_script.is_valid());
			CHECK(compiled_script->is_valid());
			CHECK(compiled_script.ptr() != live_script.ptr());
			detached_script_id = compiled_script->get_instance_id();

			// Live cache and ResourceCache still point to the original live script.
			CHECK_EQ(GDScriptCache::get_cached_script(script_path).ptr(), live_script.ptr());
			CHECK_EQ(ResourceCache::get_ref(script_path).ptr(), live_script.ptr());

			// Live function pointer, static state, instance state, and suspended continuation are completely unchanged.
			CHECK_EQ(live_script->get_member_functions().get(SNAME("wait_and_compute")), live_function_before);
			CHECK_EQ(int(live_script->get(SNAME("shared_counter"))), 77);
			CHECK_EQ(int(live_instance->get(SNAME("instance_value"))), 250);
			CHECK(suspended_state->is_valid());
		}

		// Detached script is not retained by static_gdscript_cache or any global cache after session teardown.
		CHECK_EQ(ObjectDB::get_instance(detached_script_id), nullptr);

		// Resume the suspended continuation and verify clean completion with intact state.
		Variant resumed_value;
		live_instance->emit_signal(SNAME("resume_now"));
		// After signal emission completes the continuation, the state is no longer pending.
		CHECK(!suspended_state->is_valid());
		CHECK_EQ(int(live_script->get(SNAME("shared_counter"))), 77);
		CHECK_EQ(int(live_instance->get(SNAME("instance_value"))), 250);

		// Also verify a fresh await continuation using resume() directly returns the exact computed value.
		Ref<GDScriptFunctionState> second_state = live_instance->call(SNAME("wait_and_compute"), 5);
		REQUIRE(second_state.is_valid());
		CHECK(second_state->is_valid());
		{
			GDScriptExportCompileSession second_session(true);
			Error compile_error = OK;
			Ref<GDScript> second_compiled = second_session.get_or_compile(script_path, compile_error);
			CHECK(!GDScriptCache::has_active_export_session());
			REQUIRE_EQ(compile_error, OK);
			REQUIRE(second_compiled.is_valid());
		}
		CHECK(second_state->is_valid());
		resumed_value = second_state->resume(Variant());
		CHECK_EQ(int(resumed_value), 332);

		// Cleanup live script from caches.
		suspended_state.unref();
		second_state.unref();
		live_instance.unref();
		live_script->clear();
		live_script.unref();
		GDScriptCache::remove_script(script_path);
	}

	TEST_CASE("Initially uncached non-GDScript preload resources leave GDScriptCache and ResourceCache unchanged") {
		ensure_gdscript_language_initialized();

		const String project_dir = TestUtils::get_temp_path("gdscript_export_session_uncached_preloads");
		Ref<DirAccess> directory = DirAccess::create_for_path(project_dir.get_base_dir());
		REQUIRE(directory.is_valid());
		directory->make_dir_recursive(project_dir);

		ScopedResourcePath resource_path_guard(project_dir);
		const String data_resource_path = "res://uncached_data.tres";
		const String child_script_path = "res://uncached_child.gd";
		const String child_scene_path = "res://uncached_child.tscn";
		const String root_script_path = "res://uncached_root.gd";

		write_file_contents(
				project_dir.path_join("uncached_data.tres"),
				"[gd_resource type=\"Resource\" format=3]\n"
				"[resource]\n"
				"resource_name = \"DetachedData\"\n");

		write_file_contents(
				project_dir.path_join("uncached_child.gd"),
				"extends Node\n"
				"const CHILD_VALUE := 123\n");

		write_file_contents(
				project_dir.path_join("uncached_child.tscn"),
				"[gd_scene format=3]\n"
				"[ext_resource type=\"Script\" path=\"res://uncached_child.gd\" id=\"1\"]\n"
				"[node name=\"ChildRoot\" type=\"Node\"]\n"
				"script = ExtResource(\"1\")\n");

		write_file_contents(
				project_dir.path_join("uncached_root.gd"),
				"extends Node\n"
				"const DATA_RES: Resource = preload(\"res://uncached_data.tres\")\n"
				"const CHILD_SCENE: PackedScene = preload(\"res://uncached_child.tscn\")\n");

		// Confirm nothing is cached beforehand.
		CHECK(!ResourceCache::has(data_resource_path));
		CHECK(!ResourceCache::has(child_script_path));
		CHECK(!ResourceCache::has(child_scene_path));
		CHECK(!ResourceCache::has(root_script_path));
		CHECK(GDScriptCache::get_cached_script(child_script_path).is_null());
		CHECK(GDScriptCache::get_cached_script(root_script_path).is_null());
		CHECK(!GDScriptCache::has_parser(child_script_path));
		CHECK(!GDScriptCache::has_parser(root_script_path));

		{
			GDScriptExportCompileSession session(true);
			Error compile_error = OK;
			Ref<GDScript> compiled_root = session.get_or_compile(root_script_path, compile_error);
			CHECK(!GDScriptCache::has_active_export_session());
			REQUIRE_EQ(compile_error, OK);
			REQUIRE(compiled_root.is_valid());
			CHECK(compiled_root->is_valid());

			const HashMap<StringName, Variant> &root_constants = compiled_root->get_constants();
			REQUIRE(root_constants.has(SNAME("DATA_RES")));
			REQUIRE(root_constants.has(SNAME("CHILD_SCENE")));

			Ref<Resource> preloaded_data = root_constants[SNAME("DATA_RES")];
			REQUIRE(preloaded_data.is_valid());
			CHECK_EQ(preloaded_data->get_class(), "Resource");
			CHECK_EQ(preloaded_data->get_name(), "DetachedData");
			CHECK_EQ(preloaded_data->get_path(), data_resource_path);

			Ref<PackedScene> preloaded_scene = root_constants[SNAME("CHILD_SCENE")];
			REQUIRE(preloaded_scene.is_valid());
			CHECK_EQ(preloaded_scene->get_class(), "PackedScene");
			CHECK_EQ(preloaded_scene->get_path(), child_scene_path);
			CHECK_EQ(preloaded_scene->get_state()->get_node_count(), 1);
		}

		// Verify neither ResourceCache nor GDScriptCache was populated.
		CHECK(!ResourceCache::has(data_resource_path));
		CHECK(!ResourceCache::has(child_script_path));
		CHECK(!ResourceCache::has(child_scene_path));
		CHECK(!ResourceCache::has(root_script_path));
		CHECK(GDScriptCache::get_cached_script(child_script_path).is_null());
		CHECK(GDScriptCache::get_cached_script(root_script_path).is_null());
		CHECK(!GDScriptCache::has_parser(child_script_path));
		CHECK(!GDScriptCache::has_parser(root_script_path));
		CHECK(!GDScriptCache::has_active_export_session());
	}

	TEST_CASE("Preload cycle script -> scene -> script -> preload(same scene) completes placeholder in place") {
		ensure_gdscript_language_initialized();

		const String project_dir = TestUtils::get_temp_path("gdscript_export_session_preload_cycle");
		Ref<DirAccess> directory = DirAccess::create_for_path(project_dir.get_base_dir());
		REQUIRE(directory.is_valid());
		directory->make_dir_recursive(project_dir);

		ScopedResourcePath resource_path_guard(project_dir);
		const String entry_script_path = "res://cycle_entry.gd";
		const String scene_path = "res://cycle_scene.tscn";
		const String scene_script_path = "res://cycle_scene_node.gd";

		write_file_contents(
				project_dir.path_join("cycle_entry.gd"),
				"extends Node\n"
				"const CYCLE_SCENE: PackedScene = preload(\"res://cycle_scene.tscn\")\n");

		write_file_contents(
				project_dir.path_join("cycle_scene_node.gd"),
				"extends Node\n"
				"const SAME_SCENE: PackedScene = preload(\"res://cycle_scene.tscn\")\n");

		write_file_contents(
				project_dir.path_join("cycle_scene.tscn"),
				"[gd_scene format=3]\n"
				"[ext_resource type=\"Script\" path=\"res://cycle_scene_node.gd\" id=\"1\"]\n"
				"[node name=\"CycleRoot\" type=\"Node\"]\n"
				"script = ExtResource(\"1\")\n");

		CHECK(!ResourceCache::has(entry_script_path));
		CHECK(!ResourceCache::has(scene_path));
		CHECK(!ResourceCache::has(scene_script_path));
		CHECK(GDScriptCache::get_cached_script(entry_script_path).is_null());
		CHECK(GDScriptCache::get_cached_script(scene_script_path).is_null());

		{
			GDScriptExportCompileSession session(true);
			Error entry_error = OK;
			Ref<GDScript> compiled_entry = session.get_or_compile(entry_script_path, entry_error);
			CHECK(!GDScriptCache::has_active_export_session());
			REQUIRE_EQ(entry_error, OK);
			REQUIRE(compiled_entry.is_valid());
			CHECK(compiled_entry->is_valid());

			const HashMap<StringName, Variant> &entry_constants = compiled_entry->get_constants();
			REQUIRE(entry_constants.has(SNAME("CYCLE_SCENE")));
			Ref<PackedScene> entry_scene = entry_constants[SNAME("CYCLE_SCENE")];
			REQUIRE(entry_scene.is_valid());
			CHECK_EQ(entry_scene->get_class(), "PackedScene");
			CHECK_EQ(entry_scene->get_path(), scene_path);
			REQUIRE(entry_scene->get_state().is_valid());
			CHECK_EQ(entry_scene->get_state()->get_node_count(), 1);
			CHECK_EQ(entry_scene->get_state()->get_node_name(0), StringName("CycleRoot"));

			Error scene_script_error = OK;
			Ref<GDScript> compiled_scene_script = session.get_or_compile(scene_script_path, scene_script_error);
			CHECK(!GDScriptCache::has_active_export_session());
			REQUIRE_EQ(scene_script_error, OK);
			REQUIRE(compiled_scene_script.is_valid());
			CHECK(compiled_scene_script->is_valid());

			const HashMap<StringName, Variant> &scene_script_constants = compiled_scene_script->get_constants();
			REQUIRE(scene_script_constants.has(SNAME("SAME_SCENE")));
			Ref<PackedScene> back_scene = scene_script_constants[SNAME("SAME_SCENE")];
			REQUIRE(back_scene.is_valid());
			// Placeholder populated in place must be the exact same PackedScene object with exact type and logical path.
			CHECK_EQ(back_scene.ptr(), entry_scene.ptr());
			CHECK_EQ(back_scene->get_class(), "PackedScene");
			CHECK_EQ(back_scene->get_path(), scene_path);
			CHECK_EQ(back_scene->get_state()->get_node_count(), 1);
			CHECK_EQ(back_scene->get_state()->get_node_name(0), StringName("CycleRoot"));
		}

		// Neither global ResourceCache nor global GDScriptCache should have been modified.
		CHECK(!ResourceCache::has(entry_script_path));
		CHECK(!ResourceCache::has(scene_path));
		CHECK(!ResourceCache::has(scene_script_path));
		CHECK(GDScriptCache::get_cached_script(entry_script_path).is_null());
		CHECK(GDScriptCache::get_cached_script(scene_script_path).is_null());
		CHECK(!GDScriptCache::has_parser(entry_script_path));
		CHECK(!GDScriptCache::has_parser(scene_script_path));
		CHECK(!GDScriptCache::has_active_export_session());
	}

	TEST_CASE("Session guard resets on error and release/debug profile flags control opcodes") {
		ensure_gdscript_language_initialized();

		const String project_dir = TestUtils::get_temp_path("gdscript_export_session_profiles");
		Ref<DirAccess> directory = DirAccess::create_for_path(project_dir.get_base_dir());
		REQUIRE(directory.is_valid());
		directory->make_dir_recursive(project_dir);

		ScopedResourcePath resource_path_guard(project_dir);
		const String invalid_script_path = "res://invalid_syntax.gd";
		const String debug_ops_script_path = "res://debug_ops.gd";
		const String autoload_script_path = "res://my_autoload.gd";
		const String autoload_user_path = "res://uses_autoload.gd";

		write_file_contents(
				project_dir.path_join("invalid_syntax.gd"),
				"extends Node\nfunc broken(:\n\tpass\n");

		const String debug_ops_source =
				"extends Node\n"
				"func check_value(value: int) -> int:\n"
				"\tassert(value > 0, \"secret assert text\")\n"
				"\tbreakpoint\n"
				"\treturn value + 1\n";
		write_file_contents(project_dir.path_join("debug_ops.gd"), debug_ops_source);

		write_file_contents(
				project_dir.path_join("my_autoload.gd"),
				"extends Node\n"
				"var tag: int = 42\n");

		const String autoload_user_source =
				"extends Node\n"
				"func read_tag() -> int:\n"
				"\treturn TestExportAutoload.tag\n";
		write_file_contents(project_dir.path_join("uses_autoload.gd"), autoload_user_source);

		// Active session guard must clear even when compilation fails.
		{
			GDScriptExportCompileSession error_session(true);
			Error compile_error = OK;
			ERR_PRINT_OFF;
			Ref<GDScript> failed_script = error_session.get_or_compile(invalid_script_path, compile_error);
			ERR_PRINT_ON;
			CHECK(!GDScriptCache::has_active_export_session());
			CHECK_NE(compile_error, OK);
			CHECK(failed_script.is_null());
			CHECK(!error_session.get_last_error_message().is_empty());
		}

#ifdef DEBUG_ENABLED
		// Release export profile omits assert/breakpoint/line opcodes (when call stack tracking is off).
		const Variant previous_track_stacks = ProjectSettings::get_singleton()->get_setting("debug/settings/gdscript/always_track_call_stacks", false);
		ProjectSettings::get_singleton()->set_setting("debug/settings/gdscript/always_track_call_stacks", false);

		{
			GDScriptExportCompileSession release_session(true);
			CHECK(release_session.is_release_profile());
			CHECK(!release_session.should_emit_debug_opcodes());
			CHECK(!release_session.should_emit_line_opcodes());

			Error release_error = OK;
			Ref<GDScript> release_script = release_session.get_or_compile(debug_ops_script_path, release_error);
			CHECK(!GDScriptCache::has_active_export_session());
			REQUIRE_EQ(release_error, OK);
			REQUIRE(release_script.is_valid());
			const GDScriptFunction *release_function = release_script->get_member_functions().get(SNAME("check_value"));
			REQUIRE(release_function != nullptr);
			CHECK(!function_has_constant(release_function, String("secret assert text")));

			const Vector<String> release_disassembly = disassemble_function(release_function, debug_ops_source);
			CHECK(!disassembly_has_instruction(release_disassembly, "assert ("));
			CHECK(!disassembly_has_instruction(release_disassembly, "breakpoint"));
			CHECK(!disassembly_has_instruction(release_disassembly, "line "));
		}

		{
			GDScriptExportCompileSession debug_session(false);
			CHECK(!debug_session.is_release_profile());
			CHECK(debug_session.should_emit_debug_opcodes());
			CHECK(debug_session.should_emit_line_opcodes());

			Error debug_error = OK;
			Ref<GDScript> debug_script = debug_session.get_or_compile(debug_ops_script_path, debug_error);
			CHECK(!GDScriptCache::has_active_export_session());
			REQUIRE_EQ(debug_error, OK);
			REQUIRE(debug_script.is_valid());
			const GDScriptFunction *debug_function = debug_script->get_member_functions().get(SNAME("check_value"));
			REQUIRE(debug_function != nullptr);
			CHECK(function_has_constant(debug_function, String("secret assert text")));

			const Vector<String> debug_disassembly = disassemble_function(debug_function, debug_ops_source);
			CHECK(disassembly_has_instruction(debug_disassembly, "assert ("));
			CHECK(disassembly_has_instruction(debug_disassembly, "breakpoint"));
			CHECK(disassembly_has_instruction(debug_disassembly, "line "));
		}

		ProjectSettings::get_singleton()->set_setting("debug/settings/gdscript/always_track_call_stacks", true);
		{
			GDScriptExportCompileSession tracked_release_session(true);
			CHECK(!tracked_release_session.should_emit_debug_opcodes());
			CHECK(tracked_release_session.should_emit_line_opcodes());

			Error tracked_error = OK;
			Ref<GDScript> tracked_script = tracked_release_session.get_or_compile(debug_ops_script_path, tracked_error);
			CHECK(!GDScriptCache::has_active_export_session());
			REQUIRE_EQ(tracked_error, OK);
			REQUIRE(tracked_script.is_valid());
			const GDScriptFunction *tracked_function = tracked_script->get_member_functions().get(SNAME("check_value"));
			REQUIRE(tracked_function != nullptr);
			CHECK(!function_has_constant(tracked_function, String("secret assert text")));

			const Vector<String> tracked_disassembly = disassemble_function(tracked_function, debug_ops_source);
			CHECK(!disassembly_has_instruction(tracked_disassembly, "assert ("));
			CHECK(!disassembly_has_instruction(tracked_disassembly, "breakpoint"));
			CHECK(disassembly_has_instruction(tracked_disassembly, "line "));
		}

		ProjectSettings::get_singleton()->set_setting("debug/settings/gdscript/always_track_call_stacks", previous_track_stacks);
#endif

		// Singleton autoloads compile to OPCODE_STORE_GLOBAL instead of OPCODE_STORE_NAMED_GLOBAL.
		ProjectSettings::get_singleton()->set_setting(
				"autoload/TestExportAutoload",
				ProjectSettings::stringify_autoload_value(autoload_script_path, true));
#ifdef TOOLS_ENABLED
		GDScriptLanguage::get_singleton()->add_named_global_constant(SNAME("TestExportAutoload"), Variant());
#endif

		{
			GDScriptExportCompileSession autoload_session(true);
			Error autoload_error = OK;
			Ref<GDScript> compiled_user = autoload_session.get_or_compile(autoload_user_path, autoload_error);
			CHECK(!GDScriptCache::has_active_export_session());
			REQUIRE_EQ(autoload_error, OK);
			REQUIRE(compiled_user.is_valid());
			const GDScriptFunction *read_tag_function = compiled_user->get_member_functions().get(SNAME("read_tag"));
			REQUIRE(read_tag_function != nullptr);
			CHECK(function_has_global_name(read_tag_function, SNAME("tag")));
			CHECK(!function_has_global_name(read_tag_function, SNAME("TestExportAutoload")));
#ifdef DEBUG_ENABLED
			const Vector<String> autoload_disassembly = disassemble_function(read_tag_function, autoload_user_source);
			CHECK(disassembly_has_instruction(autoload_disassembly, "store global "));
			CHECK(!disassembly_has_instruction(autoload_disassembly, "store named global "));
#endif
		}

#ifdef TOOLS_ENABLED
		GDScriptLanguage::get_singleton()->remove_named_global_constant(SNAME("TestExportAutoload"));
#endif
		ProjectSettings::get_singleton()->clear("autoload/TestExportAutoload");
		CHECK(!GDScriptCache::has_active_export_session());
	}

	TEST_CASE("Detached inner classes do not pollute global GDScriptLanguage orphan_subclasses") {
		ensure_gdscript_language_initialized();

		const String project_dir = TestUtils::get_temp_path("gdscript_export_session_inner_classes");
		Ref<DirAccess> directory = DirAccess::create_for_path(project_dir.get_base_dir());
		REQUIRE(directory.is_valid());
		directory->make_dir_recursive(project_dir);

		ScopedResourcePath resource_path_guard(project_dir);
		const String owner_script_path = "res://inner_owner.gd";
		const String consumer_script_path = "res://inner_consumer.gd";

		write_file_contents(
				project_dir.path_join("inner_owner.gd"),
				"extends RefCounted\n"
				"class Inner:\n"
				"\tvar payload: int = 9\n");

		write_file_contents(
				project_dir.path_join("inner_consumer.gd"),
				"extends RefCounted\n"
				"const OWNER := preload(\"res://inner_owner.gd\")\n"
				"const INNER_CLASS := OWNER.Inner\n");

		String inner_fqcn;
		Ref<GDScript> held_inner_subclass;
		{
			GDScriptExportCompileSession session(true);
			Error consumer_error = OK;
			Ref<GDScript> compiled_consumer = session.get_or_compile(consumer_script_path, consumer_error);
			CHECK(!GDScriptCache::has_active_export_session());
			REQUIRE_EQ(consumer_error, OK);
			REQUIRE(compiled_consumer.is_valid());

			Error owner_error = OK;
			Ref<GDScript> compiled_owner = session.get_or_compile(owner_script_path, owner_error);
			CHECK(!GDScriptCache::has_active_export_session());
			REQUIRE_EQ(owner_error, OK);
			REQUIRE(compiled_owner.is_valid());
			CHECK(compiled_owner->is_detached_export_script());

			const HashMap<StringName, Ref<GDScript>> &owner_subclasses = compiled_owner->get_subclasses();
			REQUIRE(owner_subclasses.has(SNAME("Inner")));
			held_inner_subclass = owner_subclasses[SNAME("Inner")];
			REQUIRE(held_inner_subclass.is_valid());
			CHECK(held_inner_subclass->is_detached_export_script());
			inner_fqcn = held_inner_subclass->get_fully_qualified_name();
			REQUIRE(!inner_fqcn.is_empty());

			// Session teardown calls compiled_owner->clear() while held_inner_subclass is still alive in ObjectDB.
		}

		// Detached inner class must NOT have been registered in GDScriptLanguage::orphan_subclasses.
		CHECK(GDScriptLanguage::get_singleton()->get_orphan_subclass(inner_fqcn).is_null());

		// Also verify a pre-existing live orphan subclass with the same FQCN is neither consumed nor clobbered.
		Ref<GDScript> live_sentinel_subclass;
		live_sentinel_subclass.instantiate();
		GDScriptLanguage::get_singleton()->add_orphan_subclass(inner_fqcn, live_sentinel_subclass->get_instance_id());
		{
			GDScriptExportCompileSession second_session(true);
			Error owner_error = OK;
			Ref<GDScript> recompiled_owner = second_session.get_or_compile(owner_script_path, owner_error);
			CHECK(!GDScriptCache::has_active_export_session());
			REQUIRE_EQ(owner_error, OK);
			REQUIRE(recompiled_owner.is_valid());
			REQUIRE(recompiled_owner->get_subclasses().has(SNAME("Inner")));
			CHECK(recompiled_owner->get_subclasses()[SNAME("Inner")].ptr() != live_sentinel_subclass.ptr());
		}
		CHECK_EQ(GDScriptLanguage::get_singleton()->get_orphan_subclass(inner_fqcn).ptr(), live_sentinel_subclass.ptr());

		held_inner_subclass->clear();
		held_inner_subclass.unref();
		live_sentinel_subclass.unref();
		CHECK(!ResourceCache::has(owner_script_path));
		CHECK(!ResourceCache::has(consumer_script_path));
		CHECK(GDScriptCache::get_cached_script(owner_script_path).is_null());
		CHECK(GDScriptCache::get_cached_script(consumer_script_path).is_null());
	}
}

} // namespace TestGDScriptExportSession
