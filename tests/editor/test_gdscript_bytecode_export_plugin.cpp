/**************************************************************************/
/*  test_gdscript_bytecode_export_plugin.cpp                              */
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

TEST_FORCE_LINK(test_gdscript_bytecode_export_plugin)

#include "modules/modules_enabled.gen.h"

#if defined(TOOLS_ENABLED) && defined(MODULE_GDSCRIPT_ENABLED)

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/file_access_pack.h"
#include "core/io/resource_importer.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/os/os.h"
#include "core/string/print_string.h"
#include "editor/export/editor_export.h"
#include "editor/export/editor_export_platform.h"
#include "editor/export/editor_export_plugin.h"
#include "editor/export/editor_export_preset.h"
#include "editor/file_system/editor_file_system.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/packed_scene.h"
#include "tests/test_utils.h"

#include "modules/gdscript/editor/gdscript_editor_export_plugin.h"
#include "modules/gdscript/gdscript.h"
#include "modules/gdscript/gdscript_bytecode_format.h"
#include "modules/gdscript/gdscript_bytecode_loader.h"
#include "modules/gdscript/gdscript_cache.h"
#include "modules/gdscript/gdscript_function.h"

namespace TestGDScriptBytecodeExportPlugin {

class BytecodeMockExportPlatform : public EditorExportPlatform {
	GDCLASS(BytecodeMockExportPlatform, EditorExportPlatform);

public:
	virtual void get_preset_features(const Ref<EditorExportPreset> &p_preset, List<String> *r_features) const override {
		if (r_features) {
			r_features->push_back("mock_platform");
		}
	}

	virtual void get_export_options(List<ExportOption> *r_options) const override {
		if (r_options) {
			r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "mock/enabled"), true));
		}
	}

	virtual String get_os_name() const override { return "MockOS"; }
	virtual String get_name() const override { return "MockPlatform"; }
	virtual Ref<Texture2D> get_logo() const override { return Ref<Texture2D>(); }

	virtual bool has_valid_export_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error, bool &r_missing_templates, bool p_debug = false) const override {
		r_missing_templates = false;
		return true;
	}

	virtual bool has_valid_project_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error) const override {
		return true;
	}

	virtual List<String> get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const override {
		List<String> extensions;
		extensions.push_back("mock");
		return extensions;
	}

	virtual Error export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, BitField<EditorExportPlatform::DebugFlags> p_flags = 0, bool p_notify = true) override {
		return export_pack(p_preset, p_debug, p_path, p_flags);
	}

	virtual void get_platform_features(List<String> *r_features) const override {
		if (r_features) {
			r_features->push_back("mock");
		}
	}
};

class EmbeddedScriptResourceCustomizerPlugin : public EditorExportPlugin {
	GDCLASS(EmbeddedScriptResourceCustomizerPlugin, EditorExportPlugin);

public:
	String target_resource_path;
	String target_scene_path;

	virtual String get_name() const override { return "EmbeddedScriptResourceCustomizerPlugin"; }
	virtual bool supports_platform(const Ref<EditorExportPlatform> &p_export_platform) const override { return true; }

protected:
	virtual bool _begin_customize_resources(const Ref<EditorExportPlatform> &p_platform, const Vector<String> &p_features) override {
		return !target_resource_path.is_empty();
	}

	virtual Ref<Resource> _customize_resource(const Ref<Resource> &p_resource, const String &p_path) override {
		if (!target_resource_path.is_empty() && p_path == target_resource_path && p_resource.is_valid()) {
			Ref<GDScript> embedded_script;
			embedded_script.instantiate();
			embedded_script->set_source_code("extends Resource\nvar injected_value: int = 7\n");
			p_resource->set_script(embedded_script);
			return p_resource;
		}
		return Ref<Resource>();
	}

	virtual bool _begin_customize_scenes(const Ref<EditorExportPlatform> &p_platform, const Vector<String> &p_features) override {
		return !target_scene_path.is_empty();
	}

	virtual Node *_customize_scene(Node *p_root, const String &p_path) override {
		if (!target_scene_path.is_empty() && p_path == target_scene_path && p_root != nullptr) {
			Ref<GDScript> embedded_script;
			embedded_script.instantiate();
			embedded_script->set_source_code("extends Node\nvar injected_node_value: int = 9\n");
			p_root->set_script(embedded_script);
			return p_root;
		}
		return nullptr;
	}

	virtual uint64_t _get_customization_configuration_hash() const override {
		return 0x51C36E01ULL;
	}
};

class InMemoryExtraFileInjectorPlugin : public EditorExportPlugin {
	GDCLASS(InMemoryExtraFileInjectorPlugin, EditorExportPlugin);

public:
	String trigger_path;
	String injected_path;
	Vector<uint8_t> injected_bytes;

	virtual String get_name() const override { return "A_InMemoryExtraFileInjectorPlugin"; }
	virtual bool supports_platform(const Ref<EditorExportPlatform> &p_export_platform) const override { return true; }

protected:
	virtual void _export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features) override {
		if (!trigger_path.is_empty() && p_path == trigger_path) {
			add_file(injected_path, injected_bytes, false);
		}
	}
};

class StubBytecodeResolver : public GDScriptBytecodeExternalResolver {
public:
	HashMap<String, Ref<Resource>> resources;
	HashMap<String, Ref<GDScript>> scripts;

	virtual Ref<Resource> resolve_resource(const String &p_path, const String &p_type_hint, Error &r_error) override {
		HashMap<String, Ref<Resource>>::ConstIterator entry = resources.find(p_path);
		if (!entry || entry->value.is_null()) {
			r_error = ERR_FILE_NOT_FOUND;
			return Ref<Resource>();
		}
		r_error = OK;
		return entry->value;
	}

	virtual Ref<GDScript> resolve_script(const String &p_path, const String &p_fully_qualified_name, bool &r_is_local_class, Error &r_error) override {
		r_is_local_class = false;
		HashMap<String, Ref<GDScript>>::ConstIterator entry = scripts.find(p_path);
		if (!entry || entry->value.is_null()) {
			r_error = ERR_FILE_NOT_FOUND;
			return Ref<GDScript>();
		}
		r_error = OK;
		return entry->value;
	}
};

struct ScopedProjectSetting {
	String setting_name;
	bool existed = false;
	Variant previous_value;

	ScopedProjectSetting(const String &p_name, const Variant &p_new_value) :
			setting_name(p_name) {
		ProjectSettings *settings = ProjectSettings::get_singleton();
		existed = settings->has_setting(setting_name);
		if (existed) {
			previous_value = settings->get_setting(setting_name);
		}
		settings->set_setting(setting_name, p_new_value);
	}

	~ScopedProjectSetting() {
		ProjectSettings *settings = ProjectSettings::get_singleton();
		if (existed) {
			settings->set_setting(setting_name, previous_value);
		} else if (settings->has_setting(setting_name)) {
			settings->clear(setting_name);
		}
	}
};

struct ScopedAutoload {
	StringName autoload_name;
	String setting_key;
	bool registered_named_global = false;

	ScopedAutoload(const StringName &p_name, const String &p_path, bool p_is_singleton) :
			autoload_name(p_name), setting_key("autoload/" + String(p_name)) {
		ProjectSettings::get_singleton()->set_setting(
				setting_key,
				ProjectSettings::stringify_autoload_value(p_path, p_is_singleton));

		if (p_is_singleton && !GDScriptLanguage::get_singleton()->get_named_globals_map().has(p_name)) {
			GDScriptLanguage::get_singleton()->add_named_global_constant(p_name, Variant());
			registered_named_global = true;
		}
	}

