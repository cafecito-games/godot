/**************************************************************************/
/*  test_resource_uid.cpp                                                 */
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

TEST_FORCE_LINK(test_resource_uid)

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_uid.h"
#include "tests/test_utils.h"

namespace TestResourceUID {

namespace {

class ScopedResourceProjectDir {
	String old_resource_path;
	String temp_directory;

public:
	explicit ScopedResourceProjectDir(const String &p_suffix) {
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
	}

	~ScopedResourceProjectDir() {
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

	const String &get_directory() const { return temp_directory; }
};

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

TEST_CASE("[ResourceUID] Must encode/decode maximum/minimum UID correctly") {
	CHECK_MESSAGE(ResourceUID::get_singleton()->id_to_text(0x7fffffffffffffff) == "uid://d4n4ub6itg400", "Maximum UID must encode correctly.");
	CHECK_MESSAGE(ResourceUID::get_singleton()->text_to_id("uid://d4n4ub6itg400") == 0x7fffffffffffffff, "Maximum UID must decode correctly.");

	CHECK_MESSAGE(ResourceUID::get_singleton()->id_to_text(0) == "uid://a", "Minimum UID must encode correctly.");
	CHECK_MESSAGE(ResourceUID::get_singleton()->text_to_id("uid://a") == 0, "Minimum UID must decode correctly.");
}

TEST_CASE("[ResourceUID] Must encode and decode invalid UIDs correctly") {
	ResourceUID *rid = ResourceUID::get_singleton();
	CHECK_MESSAGE(rid->id_to_text(-1) == "uid://<invalid>", "Invalid UID -1 must encode correctly.");
	CHECK_MESSAGE(rid->text_to_id("uid://<invalid>") == -1, "Invalid UID -1 must decode correctly.");

	CHECK_MESSAGE(rid->id_to_text(-2) == rid->id_to_text(-1), "Invalid UID -2 must encode to the same as -1.");

	CHECK_MESSAGE(rid->text_to_id("dm3rdgs30kfci") == -1, "UID without scheme must decode correctly.");
}

TEST_CASE("[ResourceUID] Must encode and decode various UIDs correctly") {
	ResourceUID *rid = ResourceUID::get_singleton();
	CHECK_MESSAGE(rid->id_to_text(1) == "uid://b", "UID 1 must encode correctly.");
	CHECK_MESSAGE(rid->text_to_id("uid://b") == 1, "UID 1 must decode correctly.");

	CHECK_MESSAGE(rid->id_to_text(8060368642360689600) == "uid://dm3rdgs30kfci", "A normal UID must encode correctly.");
	CHECK_MESSAGE(rid->text_to_id("uid://dm3rdgs30kfci") == 8060368642360689600, "A normal UID must decode correctly.");
}

TEST_CASE("[ResourceUID] Atomic cache save, update, and remove persistence") {
	ScopedResourceProjectDir project_dir("resource_uid_atomic_cache");
	ResourceUID *resource_uid = ResourceUID::get_singleton();
	resource_uid->clear();

	const ResourceUID::ID kept_id = 123456;
	const ResourceUID::ID removed_id = 654321;
	resource_uid->add_id(kept_id, "res://kept.gd");
	resource_uid->add_id(removed_id, "res://removed.gd");

	CHECK_EQ(resource_uid->save_to_cache(), OK);
	CHECK(FileAccess::exists(ResourceUID::get_cache_file()));
	CHECK_FALSE(has_temporary_files(ProjectSettings::get_singleton()->get_project_data_path()));

	resource_uid->remove_id(removed_id);
	CHECK_EQ(resource_uid->update_cache(), OK);
	CHECK_FALSE(has_temporary_files(ProjectSettings::get_singleton()->get_project_data_path()));

	resource_uid->clear();
	CHECK_EQ(resource_uid->load_from_cache(true), OK);
	CHECK(resource_uid->has_id(kept_id));
	CHECK_EQ(resource_uid->get_id_path(kept_id), "res://kept.gd");
	CHECK_FALSE(resource_uid->has_id(removed_id));

	resource_uid->clear();
}

#ifdef TOOLS_ENABLED
TEST_CASE("[ResourceUID] Self-heals stale UID path via startup scan") {
	ScopedResourceProjectDir project_dir("resource_uid_stale_heal");
	ResourceUID *resource_uid = ResourceUID::get_singleton();
	ResourceUIDScanForUIDOnStartup old_callback = ResourceUID::scan_for_uid_on_startup;
	resource_uid->clear();

	Ref<DirAccess> directory_access = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	REQUIRE(directory_access.is_valid());
	REQUIRE_EQ(directory_access->make_dir_recursive("res://moved"), OK);

	const ResourceUID::ID moved_id = 987654321;
	const String moved_uid_text = resource_uid->id_to_text(moved_id);
	const ResourceUID::ID missing_tres_id = 123456789;
	const String missing_tres_uid_text = resource_uid->id_to_text(missing_tres_id);

	{
		Ref<FileAccess> script_file = FileAccess::open("res://moved/sample.gd", FileAccess::WRITE);
		REQUIRE(script_file.is_valid());
		script_file->store_string("extends RefCounted\n");
	}
	{
		Ref<FileAccess> uid_file = FileAccess::open("res://moved/sample.gd.uid", FileAccess::WRITE);
		REQUIRE(uid_file.is_valid());
		uid_file->store_string(moved_uid_text + "\n");
	}
	{
		Ref<FileAccess> tres_file = FileAccess::open("res://moved/data.tres", FileAccess::WRITE);
		REQUIRE(tres_file.is_valid());
		tres_file->store_string("[gd_resource type=\"Resource\" format=3 uid=\"" + missing_tres_uid_text + "\"]\n\n[resource]\n");
	}

	resource_uid->add_id(moved_id, "res://stale/old_sample.gd");
	ResourceUID::scan_for_uid_on_startup = ResourceUID::scan_project_uids_from_disk;

	CHECK_EQ(resource_uid->get_id_path(moved_id), "res://moved/sample.gd");
	CHECK_EQ(ResourceUID::scan_for_uid_on_startup, nullptr);
	CHECK_EQ(resource_uid->get_id_path(missing_tres_id), "res://moved/data.tres");

	// Reload from the persisted binary cache to verify both healed paths were written atomically.
	resource_uid->clear();
	CHECK_EQ(resource_uid->load_from_cache(true), OK);
	CHECK_EQ(resource_uid->get_id_path(moved_id), "res://moved/sample.gd");
	CHECK_EQ(resource_uid->get_id_path(missing_tres_id), "res://moved/data.tres");
	CHECK_FALSE(has_temporary_files(ProjectSettings::get_singleton()->get_project_data_path()));

	ResourceUID::scan_for_uid_on_startup = old_callback;
	resource_uid->clear();
}
#endif // TOOLS_ENABLED

} // namespace TestResourceUID
