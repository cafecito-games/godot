/**************************************************************************/
/*  test_project_settings.cpp                                             */
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

TEST_FORCE_LINK(test_project_settings)

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/resource_uid.h"
#include "core/object/message_queue.h"
#include "core/variant/variant.h"
#include "tests/signal_watcher.h"
#include "tests/test_utils.h"

namespace TestProjectSettings {

class ResourceUIDMappingCleanup {
	ResourceUID *resource_uid = nullptr;
	ResourceUID::ID uid = ResourceUID::INVALID_ID;

public:
	ResourceUIDMappingCleanup(ResourceUID *p_resource_uid, ResourceUID::ID p_uid) :
			resource_uid(p_resource_uid), uid(p_uid) {}

	~ResourceUIDMappingCleanup() {
		if (resource_uid->has_id(uid)) {
			resource_uid->remove_id(uid);
		}
	}
};

class ProjectSettingsAutoloadCleanup {
	ProjectSettings *settings = nullptr;
	LocalVector<StringName> settings_to_clear;

public:
	ProjectSettingsAutoloadCleanup(ProjectSettings *p_settings, const LocalVector<StringName> &p_settings_to_clear) :
			settings(p_settings), settings_to_clear(p_settings_to_clear) {}

	~ProjectSettingsAutoloadCleanup() {
		for (const StringName &setting : settings_to_clear) {
			settings->set_setting(setting, Variant());
		}
	}
};

TEST_CASE("[ProjectSettings] Get existing setting") {
	CHECK(ProjectSettings::get_singleton()->has_setting("application/run/main_scene"));

	Variant variant = ProjectSettings::get_singleton()->get_setting("application/run/main_scene");
	CHECK_EQ(variant.get_type(), Variant::STRING);

	String name = variant;
	CHECK_EQ(name, String());
}

TEST_CASE("[ProjectSettings] Default value is ignored if setting exists") {
	CHECK(ProjectSettings::get_singleton()->has_setting("application/run/main_scene"));

	Variant variant = ProjectSettings::get_singleton()->get_setting("application/run/main_scene", "SomeDefaultValue");
	CHECK_EQ(variant.get_type(), Variant::STRING);

	String name = variant;
	CHECK_EQ(name, String());
}

TEST_CASE("[ProjectSettings] Non existing setting is null") {
	CHECK_FALSE(ProjectSettings::get_singleton()->has_setting("not_existing_setting"));

	Variant variant = ProjectSettings::get_singleton()->get_setting("not_existing_setting");
	CHECK_EQ(variant.get_type(), Variant::NIL);
}

TEST_CASE("[ProjectSettings] Non existing setting should return default value") {
	CHECK_FALSE(ProjectSettings::get_singleton()->has_setting("not_existing_setting"));

	Variant variant = ProjectSettings::get_singleton()->get_setting("not_existing_setting");
	CHECK_EQ(variant.get_type(), Variant::NIL);

	variant = ProjectSettings::get_singleton()->get_setting("not_existing_setting", "my_nice_default_value");
	CHECK_EQ(variant.get_type(), Variant::STRING);

	String name = variant;
	CHECK_EQ(name, "my_nice_default_value");

	CHECK_FALSE(ProjectSettings::get_singleton()->has_setting("not_existing_setting"));
}

TEST_CASE("[ProjectSettings] Set value should be returned when retrieved") {
	CHECK_FALSE(ProjectSettings::get_singleton()->has_setting("my_custom_setting"));

	Variant variant = ProjectSettings::get_singleton()->get_setting("my_custom_setting");
	CHECK_EQ(variant.get_type(), Variant::NIL);

	ProjectSettings::get_singleton()->set_setting("my_custom_setting", true);
	CHECK(ProjectSettings::get_singleton()->has_setting("my_custom_setting"));

	variant = ProjectSettings::get_singleton()->get_setting("my_custom_setting");
	CHECK_EQ(variant.get_type(), Variant::BOOL);

	bool value = variant;
	CHECK_EQ(true, value);

	CHECK(ProjectSettings::get_singleton()->has_setting("my_custom_setting"));
}

TEST_CASE("[ProjectSettings] localize_path") {
	String old_resource_path = TestProjectSettingsInternalsAccessor::resource_path();
	TestProjectSettingsInternalsAccessor::resource_path() = DirAccess::create(DirAccess::ACCESS_FILESYSTEM)->get_current_dir();
	String root_path = ProjectSettings::get_singleton()->get_resource_path();
#ifdef WINDOWS_ENABLED
	String root_path_win = ProjectSettings::get_singleton()->get_resource_path().replace_char('/', '\\');
#endif

	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("filename"), "res://filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("path/filename"), "res://path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("path/something/../filename"), "res://path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("path/./filename"), "res://path/filename");
#ifdef WINDOWS_ENABLED
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("path\\filename"), "res://path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("path\\something\\..\\filename"), "res://path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("path\\.\\filename"), "res://path/filename");
#endif

	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("../filename"), "../filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("../path/filename"), "../path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("..\\path\\filename"), "../path/filename");

	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("/testroot/filename"), "/testroot/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("/testroot/path/filename"), "/testroot/path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("/testroot/path/something/../filename"), "/testroot/path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("/testroot/path/./filename"), "/testroot/path/filename");
#ifdef WINDOWS_ENABLED
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("C:/testroot/filename"), "C:/testroot/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("C:/testroot/path/filename"), "C:/testroot/path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("C:/testroot/path/something/../filename"), "C:/testroot/path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("C:/testroot/path/./filename"), "C:/testroot/path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("C:\\testroot\\filename"), "C:/testroot/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("C:\\testroot\\path\\filename"), "C:/testroot/path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("C:\\testroot\\path\\something\\..\\filename"), "C:/testroot/path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path("C:\\testroot\\path\\.\\filename"), "C:/testroot/path/filename");
#endif