	~ScopedAutoload() {
		if (registered_named_global && GDScriptLanguage::get_singleton()->get_named_globals_map().has(autoload_name)) {
			GDScriptLanguage::get_singleton()->remove_named_global_constant(autoload_name);
		}
		if (ProjectSettings::get_singleton()->has_setting(setting_key)) {
			ProjectSettings::get_singleton()->clear(setting_key);
		}
	}
};

struct ScopedNamedGlobal {
	StringName global_name;

	ScopedNamedGlobal(const StringName &p_name, const Variant &p_value) :
			global_name(p_name) {
		GDScriptLanguage::get_singleton()->add_named_global_constant(global_name, p_value);
	}

	~ScopedNamedGlobal() {
		if (GDScriptLanguage::get_singleton()->get_named_globals_map().has(global_name)) {
			GDScriptLanguage::get_singleton()->remove_named_global_constant(global_name);
		}
	}
};

static void ensure_gdscript_language_initialized() {
	if (!GDScriptLanguage::get_singleton()->get_global_map().has(SNAME("Node"))) {
		GDScriptLanguage::get_singleton()->init();
	}
}

static Vector<uint8_t> utf8_bytes(const String &p_text) {
	CharString utf8 = p_text.utf8();
	Vector<uint8_t> bytes;
	bytes.resize(utf8.length());
	if (utf8.length() > 0) {
		memcpy(bytes.ptrw(), utf8.get_data(), utf8.length());
	}
	return bytes;
}

static bool buffer_contains_ascii(const Vector<uint8_t> &p_buffer, const char *p_needle) {
	const size_t needle_length = strlen(p_needle);
	if (needle_length == 0 || static_cast<size_t>(p_buffer.size()) < needle_length) {
		return false;
	}
	const uint8_t *haystack = p_buffer.ptr();
	const size_t limit = static_cast<size_t>(p_buffer.size()) - needle_length;
	for (size_t offset = 0; offset <= limit; offset++) {
		if (memcmp(haystack + offset, p_needle, needle_length) == 0) {
			return true;
		}
	}
	return false;
}

static HashMap<String, Vector<uint8_t>> read_pck_entries(const String &p_pck_path) {
	HashMap<String, Vector<uint8_t>> entries;
	Ref<FileAccess> file = FileAccess::open(p_pck_path, FileAccess::READ);
	REQUIRE(file.is_valid());

	const uint32_t magic = file->get_32();
	REQUIRE(magic == PACK_HEADER_MAGIC);

	const uint32_t format_version = file->get_32();
	REQUIRE((format_version == PACK_FORMAT_VERSION_V2 || format_version == PACK_FORMAT_VERSION_V3 || format_version == PACK_FORMAT_VERSION_V4));
	file->get_32(); // major
	file->get_32(); // minor
	file->get_32(); // patch

	const uint32_t pack_flags = file->get_32();
	REQUIRE((pack_flags & PACK_DIR_ENCRYPTED) == 0);
	const uint64_t file_base = file->get_64();

	if (format_version == PACK_FORMAT_VERSION_V3 || format_version == PACK_FORMAT_VERSION_V4) {
		const uint64_t directory_offset = file->get_64();
		file->seek(directory_offset);
	} else {
		for (int reserved_index = 0; reserved_index < 16; reserved_index++) {
			file->get_32();
		}
	}

	const uint32_t file_count = file->get_32();
	struct EntryMeta {
		String path;
		uint64_t offset = 0;
		uint64_t size = 0;
	};
	Vector<EntryMeta> entry_metas;
	entry_metas.reserve(file_count);

	for (uint32_t index = 0; index < file_count; index++) {
		const uint32_t path_length = file->get_32();
		CharString path_utf8;
		path_utf8.resize_uninitialized(path_length + 1);
		file->get_buffer(reinterpret_cast<uint8_t *>(path_utf8.ptrw()), path_length);
		path_utf8[path_length] = '\0';

		EntryMeta meta;
		meta.path = String::utf8(path_utf8.ptr(), path_length).strip_edges();
		while (meta.path.length() > 0 && meta.path[meta.path.length() - 1] == '\0') {
			meta.path = meta.path.substr(0, meta.path.length() - 1);
		}
		if (!meta.path.begins_with("res://")) {
			meta.path = "res://" + meta.path;
		}
		meta.offset = file_base + file->get_64();
		meta.size = file->get_64();
		uint8_t md5[16] = {};
		file->get_buffer(md5, 16);
		const uint32_t flags = file->get_32();
		if ((flags & PACK_FILE_REMOVAL) == 0) {
			entry_metas.push_back(meta);
		}
	}

	for (const EntryMeta &meta : entry_metas) {
		file->seek(meta.offset);
		Vector<uint8_t> data;
		data.resize(meta.size);
		if (meta.size > 0) {
			const uint64_t read_bytes = file->get_buffer(data.ptrw(), meta.size);
			REQUIRE(read_bytes == meta.size);
		}
		entries[meta.path] = data;
	}

	return entries;
}

#ifdef DEBUG_ENABLED
static void capture_disassembly_line(void *p_userdata, const String &p_string, bool p_error, bool p_rich) {
	if (!p_error) {
		Vector<String> *captured_lines = static_cast<Vector<String> *>(p_userdata);
		captured_lines->push_back(p_string);
	}
}

static Vector<String> disassemble_function(const GDScriptFunction *p_function, const String &p_source_code) {
	REQUIRE(p_function != nullptr);
	Vector<String> lines;
	PrintHandlerList handler;
	handler.printfunc = capture_disassembly_line;
	handler.userdata = &lines;
	add_print_handler(&handler);
	p_function->disassemble(p_source_code.split("\n"));
	remove_print_handler(&handler);
	return lines;
}

static bool disassembly_contains_substring(const Vector<String> &p_lines, const String &p_needle) {
	for (const String &line : p_lines) {
		if (line.contains(p_needle)) {
			return true;
		}
	}
	return false;
}
#endif // DEBUG_ENABLED

struct BytecodeExportTestEnvironment {
	String previous_resource_path;
	ResourceLoaderImport previous_import_callback = nullptr;
	ResourceFormatImporterLoadOnStartup previous_load_on_startup = nullptr;

	String base_dir;
	String project_dir;
	String output_dir;
	EditorExport *editor_export = nullptr;
	EditorFileSystem *editor_file_system = nullptr;
	Ref<BytecodeMockExportPlatform> platform;
	Ref<EditorExportGDScript> gdscript_plugin;

	explicit BytecodeExportTestEnvironment(const String &p_name) {
		ensure_gdscript_language_initialized();

		previous_resource_path = TestProjectSettingsInternalsAccessor::resource_path();
		previous_import_callback = ResourceLoader::import;
		previous_load_on_startup = ResourceImporter::load_on_startup;

		base_dir = TestUtils::get_temp_path("gdscript_bytecode_export_" + p_name);
		if (DirAccess::exists(base_dir)) {
			Ref<DirAccess> existing_dir = DirAccess::open(base_dir);
			if (existing_dir.is_valid()) {
				existing_dir->erase_contents_recursive();
			}
		}

		project_dir = base_dir.path_join("project");
		output_dir = base_dir.path_join("output");
		DirAccess::make_dir_recursive_absolute(project_dir);
		DirAccess::make_dir_recursive_absolute(project_dir.path_join(".godot"));
		DirAccess::make_dir_recursive_absolute(output_dir);

		write_project_text_file("project.godot", "config_version=5\n\n[application]\nconfig/name=\"BytecodeExportPluginTest\"\n");

		TestProjectSettingsInternalsAccessor::resource_path() = project_dir;

		editor_file_system = memnew(EditorFileSystem);
		SceneTree::get_singleton()->get_root()->add_child(editor_file_system);

		editor_export = memnew(EditorExport);
		SceneTree::get_singleton()->get_root()->add_child(editor_export);

		platform.instantiate();
		editor_export->add_export_platform(platform);

		gdscript_plugin.instantiate();
		editor_export->add_export_plugin(gdscript_plugin);
	}

