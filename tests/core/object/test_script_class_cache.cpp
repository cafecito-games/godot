/**************************************************************************/
/*  test_script_class_cache.cpp                                           */
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

TEST_FORCE_LINK(test_script_class_cache)

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_uid.h"
#include "core/object/script_language.h"
#include "tests/test_utils.h"

namespace TestScriptClassCache {

namespace {

class ScopedScriptProjectDir {
	String old_resource_path;
	String temp_directory;

public:
	explicit ScopedScriptProjectDir(const String &p_suffix) {
		old_resource_path = TestProjectSettingsInternalsAccessor::resource_path();
		temp_directory = TestUtils::get_temp_path(p_suffix);
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		if (filesystem->dir_exists(temp_directory)) {
			Ref<DirAccess> cleanup = DirAccess::open(temp_directory);
			if (cleanup.is_valid()) {
				cleanup->erase_contents_recursive();
			}
			filesystem->remove(temp_directory);
		}
		filesystem->make_dir_recursive(temp_directory);
		TestProjectSettingsInternalsAccessor::resource_path() = temp_directory;
		ScriptServer::global_classes_clear();
		ProjectSettings::get_singleton()->store_global_class_list(Array());
		Ref<DirAccess> data_cleanup = DirAccess::open(ProjectSettings::get_singleton()->get_project_data_path());
		if (data_cleanup.is_valid()) {
			data_cleanup->erase_contents_recursive();
		}
	}