	CHECK_EQ(ProjectSettings::get_singleton()->localize_path(root_path + "/filename"), "res://filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path(root_path + "/path/filename"), "res://path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path(root_path + "/path/something/../filename"), "res://path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path(root_path + "/path/./filename"), "res://path/filename");
#ifdef WINDOWS_ENABLED
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path(root_path_win + "\\filename"), "res://filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path(root_path_win + "\\path\\filename"), "res://path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path(root_path_win + "\\path\\something\\..\\filename"), "res://path/filename");
	CHECK_EQ(ProjectSettings::get_singleton()->localize_path(root_path_win + "\\path\\.\\filename"), "res://path/filename");
#endif

	TestProjectSettingsInternalsAccessor::resource_path() = old_resource_path;
}

TEST_CASE("[SceneTree][ProjectSettings] settings_changed signal") {
	SIGNAL_WATCH(ProjectSettings::get_singleton(), SNAME("settings_changed"));

	ProjectSettings::get_singleton()->set_setting("test_signal_setting", "test_value");
	MessageQueue::get_singleton()->flush();

	SIGNAL_CHECK("settings_changed", { {} });

	SIGNAL_UNWATCH(ProjectSettings::get_singleton(), SNAME("settings_changed"));
}

TEST_CASE("[ProjectSettings] get_changed_settings basic functionality") {
	String setting_name = "test_changed_setting";
	ProjectSettings::get_singleton()->set_setting(setting_name, "test_value");

	PackedStringArray changes = ProjectSettings::get_singleton()->get_changed_settings();
	CHECK(changes.has(setting_name));
}

TEST_CASE("[ProjectSettings] get_changed_settings multiple settings") {
	ProjectSettings::get_singleton()->set_setting("test_setting_1", "value1");
	ProjectSettings::get_singleton()->set_setting("test_setting_2", "value2");
	ProjectSettings::get_singleton()->set_setting("another_group/setting", "value3");

	PackedStringArray changes = ProjectSettings::get_singleton()->get_changed_settings();
	CHECK(changes.has("test_setting_1"));
	CHECK(changes.has("test_setting_2"));
	CHECK(changes.has("another_group/setting"));
}

TEST_CASE("[ProjectSettings] check_changed_settings_in_group") {
	ProjectSettings::get_singleton()->set_setting("group1/setting1", "value1");
	ProjectSettings::get_singleton()->set_setting("group1/setting2", "value2");
	ProjectSettings::get_singleton()->set_setting("group2/setting1", "value3");
	ProjectSettings::get_singleton()->set_setting("other_setting", "value4");

	CHECK(ProjectSettings::get_singleton()->check_changed_settings_in_group("group1/"));
	CHECK(ProjectSettings::get_singleton()->check_changed_settings_in_group("group2/"));
	CHECK_FALSE(ProjectSettings::get_singleton()->check_changed_settings_in_group("nonexistent/"));

	CHECK(ProjectSettings::get_singleton()->check_changed_settings_in_group("group1"));
	CHECK(ProjectSettings::get_singleton()->check_changed_settings_in_group("other_setting"));
}

TEST_CASE("[SceneTree][ProjectSettings] Changes cleared after settings_changed signal") {
	SIGNAL_WATCH(ProjectSettings::get_singleton(), SNAME("settings_changed"));

	ProjectSettings::get_singleton()->set_setting("signal_clear_test", "value");

	PackedStringArray changes_before = ProjectSettings::get_singleton()->get_changed_settings();
	CHECK(changes_before.has("signal_clear_test"));

	MessageQueue::get_singleton()->flush();

	SIGNAL_CHECK("settings_changed", { {} });

	PackedStringArray changes_after = ProjectSettings::get_singleton()->get_changed_settings();
	CHECK_FALSE(changes_after.has("signal_clear_test"));

	SIGNAL_UNWATCH(ProjectSettings::get_singleton(), SNAME("settings_changed"));
}

TEST_CASE("[ProjectSettings] No tracking when setting same value") {
	String setting_name = "same_value_test";
	String test_value = "same_value";

	ProjectSettings::get_singleton()->set_setting(setting_name, test_value);
	int count_before = ProjectSettings::get_singleton()->get_changed_settings().size();

	// Setting the same value should not be tracked due to early return.
	ProjectSettings::get_singleton()->set_setting(setting_name, test_value);
	int count_after = ProjectSettings::get_singleton()->get_changed_settings().size();

	CHECK_EQ(count_before, count_after);
}

TEST_CASE("[ProjectSettings][Autoload] Parses path, legacy UID, and combined values") {
	String path;
	bool is_singleton = false;

	ProjectSettings::parse_autoload_value("res://plain.gd", path, is_singleton);
	CHECK_EQ(path, "res://plain.gd");
	CHECK_FALSE(is_singleton);

	ProjectSettings::parse_autoload_value("*res://singleton.gd", path, is_singleton);
	CHECK_EQ(path, "res://singleton.gd");
	CHECK(is_singleton);

	ResourceUID *resource_uid = ResourceUID::get_singleton();
	const ResourceUID::ID uid = resource_uid->create_id();
	ResourceUIDMappingCleanup uid_cleanup(resource_uid, uid);
	const String uid_text = resource_uid->id_to_text(uid);
	resource_uid->add_id(uid, "res://uid_target.gd");

	ProjectSettings::parse_autoload_value("*" + uid_text, path, is_singleton);
	CHECK_EQ(path, "res://uid_target.gd");
	CHECK(is_singleton);

	ProjectSettings::parse_autoload_value("res://fallback.gd::" + uid_text, path, is_singleton);
	CHECK_EQ(path, "res://uid_target.gd");
	CHECK_FALSE(is_singleton);

	ProjectSettings::parse_autoload_value("*res://fallback.gd::uid://<invalid>", path, is_singleton);
	CHECK_EQ(path, "res://fallback.gd");
	CHECK(is_singleton);
}

TEST_CASE("[ProjectSettings][Autoload] Stringifies path values and singleton markers") {
	CHECK_EQ(
			ProjectSettings::stringify_autoload_value("res://autoload_without_uid.gd", false),
			"res://autoload_without_uid.gd");
	CHECK_EQ(
			ProjectSettings::stringify_autoload_value("res://autoload_without_uid.gd", true),
			"*res://autoload_without_uid.gd");

	REQUIRE_FALSE(Engine::get_singleton()->is_editor_hint());
	ResourceUID *resource_uid = ResourceUID::get_singleton();
	const ResourceUID::ID uid = resource_uid->create_id();
	REQUIRE_NE(uid, ResourceUID::INVALID_ID);
	const String path = "res://autoload_with_uid.gd";
	const String uid_text = resource_uid->id_to_text(uid);
	resource_uid->enable_reverse_cache();
	{
		ResourceUIDMappingCleanup uid_cleanup(resource_uid, uid);
		resource_uid->add_id(uid, path);
		REQUIRE_EQ(resource_uid->get_path_id(path), uid);

		CHECK_EQ(ProjectSettings::stringify_autoload_value(path, false), path + "::" + uid_text);
		CHECK_EQ(ProjectSettings::stringify_autoload_value(path, true), "*" + path + "::" + uid_text);
	}
	CHECK_FALSE(resource_uid->has_id(uid));
	CHECK_EQ(resource_uid->get_path_id(path), ResourceUID::INVALID_ID);
}

TEST_CASE("[ProjectSettings][Autoload] Resolves delayed entries and preserves prepend order") {
	ProjectSettings *settings = ProjectSettings::get_singleton();
	const StringName normal_setting = "autoload/AutoloadPathUidNormal";
	const StringName prepended_setting = "autoload_prepend/AutoloadPathUidPrepended";
	ResourceUID *resource_uid = ResourceUID::get_singleton();
	const ResourceUID::ID uid = resource_uid->create_id();
	ResourceUIDMappingCleanup uid_cleanup(resource_uid, uid);
	ProjectSettingsAutoloadCleanup autoload_cleanup(settings, { normal_setting, prepended_setting });
	const String uid_text = resource_uid->id_to_text(uid);

	CHECK_FALSE(resource_uid->has_id(uid));
	settings->set_setting(normal_setting, "*" + uid_text);
	settings->set_setting(prepended_setting, "*res://prepended.gd");

	CHECK(settings->has_autoload("AutoloadPathUidNormal"));
	CHECK(settings->has_autoload("AutoloadPathUidPrepended"));

	resource_uid->add_id(uid, "res://normal.gd");

	CHECK_EQ(settings->get_autoload("AutoloadPathUidNormal").path, "res://normal.gd");

	int normal_position = -1;
	int prepended_position = -1;
	int position = 0;
	for (const KeyValue<StringName, ProjectSettings::AutoloadInfo> &E : settings->get_autoload_list()) {
		if (E.key == StringName("AutoloadPathUidNormal")) {
			normal_position = position;
		} else if (E.key == StringName("AutoloadPathUidPrepended")) {
			prepended_position = position;
		}
		position++;
	}

	CHECK_NE(normal_position, -1);
	CHECK_NE(prepended_position, -1);
	CHECK_LT(prepended_position, normal_position);
}

TEST_CASE("[ProjectSettings][Autoload] Resolves retained UID values after mapping changes") {
	ProjectSettings *settings = ProjectSettings::get_singleton();
	const StringName setting = "autoload/AutoloadPathUidRetarget";
	ResourceUID *resource_uid = ResourceUID::get_singleton();
	const ResourceUID::ID uid = resource_uid->create_id();
	REQUIRE_NE(uid, ResourceUID::INVALID_ID);
	ResourceUIDMappingCleanup uid_cleanup(resource_uid, uid);
	ProjectSettingsAutoloadCleanup autoload_cleanup(settings, { setting });
	const String uid_text = resource_uid->id_to_text(uid);

	resource_uid->add_id(uid, "res://uid_target_a.gd");
	settings->set_setting(setting, "*" + uid_text);
	CHECK_EQ(settings->get_autoload("AutoloadPathUidRetarget").path, "res://uid_target_a.gd");

	resource_uid->set_id(uid, "res://uid_target_b.gd");
	CHECK_EQ(settings->get_autoload("AutoloadPathUidRetarget").path, "res://uid_target_b.gd");
}

TEST_CASE("[ProjectSettings][Autoload] Returns independent snapshots") {
	using AutoloadMap = HashMap<StringName, ProjectSettings::AutoloadInfo>;

	ProjectSettings *settings = ProjectSettings::get_singleton();
	static_assert(std::is_same_v<decltype(settings->get_autoload_list()), AutoloadMap>);

	const StringName setting = "autoload/AutoloadPathUidSnapshot";
	const StringName name = "AutoloadPathUidSnapshot";
	ProjectSettingsAutoloadCleanup autoload_cleanup(settings, { setting });

	settings->set_setting(setting, "res://snapshot_a.gd");
	const AutoloadMap first_snapshot = settings->get_autoload_list();
	REQUIRE(first_snapshot.has(name));
	CHECK_EQ(first_snapshot[name].path, "res://snapshot_a.gd");

	settings->set_setting(setting, "res://snapshot_b.gd");
	const AutoloadMap second_snapshot = settings->get_autoload_list();
	REQUIRE(second_snapshot.has(name));
	CHECK_EQ(first_snapshot[name].path, "res://snapshot_a.gd");
	CHECK_EQ(second_snapshot[name].path, "res://snapshot_b.gd");
}

TEST_CASE("[ProjectSettings][Autoload] Preserves sequential normal and prepend ordering") {
	ProjectSettings *settings = ProjectSettings::get_singleton();
	const StringName name_a = "AutoloadPathUidOrderA";
	const StringName name_b = "AutoloadPathUidOrderB";
	const StringName name_c = "AutoloadPathUidOrderC";
	const StringName normal_a = "autoload/AutoloadPathUidOrderA";
	const StringName normal_b = "autoload/AutoloadPathUidOrderB";
	const StringName normal_c = "autoload/AutoloadPathUidOrderC";
	const StringName prepend_a = "autoload_prepend/AutoloadPathUidOrderA";
	const StringName prepend_b = "autoload_prepend/AutoloadPathUidOrderB";
	const StringName prepend_c = "autoload_prepend/AutoloadPathUidOrderC";
	ProjectSettingsAutoloadCleanup autoload_cleanup(settings, { normal_a, normal_b, normal_c, prepend_a, prepend_b, prepend_c });

	auto get_test_order = [settings, name_a, name_b, name_c]() {
		LocalVector<StringName> order;
		for (const KeyValue<StringName, ProjectSettings::AutoloadInfo> &E : settings->get_autoload_list()) {
			if (E.key == name_a || E.key == name_b || E.key == name_c) {
				order.push_back(E.key);
			}
		}
		return order;
	};

	settings->set_setting(normal_a, "res://a.gd");
	settings->set_setting(normal_b, "res://b.gd");
	settings->set_setting(prepend_b, "res://b_prepend.gd");
	settings->set_setting(prepend_a, "res://a_prepend.gd");

	LocalVector<StringName> order = get_test_order();
	REQUIRE_EQ(order.size(), 2);
	CHECK_EQ(order[0], name_a);
	CHECK_EQ(order[1], name_b);

	settings->set_setting(normal_a, "res://a_replaced.gd");
	settings->set_setting(normal_b, "res://b_replaced.gd");
	order = get_test_order();
	REQUIRE_EQ(order.size(), 2);
	CHECK_EQ(order[0], name_a);
	CHECK_EQ(order[1], name_b);

	settings->set_setting(normal_c, "res://c.gd");
	settings->set_setting(prepend_c, "res://c_prepend.gd");
	order = get_test_order();
	REQUIRE_EQ(order.size(), 3);
	CHECK_EQ(order[0], name_c);
	CHECK_EQ(order[1], name_a);
	CHECK_EQ(order[2], name_b);

	settings->set_setting(normal_c, "res://c_replaced.gd");
	order = get_test_order();
	REQUIRE_EQ(order.size(), 3);
	CHECK_EQ(order[0], name_c);
	CHECK_EQ(order[1], name_a);
	CHECK_EQ(order[2], name_b);
}

} // namespace TestProjectSettings