	~BytecodeExportTestEnvironment() {
		if (editor_export) {
			if (gdscript_plugin.is_valid()) {
				editor_export->remove_export_plugin(gdscript_plugin);
			}
			SceneTree::get_singleton()->get_root()->remove_child(editor_export);
			memdelete(editor_export);
			editor_export = nullptr;
		}
		if (editor_file_system) {
			SceneTree::get_singleton()->get_root()->remove_child(editor_file_system);
			memdelete(editor_file_system);
			editor_file_system = nullptr;
		}
		ResourceLoader::import = previous_import_callback;
		ResourceImporter::load_on_startup = previous_load_on_startup;
		TestProjectSettingsInternalsAccessor::resource_path() = previous_resource_path;
	}

	void write_project_text_file(const String &p_relative_path, const String &p_contents) const {
		const String absolute_path = project_dir.path_join(p_relative_path);
		DirAccess::make_dir_recursive_absolute(absolute_path.get_base_dir());
		Ref<FileAccess> file = FileAccess::open(absolute_path, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string(p_contents);
	}

	Ref<EditorExportPreset> create_preset(const Vector<String> &p_files, EditorExportPreset::ScriptExportMode p_script_mode) {
		Ref<EditorExportPreset> preset = platform->create_preset();
		preset->set_name("BytecodeTestPreset");
		preset->set_script_export_mode(p_script_mode);
		preset->set_export_filter(EditorExportPreset::EXPORT_SELECTED_RESOURCES);
		for (const String &file_path : p_files) {
			preset->add_export_file(file_path);
		}
		return preset;
	}

	Vector<String> list_output_files() const {
		Vector<String> entries;
		Ref<DirAccess> dir = DirAccess::open(output_dir);
		if (dir.is_null()) {
			return entries;
		}
		dir->list_dir_begin();
		String entry = dir->get_next();
		while (!entry.is_empty()) {
			if (entry != "." && entry != "..") {
				entries.push_back(entry);
			}
			entry = dir->get_next();
		}
		dir->list_dir_end();
		return entries;
	}

	String collect_platform_error_messages() const {
		String combined;
		for (int index = 0; index < platform->get_message_count(); index++) {
			EditorExportPlatform::ExportMessage message = platform->get_message(index);
			if (message.msg_type == EditorExportPlatform::EXPORT_MESSAGE_ERROR) {
				if (!combined.is_empty()) {
					combined += "\n";
				}
				combined += message.text;
			}
		}
		return combined;
	}
};

static Vector<uint8_t> build_binary_resource_bytes(const String &p_temp_dir, const String &p_filename, bool p_with_embedded_gdscript, uint32_t p_save_flags) {
	Ref<Resource> resource;
	resource.instantiate();
	resource->set_name(p_with_embedded_gdscript ? "binary_with_embedded_script" : "binary_clean_resource");
	if (p_with_embedded_gdscript) {
		Ref<GDScript> embedded_script;
		embedded_script.instantiate();
		embedded_script->set_source_code("extends Resource\nvar binary_embedded: int = 123\n");
		resource->set_script(embedded_script);
	}

	const String temp_file_path = p_temp_dir.path_join(p_filename);
	DirAccess::make_dir_recursive_absolute(temp_file_path.get_base_dir());
	const Error save_error = ResourceSaver::save(resource, temp_file_path, p_save_flags);
	REQUIRE(save_error == OK);

	Vector<uint8_t> bytes = FileAccess::get_file_as_bytes(temp_file_path);
	REQUIRE(bytes.size() >= 4);
	DirAccess::remove_absolute(temp_file_path);
	return bytes;
}

TEST_CASE("[Editor][Export][GDScript] Compiled bytecode PCK export covers language features, debug/release profiles, and cache isolation") {
	BytecodeExportTestEnvironment env("e2e_features");
	ScopedProjectSetting call_stack_setting("debug/settings/gdscript/always_track_call_stacks", false);
	ScopedProjectSetting binary_convert_setting("editor/export/convert_text_resources_to_binary", false);

	env.write_project_text_file("autoload_singleton.gd",
			"extends Node\n"
			"var bonus: int = 5\n"
			"func get_bonus() -> int:\n"
			"\treturn bonus\n");

	env.write_project_text_file("helper.gd",
			"extends RefCounted\n"
			"func compute(value: int) -> int:\n"
			"\treturn value * 2\n");

	env.write_project_text_file("static_unload_script.gd",
			"@static_unload\n"
			"extends RefCounted\n"
			"static var cached_total: int = 42\n"
			"static func _static_init() -> void:\n"
			"\tcached_total += 1\n");

	env.write_project_text_file("data.tres",
			"[gd_resource type=\"Resource\" format=3]\n"
			"[resource]\n"
			"resource_name = \"PreloadedData\"\n");

	const String main_feature_source =
			"# UNIQUE_SOURCE_COMMENT_SENTINEL_98765\n"
			"extends Node\n"
			"\n"
			"signal value_changed(new_value: int)\n"
			"\n"
			"@export var speed: float = 10.0\n"
			"@onready var child_node: Node = get_node_or_null(\"Child\")\n"
			"\n"
			"static var shared_count: int = 0\n"
			"\n"
			"const HELPER_SCRIPT = preload(\"res://helper.gd\")\n"
			"const PRELOADED_DATA = preload(\"res://data.tres\")\n"
			"\n"
			"class InnerCalculator:\n"
			"\tvar factor: int = 3\n"
			"\tfunc scale(input_value: int) -> int:\n"
			"\t\treturn input_value * factor\n"
			"\n"
			"var typed_numbers: Array[int] = [1, 2, 3]\n"
			"var typed_lookup: Dictionary[String, int] = {\"alpha\": 10}\n"
			"\n"
			"static func _static_init() -> void:\n"
			"\tshared_count = 1\n"
			"\n"
			"@rpc(\"any_peer\", \"call_local\")\n"
			"func sync_state(delta: int) -> int:\n"
			"\tassert(delta >= 0, \"UNIQUE_ASSERT_MESSAGE_SENTINEL_54321\")\n"
			"\tvar calc := InnerCalculator.new()\n"
			"\tvar helper := HELPER_SCRIPT.new()\n"
			"\tvar adder := func(offset: int) -> int:\n"
			"\t\treturn int(speed) + offset + calc.scale(delta)\n"
			"\tvar autoload_bonus: int = ExportTestAutoload.get_bonus()\n"
			"\treturn helper.compute(adder.call(typed_numbers[0])) + typed_lookup[\"alpha\"] + autoload_bonus\n"
			"\n"
			"func await_change() -> int:\n"
			"\tvar received: int = await value_changed\n"
			"\treturn received\n";
	env.write_project_text_file("main_feature.gd", main_feature_source);

	env.write_project_text_file("main_scene.tscn",
			"[gd_scene load_steps=2 format=3]\n"
			"[ext_resource type=\"Script\" path=\"res://main_feature.gd\" id=\"1_ext\"]\n"
			"[node name=\"Main\" type=\"Node\"]\n"
			"script = ExtResource(\"1_ext\")\n");

	ScopedAutoload autoload(SNAME("ExportTestAutoload"), "res://autoload_singleton.gd", true);

	Vector<String> selected_files;
	selected_files.push_back("res://autoload_singleton.gd");
	selected_files.push_back("res://helper.gd");
	selected_files.push_back("res://static_unload_script.gd");
	selected_files.push_back("res://data.tres");
	selected_files.push_back("res://main_feature.gd");
	selected_files.push_back("res://main_scene.tscn");

	// Confirm none of our test scripts or resources exist in live caches before export.
	const Vector<String> script_paths = {
		"res://autoload_singleton.gd",
		"res://helper.gd",
		"res://static_unload_script.gd",
		"res://main_feature.gd",
	};
	for (const String &script_path : script_paths) {
		REQUIRE(GDScriptCache::get_cached_script(script_path).is_null());
		REQUIRE_FALSE(GDScriptCache::has_parser(script_path));
		REQUIRE_FALSE(ResourceCache::has(script_path));
	}
	REQUIRE_FALSE(ResourceCache::has("res://data.tres"));

	Ref<EditorExportPreset> preset = env.create_preset(selected_files, EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);

	const String release_pck_path = env.output_dir.path_join("release_bytecode.pck");
	const String debug_pck_path = env.output_dir.path_join("debug_bytecode.pck");

	const Error release_err = env.platform->export_pack(preset, false, release_pck_path);
	REQUIRE(release_err == OK);
	REQUIRE(FileAccess::exists(release_pck_path));
	CHECK_FALSE(GDScriptCache::has_active_export_session());

	const Error debug_err = env.platform->export_pack(preset, true, debug_pck_path);
	REQUIRE(debug_err == OK);
	REQUIRE(FileAccess::exists(debug_pck_path));
	CHECK_FALSE(GDScriptCache::has_active_export_session());

	// Live editor caches must remain unpolluted after both exports.
	for (const String &script_path : script_paths) {
		CHECK(GDScriptCache::get_cached_script(script_path).is_null());
		CHECK_FALSE(GDScriptCache::has_parser(script_path));
		CHECK_FALSE(ResourceCache::has(script_path));
	}
	CHECK_FALSE(ResourceCache::has("res://data.tres"));

	HashMap<String, Vector<uint8_t>> release_entries = read_pck_entries(release_pck_path);
	HashMap<String, Vector<uint8_t>> debug_entries = read_pck_entries(debug_pck_path);

	// Verify zero .gd or .gdc files shipped in either PCK.
	for (const KeyValue<String, Vector<uint8_t>> &entry : release_entries) {
		CHECK_FALSE(entry.key.ends_with(".gd"));
		CHECK_FALSE(entry.key.ends_with(".gdc"));
	}
	for (const KeyValue<String, Vector<uint8_t>> &entry : debug_entries) {
		CHECK_FALSE(entry.key.ends_with(".gd"));
		CHECK_FALSE(entry.key.ends_with(".gdc"));
	}

	// Verify every script produced a valid .gdb + .gd.remap in both release and debug PCKs.
	for (const String &script_path : script_paths) {
		const String gdb_path = script_path.get_basename() + ".gdb";
		const String remap_path = script_path + ".remap";

		REQUIRE(release_entries.has(gdb_path));
		REQUIRE(release_entries.has(remap_path));
		REQUIRE(debug_entries.has(gdb_path));
		REQUIRE(debug_entries.has(remap_path));

		const String remap_text = String::utf8(reinterpret_cast<const char *>(release_entries[remap_path].ptr()), release_entries[remap_path].size());
		CHECK(remap_text.contains(vformat("path=\"%s\"", gdb_path)));

		GDScriptBytecodeLoader::HeaderInfo release_header;
		String header_error;
		CHECK(GDScriptBytecodeLoader::check_header(release_entries[gdb_path], &release_header, &header_error) == OK);

		Ref<GDScript> release_skeleton;
		release_skeleton.instantiate();
		String skeleton_error;
		CHECK(GDScriptBytecodeLoader::load_skeleton(release_entries[gdb_path], release_skeleton, script_path, nullptr, &skeleton_error) == OK);

		GDScriptBytecodeLoader::HeaderInfo debug_header;
		CHECK(GDScriptBytecodeLoader::check_header(debug_entries[gdb_path], &debug_header, &header_error) == OK);

		Ref<GDScript> debug_skeleton;
		debug_skeleton.instantiate();
		CHECK(GDScriptBytecodeLoader::load_skeleton(debug_entries[gdb_path], debug_skeleton, script_path, nullptr, &skeleton_error) == OK);

		if (script_path == "res://static_unload_script.gd") {
			CHECK((release_header.script_flags & GDScriptBytecodeFormat::SCRIPT_FLAG_ANNOTATED_STATIC_UNLOAD) != 0);
			CHECK((release_header.script_flags & GDScriptBytecodeFormat::SCRIPT_FLAG_HAS_STATIC_DATA) != 0);
		}
	}

	// Release .gdb must omit source comments and assert message strings.
	const Vector<uint8_t> &release_main_gdb = release_entries["res://main_feature.gdb"];
	const Vector<uint8_t> &debug_main_gdb = debug_entries["res://main_feature.gdb"];
	CHECK_FALSE(buffer_contains_ascii(release_main_gdb, "UNIQUE_SOURCE_COMMENT_SENTINEL_98765"));
	CHECK_FALSE(buffer_contains_ascii(release_main_gdb, "UNIQUE_ASSERT_MESSAGE_SENTINEL_54321"));
	CHECK_FALSE(buffer_contains_ascii(debug_main_gdb, "UNIQUE_SOURCE_COMMENT_SENTINEL_98765"));

#ifdef DEBUG_ENABLED
	// Load release and debug helper/main bytecode fully to verify OPCODE_LINE stripping vs retention.
	// Register runtime global for ExportTestAutoload so load_full() can rebake OPCODE_STORE_GLOBAL.
	GDScriptLanguage::get_singleton()->add_global_constant(SNAME("ExportTestAutoload"), Variant());

	auto load_full_main = [&](const HashMap<String, Vector<uint8_t>> &p_entries) -> Ref<GDScript> {
		StubBytecodeResolver resolver;

		Ref<GDScript> helper_script;
		helper_script.instantiate();
		String helper_error;
		REQUIRE(GDScriptBytecodeLoader::load_full(p_entries["res://helper.gdb"], helper_script, "res://helper.gd", &resolver, nullptr, &helper_error) == OK);
		resolver.scripts["res://helper.gd"] = helper_script;

		Ref<Resource> preloaded_data;
		preloaded_data.instantiate();
		preloaded_data->set_name("PreloadedData");
		resolver.resources["res://data.tres"] = preloaded_data;

		Ref<GDScript> main_script;
		main_script.instantiate();
		String main_error;
		REQUIRE(GDScriptBytecodeLoader::load_full(p_entries["res://main_feature.gdb"], main_script, "res://main_feature.gd", &resolver, nullptr, &main_error) == OK);
		return main_script;
	};

	Ref<GDScript> release_main_script = load_full_main(release_entries);
	Ref<GDScript> debug_main_script = load_full_main(debug_entries);

	const HashMap<StringName, GDScriptFunction *> &release_functions = release_main_script->get_member_functions();
	const HashMap<StringName, GDScriptFunction *> &debug_functions = debug_main_script->get_member_functions();
	REQUIRE(release_functions.has(SNAME("sync_state")));
	REQUIRE(debug_functions.has(SNAME("sync_state")));

	const Vector<String> release_disassembly = disassemble_function(release_functions[SNAME("sync_state")], main_feature_source);
	const Vector<String> debug_disassembly = disassemble_function(debug_functions[SNAME("sync_state")], main_feature_source);
	CHECK_FALSE(disassembly_contains_substring(release_disassembly, "line "));
	CHECK_FALSE(disassembly_contains_substring(release_disassembly, "assert ("));
	CHECK(disassembly_contains_substring(debug_disassembly, "line "));
	CHECK(disassembly_contains_substring(debug_disassembly, "assert ("));

	release_main_script->clear();
	debug_main_script->clear();
#endif // DEBUG_ENABLED

	// Also confirm a scene referencing an external .gd script passes binary conversion cleanly.
	{
		ScopedProjectSetting enable_binary_convert("editor/export/convert_text_resources_to_binary", true);
		env.write_project_text_file("simple_external.gd", "extends Node\nfunc ping() -> int:\n\treturn 1\n");
		env.write_project_text_file("simple_external_scene.tscn",
				"[gd_scene load_steps=2 format=3]\n"
				"[ext_resource type=\"Script\" path=\"res://simple_external.gd\" id=\"1_ext\"]\n"
				"[node name=\"Root\" type=\"Node\"]\n"
				"script = ExtResource(\"1_ext\")\n");
		Vector<String> binary_scene_files;
		binary_scene_files.push_back("res://simple_external.gd");
		binary_scene_files.push_back("res://simple_external_scene.tscn");
		Ref<EditorExportPreset> binary_scene_preset = env.create_preset(binary_scene_files, EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);

		const String binary_scene_pck_path = env.output_dir.path_join("binary_scene_ok.pck");
		CHECK(env.platform->export_pack(binary_scene_preset, false, binary_scene_pck_path) == OK);
		CHECK(FileAccess::exists(binary_scene_pck_path));

		Ref<GDScript> cached_simple = GDScriptCache::get_cached_script("res://simple_external.gd");
		GDScriptCache::remove_script("res://simple_external.gd");
		if (cached_simple.is_valid()) {
			cached_simple->clear();
		}
	}
}

TEST_CASE("[Editor][Export][GDScript] Existing text and binary-token script export modes are preserved") {
	BytecodeExportTestEnvironment env("legacy_modes");
	const String script_source = "extends Node\nfunc greet() -> String:\n\treturn \"hi\"\n";
	env.write_project_text_file("greet.gd", script_source);

	Vector<String> selected_files;
	selected_files.push_back("res://greet.gd");

	SUBCASE("MODE_SCRIPT_TEXT exports raw .gd source without .gdc or .gdb") {
		Ref<EditorExportPreset> preset = env.create_preset(selected_files, EditorExportPreset::MODE_SCRIPT_TEXT);
		const String pck_path = env.output_dir.path_join("text_mode.pck");
		REQUIRE(env.platform->export_pack(preset, false, pck_path) == OK);

		HashMap<String, Vector<uint8_t>> entries = read_pck_entries(pck_path);
		REQUIRE(entries.has("res://greet.gd"));
		CHECK_FALSE(entries.has("res://greet.gdc"));
		CHECK_FALSE(entries.has("res://greet.gdb"));
		CHECK(entries["res://greet.gd"] == utf8_bytes(script_source));
	}

	SUBCASE("MODE_SCRIPT_BINARY_TOKENS exports uncompressed .gdc and .gd.remap") {
		Ref<EditorExportPreset> preset = env.create_preset(selected_files, EditorExportPreset::MODE_SCRIPT_BINARY_TOKENS);
		const String pck_path = env.output_dir.path_join("binary_tokens.pck");
		REQUIRE(env.platform->export_pack(preset, false, pck_path) == OK);

		HashMap<String, Vector<uint8_t>> entries = read_pck_entries(pck_path);
		REQUIRE(entries.has("res://greet.gdc"));
		REQUIRE(entries.has("res://greet.gd.remap"));
		CHECK_FALSE(entries.has("res://greet.gd"));
		CHECK_FALSE(entries.has("res://greet.gdb"));
	}

	SUBCASE("MODE_SCRIPT_BINARY_TOKENS_COMPRESSED exports compressed .gdc and .gd.remap") {
		Ref<EditorExportPreset> preset = env.create_preset(selected_files, EditorExportPreset::MODE_SCRIPT_BINARY_TOKENS_COMPRESSED);
		const String pck_path = env.output_dir.path_join("binary_tokens_compressed.pck");
		REQUIRE(env.platform->export_pack(preset, false, pck_path) == OK);

		HashMap<String, Vector<uint8_t>> entries = read_pck_entries(pck_path);
		REQUIRE(entries.has("res://greet.gdc"));
		REQUIRE(entries.has("res://greet.gd.remap"));
		CHECK_FALSE(entries.has("res://greet.gd"));
		CHECK_FALSE(entries.has("res://greet.gdb"));
	}
}

TEST_CASE("[Editor][Export][GDScript] Fatal export failures abort cleanly across all 5 bytecode failure modes") {
	SUBCASE("1. Script with syntax/compile error fails export with zero partial PCK") {
		BytecodeExportTestEnvironment env("fail_compile_error");
		env.write_project_text_file("broken.gd", "extends Node\nfunc broken(:\n\tpass\n");

		Vector<String> selected_files;
		selected_files.push_back("res://broken.gd");
		Ref<EditorExportPreset> preset = env.create_preset(selected_files, EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);

		const String pck_path = env.output_dir.path_join("broken.pck");
		ERR_PRINT_OFF;
		const Error err = env.platform->export_pack(preset, false, pck_path);
		ERR_PRINT_ON;

		CHECK(err != OK);
		CHECK_FALSE(FileAccess::exists(pck_path));
		CHECK(env.list_output_files().is_empty());
		CHECK_FALSE(GDScriptCache::has_active_export_session());
		const String error_messages = env.collect_platform_error_messages();
		CHECK(error_messages.contains("res://broken.gd"));
	}

	SUBCASE("2. Script referencing unsupported editor-only named global fails export") {
		BytecodeExportTestEnvironment env("fail_named_global");
		// Bind an editor-only named global to a valid GDScript meta-type on disk so the compiler
		// emits OPCODE_STORE_NAMED_GLOBAL instead of folding a primitive literal constant.
		env.write_project_text_file("editor_meta_target.gd", "extends RefCounted\n");
		Ref<GDScript> editor_only_meta_script;
		editor_only_meta_script.instantiate();
		editor_only_meta_script->set_path_cache("res://editor_meta_target.gd");
		REQUIRE(editor_only_meta_script->load_source_code("res://editor_meta_target.gd") == OK);
		REQUIRE(editor_only_meta_script->reload() == OK);

		ScopedNamedGlobal editor_only_global(SNAME("TestEditorOnlyNamedGlobal"), Variant(editor_only_meta_script));
		env.write_project_text_file("editor_global_user.gd",
				"extends Node\n"
				"func read_editor_global() -> Variant:\n"
				"\treturn TestEditorOnlyNamedGlobal\n");

		Vector<String> selected_files;
		selected_files.push_back("res://editor_global_user.gd");
		Ref<EditorExportPreset> preset = env.create_preset(selected_files, EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);

		const String pck_path = env.output_dir.path_join("editor_global.pck");
		ERR_PRINT_OFF;
		const Error err = env.platform->export_pack(preset, false, pck_path);
		ERR_PRINT_ON;

		CHECK(err == ERR_INVALID_DATA);
		CHECK_FALSE(FileAccess::exists(pck_path));
		CHECK(env.list_output_files().is_empty());
		CHECK_FALSE(GDScriptCache::has_active_export_session());
		const String error_messages = env.collect_platform_error_messages();
		CHECK(error_messages.contains("res://editor_global_user.gd"));
		CHECK(error_messages.contains("TestEditorOnlyNamedGlobal"));

		GDScriptCache::remove_script("res://editor_meta_target.gd");
		editor_only_meta_script->clear();
	}

	SUBCASE("3a. Raw text .tscn with embedded GDScript fails export when exported as text") {
		BytecodeExportTestEnvironment env("fail_raw_tscn_text");
		ScopedProjectSetting binary_convert_setting("editor/export/convert_text_resources_to_binary", false);

		env.write_project_text_file("embedded_scene.tscn",
				"[gd_scene load_steps=2 format=3]\n"
				"[sub_resource type=\"GDScript\" id=\"GDScript_1\"]\n"
				"script/source = \"extends Node\\nfunc hello():\\n\\tpass\\n\"\n"
				"[node name=\"Root\" type=\"Node\"]\n"
				"script = SubResource(\"GDScript_1\")\n");

		Vector<String> selected_files;
		selected_files.push_back("res://embedded_scene.tscn");
		Ref<EditorExportPreset> preset = env.create_preset(selected_files, EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);

		const String pck_path = env.output_dir.path_join("embedded_text.pck");
		ERR_PRINT_OFF;
		const Error err = env.platform->export_pack(preset, false, pck_path);
		ERR_PRINT_ON;

		CHECK(err == ERR_INVALID_DATA);
		CHECK_FALSE(FileAccess::exists(pck_path));
		CHECK(env.list_output_files().is_empty());
		const String error_messages = env.collect_platform_error_messages();
		CHECK(error_messages.contains("res://embedded_scene.tscn"));
		CHECK(error_messages.contains("built-in GDScript"));
	}

	SUBCASE("3b. Raw .tscn with embedded GDScript fails export when converted to binary .scn") {
		BytecodeExportTestEnvironment env("fail_raw_tscn_binary");
		ScopedProjectSetting binary_convert_setting("editor/export/convert_text_resources_to_binary", true);

		env.write_project_text_file("embedded_scene_bin.tscn",
				"[gd_scene load_steps=2 format=3]\n"
				"[sub_resource type=\"GDScript\" id=\"GDScript_1\"]\n"
				"script/source = \"extends Node\\nfunc hello():\\n\\tpass\\n\"\n"
				"[node name=\"Root\" type=\"Node\"]\n"
				"script = SubResource(\"GDScript_1\")\n");

		Vector<String> selected_files;
		selected_files.push_back("res://embedded_scene_bin.tscn");
		Ref<EditorExportPreset> preset = env.create_preset(selected_files, EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);

		const String pck_path = env.output_dir.path_join("embedded_binary.pck");
		ERR_PRINT_OFF;
		const Error err = env.platform->export_pack(preset, false, pck_path);
		ERR_PRINT_ON;

		CHECK(err == ERR_INVALID_DATA);
		CHECK_FALSE(FileAccess::exists(pck_path));
		CHECK(env.list_output_files().is_empty());
		const String error_messages = env.collect_platform_error_messages();
		CHECK(error_messages.contains("res://embedded_scene_bin.tscn"));
		CHECK(error_messages.contains("built-in GDScript"));
	}

	SUBCASE("3c. On-disk zstd-compressed RSCC binary resource with embedded GDScript fails export") {
		BytecodeExportTestEnvironment env("fail_raw_rscc");
		const Vector<uint8_t> compressed_bytes = build_binary_resource_bytes(env.base_dir.path_join("scratch"), "compressed_embedded.res", true, ResourceSaver::FLAG_COMPRESS);
		REQUIRE(compressed_bytes.size() >= 4);
		REQUIRE(compressed_bytes[0] == 'R');
		REQUIRE(compressed_bytes[1] == 'S');
		REQUIRE(compressed_bytes[2] == 'C');
		REQUIRE(compressed_bytes[3] == 'C');

		const String target_disk_path = env.project_dir.path_join("compressed_embedded.res");
		Ref<FileAccess> out_file = FileAccess::open(target_disk_path, FileAccess::WRITE);
		REQUIRE(out_file.is_valid());
		out_file->store_buffer(compressed_bytes.ptr(), compressed_bytes.size());
		out_file->close();

		Vector<String> selected_files;
		selected_files.push_back("res://compressed_embedded.res");
		Ref<EditorExportPreset> preset = env.create_preset(selected_files, EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);

		const String pck_path = env.output_dir.path_join("compressed_embedded.pck");
		ERR_PRINT_OFF;
		const Error err = env.platform->export_pack(preset, false, pck_path);
		ERR_PRINT_ON;

		CHECK(err == ERR_INVALID_DATA);
		CHECK_FALSE(FileAccess::exists(pck_path));
		CHECK(env.list_output_files().is_empty());
		const String error_messages = env.collect_platform_error_messages();
		CHECK(error_messages.contains("res://compressed_embedded.res"));
		CHECK(error_messages.contains("built-in GDScript"));
	}

	SUBCASE("4a. Resource-customizer plugin injecting an embedded GDScript into a .tres fails export") {
		BytecodeExportTestEnvironment env("fail_customizer_tres");
		env.write_project_text_file("clean_data.tres",
				"[gd_resource type=\"Resource\" format=3]\n"
				"[resource]\n"
				"resource_name = \"CleanData\"\n");

		Ref<EmbeddedScriptResourceCustomizerPlugin> customizer_plugin;
		customizer_plugin.instantiate();
		customizer_plugin->target_resource_path = "res://clean_data.tres";
		env.editor_export->add_export_plugin(customizer_plugin);

		Vector<String> selected_files;
		selected_files.push_back("res://clean_data.tres");
		Ref<EditorExportPreset> preset = env.create_preset(selected_files, EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);

		const String pck_path = env.output_dir.path_join("customized_tres.pck");
		ERR_PRINT_OFF;
		const Error err = env.platform->export_pack(preset, false, pck_path);
		ERR_PRINT_ON;

		env.editor_export->remove_export_plugin(customizer_plugin);

		CHECK(err == ERR_INVALID_DATA);
		CHECK_FALSE(FileAccess::exists(pck_path));
		CHECK(env.list_output_files().is_empty());
		const String error_messages = env.collect_platform_error_messages();
		CHECK(error_messages.contains("res://clean_data.tres"));
		CHECK(error_messages.contains("built-in GDScript"));
	}

	SUBCASE("4b. Scene-customizer plugin injecting an embedded GDScript into a .tscn fails export") {
		BytecodeExportTestEnvironment env("fail_customizer_tscn");
		env.write_project_text_file("clean_scene.tscn",
				"[gd_scene format=3]\n"
				"[node name=\"Root\" type=\"Node\"]\n");

		Ref<EmbeddedScriptResourceCustomizerPlugin> customizer_plugin;
		customizer_plugin.instantiate();
		customizer_plugin->target_scene_path = "res://clean_scene.tscn";
		env.editor_export->add_export_plugin(customizer_plugin);

		Vector<String> selected_files;
		selected_files.push_back("res://clean_scene.tscn");
		Ref<EditorExportPreset> preset = env.create_preset(selected_files, EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);

		const String pck_path = env.output_dir.path_join("customized_tscn.pck");
		ERR_PRINT_OFF;
		const Error err = env.platform->export_pack(preset, false, pck_path);
		ERR_PRINT_ON;

		env.editor_export->remove_export_plugin(customizer_plugin);

		CHECK(err == ERR_INVALID_DATA);
		CHECK_FALSE(FileAccess::exists(pck_path));
		CHECK(env.list_output_files().is_empty());
		const String error_messages = env.collect_platform_error_messages();
		CHECK(error_messages.contains("res://clean_scene.tscn"));
		CHECK(error_messages.contains("built-in GDScript"));
	}

	SUBCASE("5. In-memory add_file() payloads with embedded GDScript fail across text .tscn/.tres, RSRC, and RSCC") {
		BytecodeExportTestEnvironment env("fail_in_memory_extra");
		env.write_project_text_file("trigger.txt", "trigger");

		Vector<String> selected_files;
		selected_files.push_back("res://trigger.txt");
		Ref<EditorExportPreset> preset = env.create_preset(selected_files, EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);

		const Vector<uint8_t> embedded_tscn_bytes = utf8_bytes(
				"[gd_scene load_steps=2 format=3]\n"
				"[sub_resource type=\"GDScript\" id=\"GDScript_mem\"]\n"
				"script/source = \"extends Node\\n\"\n"
				"[node name=\"Root\" type=\"Node\"]\n"
				"script = SubResource(\"GDScript_mem\")\n");

		const Vector<uint8_t> embedded_tres_top_level_bytes = utf8_bytes(
				"[gd_resource type=\"GDScript\" format=3]\n"
				"[resource]\n"
				"script/source = \"extends Resource\\n\"\n");

		const Vector<uint8_t> embedded_rsrc_bytes = build_binary_resource_bytes(env.base_dir.path_join("scratch"), "mem_rsrc.res", true, 0);
		REQUIRE(embedded_rsrc_bytes[0] == 'R');
		REQUIRE(embedded_rsrc_bytes[1] == 'S');
		REQUIRE(embedded_rsrc_bytes[2] == 'R');
		REQUIRE(embedded_rsrc_bytes[3] == 'C');

		const Vector<uint8_t> embedded_rscc_bytes = build_binary_resource_bytes(env.base_dir.path_join("scratch"), "mem_rscc.res", true, ResourceSaver::FLAG_COMPRESS);
		REQUIRE(embedded_rscc_bytes[0] == 'R');
		REQUIRE(embedded_rscc_bytes[1] == 'S');
		REQUIRE(embedded_rscc_bytes[2] == 'C');
		REQUIRE(embedded_rscc_bytes[3] == 'C');

		const Vector<uint8_t> clean_rscc_bytes = build_binary_resource_bytes(env.base_dir.path_join("scratch"), "mem_clean_rscc.res", false, ResourceSaver::FLAG_COMPRESS);
		REQUIRE(clean_rscc_bytes[0] == 'R');
		REQUIRE(clean_rscc_bytes[1] == 'S');
		REQUIRE(clean_rscc_bytes[2] == 'C');
		REQUIRE(clean_rscc_bytes[3] == 'C');

		struct InMemoryCase {
			String path;
			Vector<uint8_t> bytes;
			bool should_fail = true;
		};
		Vector<InMemoryCase> cases;
		cases.push_back({ "res://injected_embedded.tscn", embedded_tscn_bytes, true });
		cases.push_back({ "res://injected_top_level.tres", embedded_tres_top_level_bytes, true });
		cases.push_back({ "res://injected_uncompressed.res", embedded_rsrc_bytes, true });
		cases.push_back({ "res://injected_compressed.scn", embedded_rscc_bytes, true });
		cases.push_back({ "res://injected_clean_compressed.res", clean_rscc_bytes, false });

		for (int case_index = 0; case_index < cases.size(); case_index++) {
			const InMemoryCase &test_case = cases[case_index];
			Ref<InMemoryExtraFileInjectorPlugin> injector_plugin;
			injector_plugin.instantiate();
			injector_plugin->trigger_path = "res://trigger.txt";
			injector_plugin->injected_path = test_case.path;
			injector_plugin->injected_bytes = test_case.bytes;
			env.editor_export->add_export_plugin(injector_plugin);

			const String pck_path = env.output_dir.path_join(vformat("in_memory_case_%d.pck", case_index));
			ERR_PRINT_OFF;
			const Error err = env.platform->export_pack(preset, false, pck_path);
			ERR_PRINT_ON;

			env.editor_export->remove_export_plugin(injector_plugin);

			if (test_case.should_fail) {
				CHECK(err == ERR_INVALID_DATA);
				CHECK_FALSE(FileAccess::exists(pck_path));
				const String error_messages = env.collect_platform_error_messages();
				CHECK(error_messages.contains(test_case.path));
				CHECK(error_messages.contains("built-in GDScript"));
			} else {
				CHECK(err == OK);
				CHECK(FileAccess::exists(pck_path));
				DirAccess::remove_absolute(pck_path);
			}
		}
	}
}

TEST_CASE("[Editor][Export][GDScript] Headless CLI --export-pack returns 0 on valid bytecode export and nonzero on failures") {
	const String executable_path = OS::get_singleton()->get_executable_path();
	REQUIRE_FALSE(executable_path.is_empty());

	auto run_cli_export = [&](const String &p_case_name, const HashMap<String, String> &p_project_files, const Vector<String> &p_export_files, const String &p_pck_name, int &r_exit_code, String &r_pck_path) {
		const String case_dir = TestUtils::get_temp_path("gdscript_cli_export_" + p_case_name);
		if (DirAccess::exists(case_dir)) {
			Ref<DirAccess> existing_dir = DirAccess::open(case_dir);
			if (existing_dir.is_valid()) {
				existing_dir->erase_contents_recursive();
			}
		}
		const String project_dir = case_dir.path_join("project");
		const String output_dir = case_dir.path_join("output");
		DirAccess::make_dir_recursive_absolute(project_dir);
		DirAccess::make_dir_recursive_absolute(output_dir);

		for (const KeyValue<String, String> &entry : p_project_files) {
			const String abs_path = project_dir.path_join(entry.key);
			DirAccess::make_dir_recursive_absolute(abs_path.get_base_dir());
			Ref<FileAccess> file = FileAccess::open(abs_path, FileAccess::WRITE);
			REQUIRE(file.is_valid());
			file->store_string(entry.value);
		}

		String export_files_literal;
		for (int index = 0; index < p_export_files.size(); index++) {
			if (index > 0) {
				export_files_literal += ", ";
			}
			export_files_literal += "\"" + p_export_files[index] + "\"";
		}

		const String presets_cfg =
				"[preset.0]\n"
				"name=\"BytecodePack\"\n"
				"platform=\"Linux\"\n"
				"runnable=true\n"
				"export_filter=\"resources\"\n"
				"include_filter=\"\"\n"
				"exclude_filter=\"\"\n"
				"export_files=PackedStringArray(" +
				export_files_literal +
				")\n"
				"script_export_mode=3\n"
				"\n"
				"[preset.0.options]\n"
				"custom_template/debug=\"\"\n";
		{
			Ref<FileAccess> presets_file = FileAccess::open(project_dir.path_join("export_presets.cfg"), FileAccess::WRITE);
			REQUIRE(presets_file.is_valid());
			presets_file->store_string(presets_cfg);
		}

		r_pck_path = output_dir.path_join(p_pck_name);
		List<String> args;
		args.push_back("--headless");
		args.push_back("--audio-driver");
		args.push_back("Dummy");
		args.push_back("--path");
		args.push_back(project_dir);
		args.push_back("--export-pack");
		args.push_back("BytecodePack");
		args.push_back(r_pck_path);

		String cli_output;
		r_exit_code = -1;
		const Error exec_err = OS::get_singleton()->execute(executable_path, args, &cli_output, &r_exit_code, true);
		INFO("CLI output: " << cli_output.utf8().get_data());
		REQUIRE(exec_err == OK);
	};

	SUBCASE("Valid project exits 0 and emits .gdb + .remap without .gd/.gdc") {
		HashMap<String, String> files;
		files["project.godot"] = "config_version=5\n\n[application]\nconfig/name=\"CliValidBytecode\"\n";
		files["main.gd"] = "extends Node\nfunc answer() -> int:\n\treturn 42\n";

		Vector<String> export_files;
		export_files.push_back("res://main.gd");

		int exit_code = -1;
		String pck_path;
		run_cli_export("valid", files, export_files, "valid_cli.pck", exit_code, pck_path);

		CHECK(exit_code == 0);
		REQUIRE(FileAccess::exists(pck_path));

		HashMap<String, Vector<uint8_t>> entries = read_pck_entries(pck_path);
		CHECK(entries.has("res://main.gdb"));
		CHECK(entries.has("res://main.gd.remap"));
		CHECK_FALSE(entries.has("res://main.gd"));
		CHECK_FALSE(entries.has("res://main.gdc"));

		GDScriptBytecodeLoader::HeaderInfo header_info;
		CHECK(GDScriptBytecodeLoader::check_header(entries["res://main.gdb"], &header_info) == OK);
	}

	SUBCASE("Project with embedded GDScript exits nonzero and leaves no partial PCK") {
		HashMap<String, String> files;
		files["project.godot"] = "config_version=5\n\n[application]\nconfig/name=\"CliEmbeddedFail\"\n";
		files["embedded.tscn"] =
				"[gd_scene load_steps=2 format=3]\n"
				"[sub_resource type=\"GDScript\" id=\"GDScript_cli\"]\n"
				"script/source = \"extends Node\\n\"\n"
				"[node name=\"Root\" type=\"Node\"]\n"
				"script = SubResource(\"GDScript_cli\")\n";

		Vector<String> export_files;
		export_files.push_back("res://embedded.tscn");

		int exit_code = 0;
		String pck_path;
		run_cli_export("embedded_fail", files, export_files, "embedded_cli.pck", exit_code, pck_path);

		CHECK(exit_code != 0);
		CHECK_FALSE(FileAccess::exists(pck_path));
	}

	SUBCASE("Project with compile-error script exits nonzero and leaves no partial PCK") {
		HashMap<String, String> files;
		files["project.godot"] = "config_version=5\n\n[application]\nconfig/name=\"CliCompileFail\"\n";
		files["syntax_error.gd"] = "extends Node\nfunc bad(:\n\tpass\n";

		Vector<String> export_files;
		export_files.push_back("res://syntax_error.gd");

		int exit_code = 0;
		String pck_path;
		run_cli_export("compile_fail", files, export_files, "compile_cli.pck", exit_code, pck_path);

		CHECK(exit_code != 0);
		CHECK_FALSE(FileAccess::exists(pck_path));
	}
}

TEST_CASE("[Editor][Export][GDScript] Compressed compiled bytecode mode exports zstd .gdb and still rejects built-in scripts") {
	BytecodeExportTestEnvironment env("compressed_bytecode_mode");
	env.write_project_text_file(
			"worker.gd",
			"extends RefCounted\n"
			"const TITLE: String = \"compressed_worker_title\"\n"
			"func compute(value: int) -> int:\n"
			"\tvar accumulator: int = value\n"
			"\tfor step in range(10):\n"
			"\t\taccumulator += step * 3\n"
			"\treturn accumulator\n");
	env.write_project_text_file(
			"embedded.tscn",
			"[gd_scene load_steps=2 format=3]\n"
			"[sub_resource type=\"GDScript\" id=\"GDScript_zstd\"]\n"
			"script/source = \"extends Node\\n\"\n"
			"[node name=\"Root\" type=\"Node\"]\n"
			"script = SubResource(\"GDScript_zstd\")\n");

	Vector<String> valid_files;
	valid_files.push_back("res://worker.gd");

	Ref<EditorExportPreset> uncompressed_preset = env.create_preset(valid_files, EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);
	const String uncompressed_pck = env.output_dir.path_join("worker_raw.pck");
	REQUIRE(env.platform->export_pack(uncompressed_preset, false, uncompressed_pck) == OK);

	Ref<EditorExportPreset> compressed_preset = env.create_preset(valid_files, EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE_COMPRESSED);
	const String compressed_pck = env.output_dir.path_join("worker_zstd.pck");
	REQUIRE(env.platform->export_pack(compressed_preset, false, compressed_pck) == OK);

	HashMap<String, Vector<uint8_t>> raw_entries = read_pck_entries(uncompressed_pck);
	HashMap<String, Vector<uint8_t>> zstd_entries = read_pck_entries(compressed_pck);
	REQUIRE(raw_entries.has("res://worker.gdb"));
	REQUIRE(zstd_entries.has("res://worker.gdb"));
	CHECK(zstd_entries.has("res://worker.gd.remap"));
	CHECK_FALSE(zstd_entries.has("res://worker.gd"));

	CHECK(zstd_entries["res://worker.gdb"].size() < raw_entries["res://worker.gdb"].size());

	GDScriptBytecodeLoader::HeaderInfo header;
	REQUIRE(GDScriptBytecodeLoader::check_header(zstd_entries["res://worker.gdb"], &header) == OK);
	CHECK(header.compression_mode == GDScriptBytecodeFormat::COMPRESSION_ZSTD);
	CHECK(header.uncompressed_payload_size > 0);

	Ref<GDScript> loaded = memnew(GDScript);
	REQUIRE(GDScriptBytecodeLoader::load_full(zstd_entries["res://worker.gdb"], loaded, "res://worker.gd", nullptr) == OK);
	Ref<RefCounted> instance = memnew(RefCounted);
	instance->set_script(loaded);
	CHECK(int(instance->call(StringName("compute"), 5)) == 5 + 135);

	// Built-in GDScript rejection remains active in compressed bytecode mode.
	Vector<String> embedded_files;
	embedded_files.push_back("res://embedded.tscn");
	Ref<EditorExportPreset> embedded_preset = env.create_preset(embedded_files, EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE_COMPRESSED);
	const String embedded_pck = env.output_dir.path_join("embedded_zstd.pck");
	ERR_PRINT_OFF;
	const Error embedded_err = env.platform->export_pack(embedded_preset, false, embedded_pck);
	ERR_PRINT_ON;
	CHECK(embedded_err == ERR_INVALID_DATA);
	CHECK_FALSE(FileAccess::exists(embedded_pck));
}

} // namespace TestGDScriptBytecodeExportPlugin

#endif // defined(TOOLS_ENABLED) && defined(MODULE_GDSCRIPT_ENABLED)