	~ScopedScriptProjectDir() {
		ScriptServer::global_classes_clear();
		ProjectSettings::get_singleton()->store_global_class_list(Array());
		TestProjectSettingsInternalsAccessor::resource_path() = old_resource_path;
		Ref<DirAccess> cleanup = DirAccess::open(temp_directory);
		if (cleanup.is_valid()) {
			cleanup->erase_contents_recursive();
		}
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		if (filesystem.is_valid()) {
			filesystem->remove(temp_directory);
		}
	}
};

static void write_text_file(const String &p_path, const String &p_content) {
	Ref<DirAccess> directory_access = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	REQUIRE(directory_access.is_valid());
	const String parent_directory = p_path.get_base_dir();
	if (!parent_directory.is_empty() && parent_directory != "res://") {
		REQUIRE_EQ(directory_access->make_dir_recursive(parent_directory), OK);
	}
	Ref<FileAccess> file_access = FileAccess::open(p_path, FileAccess::WRITE);
	REQUIRE(file_access.is_valid());
	file_access->store_string(p_content);
}

static bool has_temporary_files(const String &p_directory_path) {
	Ref<DirAccess> directory_access = DirAccess::open(p_directory_path);
	if (directory_access.is_null()) {
		return false;
	}
	directory_access->list_dir_begin();
	while (true) {
		const String entry = directory_access->get_next();
		if (entry.is_empty()) {
			break;
		}
		if (entry.ends_with(".tmp")) {
			directory_access->list_dir_end();
			return true;
		}
	}
	directory_access->list_dir_end();
	return false;
}

} // namespace

TEST_CASE("[ScriptServer] add_global_class updates is_abstract and is_tool flags") {
	ScriptServer::global_classes_clear();
	ScriptServer::add_global_class("FlaggedClass", "RefCounted", "GDScript", "res://flagged.gd", false, false);
	CHECK_FALSE(ScriptServer::is_global_class_abstract("FlaggedClass"));
	CHECK_FALSE(ScriptServer::is_global_class_tool("FlaggedClass"));

	ScriptServer::add_global_class("FlaggedClass", "RefCounted", "GDScript", "res://flagged.gd", true, true);
	CHECK(ScriptServer::is_global_class_abstract("FlaggedClass"));
	CHECK(ScriptServer::is_global_class_tool("FlaggedClass"));

	ScriptServer::global_classes_clear();
}

TEST_CASE("[ScriptServer] save_global_classes preserves cached icons atomically") {
	ScopedScriptProjectDir project_dir("script_class_save_icons");

	Dictionary initial_entry;
	initial_entry["base"] = StringName("Node");
	initial_entry["class"] = StringName("IconClass");
	initial_entry["icon"] = "res://icon.svg";
	initial_entry["is_abstract"] = false;
	initial_entry["is_tool"] = true;
	initial_entry["language"] = StringName("GDScript");
	initial_entry["path"] = "res://icon_class.gd";
	Array initial_classes;
	initial_classes.push_back(initial_entry);
	ProjectSettings::get_singleton()->store_global_class_list(initial_classes);

	ScriptServer::add_global_class("IconClass", "Node", "GDScript", "res://icon_class.gd", false, true);
	ScriptServer::save_global_classes();

	Ref<ConfigFile> config_file;
	config_file.instantiate();
	REQUIRE_EQ(config_file->load(ProjectSettings::get_singleton()->get_global_class_list_path()), OK);
	Array saved_classes = config_file->get_value("", "list", Array());
	REQUIRE_EQ(saved_classes.size(), 1);
	Dictionary saved_entry = saved_classes[0];
	CHECK_EQ(String(saved_entry["class"]), "IconClass");
	CHECK_EQ(String(saved_entry["icon"]), "res://icon.svg");
	CHECK_FALSE(has_temporary_files(ProjectSettings::get_singleton()->get_project_data_path()));
}

#ifdef TOOLS_ENABLED
TEST_CASE("[ScriptServer] refresh_global_classes_from_disk syncs loose scripts and respects ignore rules") {
	ScriptLanguage *gdscript_language = ScriptServer::get_language_for_extension("gd");
	if (!gdscript_language) {
		return;
	}

	ScopedScriptProjectDir project_dir("script_class_refresh_disk");

	write_text_file("res://project.godot", "config_version=5\n");
	write_text_file("res://plain.gd", "extends Node\n\nfunc hello() -> void:\n\tpass\n");
	write_text_file("res://base_no_name.gd", "extends Node2D\n");
	write_text_file("res://derived.gd", "class_name DerivedClass\nextends \"res://base_no_name.gd\"\n");
	write_text_file("res://tool_abstract.gd", "@tool\n@abstract\nclass_name ToolAbstractClass\nextends RefCounted\n");
	write_text_file("res://ignored/.gdignore", "");
	write_text_file("res://ignored/ignored_class.gd", "class_name IgnoredClass\nextends Node\n");
	write_text_file("res://nested/project.godot", "config_version=5\n");
	write_text_file("res://nested/nested_class.gd", "class_name NestedProjectClass\nextends Node\n");

	// Plain scripts without class_name still report base type through get_global_class_name(),
	// while find_global_class_name() fast-skips them.
	String plain_base;
	CHECK_EQ(gdscript_language->get_global_class_name("res://plain.gd", &plain_base), String());
	CHECK_EQ(plain_base, "Node");
	CHECK_EQ(gdscript_language->find_global_class_name("res://plain.gd"), String());

	// Seed a stale class entry that does not exist on disk.
	ScriptServer::add_global_class("StaleDeletedClass", "Node", "GDScript", "res://deleted.gd", false, false);

	CHECK(ScriptServer::refresh_global_classes_from_disk(true));
	CHECK_FALSE(ScriptServer::is_global_class("StaleDeletedClass"));
	CHECK_FALSE(ScriptServer::is_global_class("IgnoredClass"));
	CHECK_FALSE(ScriptServer::is_global_class("NestedProjectClass"));

	REQUIRE(ScriptServer::is_global_class("DerivedClass"));
	CHECK_EQ(ScriptServer::get_global_class_path("DerivedClass"), "res://derived.gd");
	CHECK_EQ(ScriptServer::get_global_class_base("DerivedClass"), StringName("Node2D"));

	REQUIRE(ScriptServer::is_global_class("ToolAbstractClass"));
	CHECK_EQ(ScriptServer::get_global_class_path("ToolAbstractClass"), "res://tool_abstract.gd");
	CHECK_EQ(ScriptServer::get_global_class_base("ToolAbstractClass"), StringName("RefCounted"));
	CHECK(ScriptServer::is_global_class_abstract("ToolAbstractClass"));
	CHECK(ScriptServer::is_global_class_tool("ToolAbstractClass"));
	CHECK(FileAccess::exists(ProjectSettings::get_singleton()->get_global_class_list_path()));
	CHECK_FALSE(has_temporary_files(ProjectSettings::get_singleton()->get_project_data_path()));

	// Second refresh with no filesystem changes should report unchanged and skip disk writes.
	CHECK_FALSE(ScriptServer::refresh_global_classes_from_disk(true));

	// Rename DerivedClass on disk and confirm refresh updates both memory and persisted cache.
	write_text_file("res://derived.gd", "class_name RenamedDerivedClass\nextends \"res://base_no_name.gd\"\n");
	CHECK(ScriptServer::refresh_global_classes_from_disk(true));
	CHECK_FALSE(ScriptServer::is_global_class("DerivedClass"));
	REQUIRE(ScriptServer::is_global_class("RenamedDerivedClass"));
	CHECK_EQ(ScriptServer::get_global_class_base("RenamedDerivedClass"), StringName("Node2D"));

	Ref<ConfigFile> config_file;
	config_file.instantiate();
	REQUIRE_EQ(config_file->load(ProjectSettings::get_singleton()->get_global_class_list_path()), OK);
	Array persisted = config_file->get_value("", "list", Array());
	CHECK_EQ(persisted.size(), 2);
}

TEST_CASE("[ScriptServer] refresh_global_classes_from_disk resolves uid:// extends when UID cache is missing or stale") {
	ScriptLanguage *gdscript_language = ScriptServer::get_language_for_extension("gd");
	if (!gdscript_language) {
		return;
	}

	ScopedScriptProjectDir project_dir("script_class_uid_extends");
	ResourceUID *resource_uid = ResourceUID::get_singleton();
	ResourceUIDScanForUIDOnStartup old_callback = ResourceUID::scan_for_uid_on_startup;

	const ResourceUID::ID base_uid = 44556677;
	const String base_uid_text = resource_uid->id_to_text(base_uid);

	write_text_file("res://project.godot", "config_version=5\n");
	write_text_file("res://moved/uid_base.gd", "extends Node3D\n");
	write_text_file("res://moved/uid_base.gd.uid", base_uid_text + "\n");
	write_text_file("res://uid_derived.gd", "class_name UidDerivedClass\nextends \"" + base_uid_text + "\"\n");

	// Missing UID mapping case: has_id() during FileAccess::fix_path triggers startup scan.
	resource_uid->clear();
	ResourceUID::scan_for_uid_on_startup = ResourceUID::scan_project_uids_from_disk;
	CHECK(ScriptServer::refresh_global_classes_from_disk(true));
	REQUIRE(ScriptServer::is_global_class("UidDerivedClass"));
	CHECK_EQ(ScriptServer::get_global_class_base("UidDerivedClass"), StringName("Node3D"));
	CHECK_EQ(ResourceUID::scan_for_uid_on_startup, nullptr);

	// Stale UID mapping case: cached path does not exist on disk and must self-heal.
	ScriptServer::global_classes_clear();
	resource_uid->clear();
	resource_uid->add_id(base_uid, "res://stale/old_uid_base.gd");
	ResourceUID::scan_for_uid_on_startup = ResourceUID::scan_project_uids_from_disk;
	CHECK(ScriptServer::refresh_global_classes_from_disk(true));
	REQUIRE(ScriptServer::is_global_class("UidDerivedClass"));
	CHECK_EQ(ScriptServer::get_global_class_base("UidDerivedClass"), StringName("Node3D"));
	CHECK_EQ(resource_uid->get_id_path(base_uid), "res://moved/uid_base.gd");

	ResourceUID::scan_for_uid_on_startup = old_callback;
	resource_uid->clear();
}
#endif // TOOLS_ENABLED

} // namespace TestScriptClassCache
