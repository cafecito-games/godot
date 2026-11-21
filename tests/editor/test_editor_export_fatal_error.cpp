/**************************************************************************/
/*  test_editor_export_fatal_error.cpp                                    */
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

TEST_FORCE_LINK(test_editor_export_fatal_error)

#ifdef TOOLS_ENABLED

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_importer.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "editor/export/editor_export.h"
#include "editor/export/editor_export_platform.h"
#include "editor/export/editor_export_plugin.h"
#include "editor/export/editor_export_preset.h"
#include "editor/file_system/editor_file_system.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "tests/test_utils.h"

namespace TestEditorExportFatalError {

class MockExportPlatform : public EditorExportPlatform {
	GDCLASS(MockExportPlatform, EditorExportPlatform);

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

struct ValidationRecord {
	String source_path;
	String exported_path;
	String resource_type;
	bool has_in_memory_data = false;
	Vector<uint8_t> in_memory_data;
};

class ConfigurableExportPlugin : public EditorExportPlugin {
	GDCLASS(ConfigurableExportPlugin, EditorExportPlugin);

public:
	String plugin_name = "ConfigurableExportPlugin";

	bool fail_in_begin = false;
	Error begin_error = ERR_PARSE_ERROR;

	String fail_on_export_file_path;
	Error export_file_error = ERR_COMPILATION_FAILED;

	bool fail_in_end = false;
	Error end_error = ERR_BUG;

	String inject_extra_trigger_path;
	String inject_extra_path;
	Vector<uint8_t> inject_extra_data;

	bool customize_resources = false;
	String customize_target_path;

	String fail_validate_source_path;
	bool fail_validate_require_in_memory = false;
	bool fail_validate_require_on_disk = false;
	Error validate_error = ERR_INVALID_DATA;

	int export_begin_count = 0;
	int export_file_count = 0;
	int export_end_count = 0;
	Vector<ValidationRecord> validation_records;

	virtual String get_name() const override { return plugin_name; }
	virtual bool supports_platform(const Ref<EditorExportPlatform> &p_export_platform) const override { return true; }

protected:
	virtual void _export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) override {
		export_begin_count++;
		if (fail_in_begin) {
			fail_export(begin_error, "TestExport", "Intentional begin failure");
		}
	}

	virtual void _export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features) override {
		export_file_count++;
		if (!inject_extra_trigger_path.is_empty() && p_path == inject_extra_trigger_path) {
			add_file(inject_extra_path, inject_extra_data, false);
		}
		if (!fail_on_export_file_path.is_empty() && p_path == fail_on_export_file_path) {
			fail_export(export_file_error, "TestExport", "Intentional file failure");
		}
	}

	virtual void _export_end() override {
		export_end_count++;
		if (fail_in_end) {
			fail_export(end_error, "TestExport", "Intentional end failure");
		}
	}

	virtual bool _begin_customize_resources(const Ref<EditorExportPlatform> &p_platform, const Vector<String> &p_features) override {
		return customize_resources;
	}

	virtual Ref<Resource> _customize_resource(const Ref<Resource> &p_resource, const String &p_path) override {
		if (customize_resources && !customize_target_path.is_empty() && p_path == customize_target_path) {
			p_resource->set_name("customized_resource");
			return p_resource;
		}
		return Ref<Resource>();
	}

	virtual uint64_t _get_customization_configuration_hash() const override {
		return 0x12345678ULL;
	}

	virtual void _validate_exported_file(const String &p_source_path, const String &p_exported_path, const String &p_type, const Vector<uint8_t> *p_in_memory_data = nullptr) override {
		ValidationRecord record;
		record.source_path = p_source_path;
		record.exported_path = p_exported_path;
		record.resource_type = p_type;
		if (p_in_memory_data != nullptr) {
			record.has_in_memory_data = true;
			record.in_memory_data = *p_in_memory_data;
		}
		validation_records.push_back(record);

		if (!fail_validate_source_path.is_empty() && p_source_path == fail_validate_source_path) {
			if (fail_validate_require_in_memory && p_in_memory_data == nullptr) {
				return;
			}
			if (fail_validate_require_on_disk && p_in_memory_data != nullptr) {
				return;
			}
			fail_export(validate_error, "TestValidation", "Intentional validation failure");
		}
	}
};

struct ExportTestEnvironment {
	String previous_resource_path;
	ResourceLoaderImport previous_import_callback = nullptr;
	ResourceFormatImporterLoadOnStartup previous_load_on_startup = nullptr;

	String project_dir;
	String output_dir;
	EditorExport *editor_export = nullptr;
	EditorFileSystem *editor_file_system = nullptr;
	Ref<MockExportPlatform> platform;

	ExportTestEnvironment(const String &p_name) {
		previous_resource_path = TestProjectSettingsInternalsAccessor::resource_path();
		previous_import_callback = ResourceLoader::import;
		previous_load_on_startup = ResourceImporter::load_on_startup;

		String base_dir = TestUtils::get_temp_path("export_fatal_" + p_name);
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

		// Write a minimal project.godot so ProjectSettings and export paths resolve cleanly.
		Ref<FileAccess> project_file = FileAccess::open(project_dir.path_join("project.godot"), FileAccess::WRITE);
		if (project_file.is_valid()) {
			project_file->store_line("config_version=5");
			project_file->store_line("[application]");
			project_file->store_line("config/name=\"ExportFatalTest\"");
		}

		TestProjectSettingsInternalsAccessor::resource_path() = project_dir;

		editor_file_system = memnew(EditorFileSystem);
		SceneTree::get_singleton()->get_root()->add_child(editor_file_system);

		editor_export = memnew(EditorExport);
		SceneTree::get_singleton()->get_root()->add_child(editor_export);

		platform.instantiate();
		editor_export->add_export_platform(platform);
	}

	~ExportTestEnvironment() {
		if (editor_export) {
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
		String absolute_path = project_dir.path_join(p_relative_path);
		DirAccess::make_dir_recursive_absolute(absolute_path.get_base_dir());
		Ref<FileAccess> file = FileAccess::open(absolute_path, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string(p_contents);
	}

	void write_project_resource_file(const String &p_relative_path) const {
		Ref<Resource> resource;
		resource.instantiate();
		resource->set_name("original_resource");
		Error save_err = ResourceSaver::save(resource, "res://" + p_relative_path);
		REQUIRE(save_err == OK);
	}

	Ref<EditorExportPreset> create_selected_files_preset(const Vector<String> &p_files) {
		Ref<EditorExportPreset> preset = platform->create_preset();
		preset->set_name("TestPreset");
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
};

static Error noop_save_callback(const Ref<EditorExportPreset> &p_preset, void *p_userdata, const String &p_path, const Vector<uint8_t> &p_data, int p_file, int p_total, const Vector<String> &p_enc_in_filters, const Vector<String> &p_enc_ex_filters, const Vector<uint8_t> &p_key, uint64_t p_seed, bool p_delta) {
	Vector<String> *saved_paths = static_cast<Vector<String> *>(p_userdata);
	if (saved_paths != nullptr) {
		saved_paths->push_back(p_path);
	}
	return OK;
}

TEST_CASE("[Editor][Export] Fatal export error aborts export_project_files and cleans up pack/zip staging files") {
	ExportTestEnvironment env("fatal_error_cleanup");
	env.write_project_text_file("first.txt", "hello");
	env.write_project_text_file("second.txt", "world");

	Vector<String> selected_files;
	selected_files.push_back("res://first.txt");
	selected_files.push_back("res://second.txt");
	Ref<EditorExportPreset> preset = env.create_selected_files_preset(selected_files);

	SUBCASE("fail_export in _export_file aborts export_project_files directly") {
		Ref<ConfigurableExportPlugin> failing_plugin;
		failing_plugin.instantiate();
		failing_plugin->plugin_name = "A_FailingPlugin";
		failing_plugin->fail_on_export_file_path = "res://first.txt";
		failing_plugin->export_file_error = ERR_COMPILATION_FAILED;
		env.editor_export->add_export_plugin(failing_plugin);

		Vector<String> saved_paths;
		ERR_PRINT_OFF;
		Error err = env.platform->export_project_files(preset, false, noop_save_callback, nullptr, &saved_paths);
		ERR_PRINT_ON;

		CHECK(err == ERR_COMPILATION_FAILED);
		CHECK(failing_plugin->get_fatal_export_error() == OK);
		CHECK_FALSE(saved_paths.has("res://first.txt"));

		env.editor_export->remove_export_plugin(failing_plugin);
	}

	SUBCASE("fail_export in _export_file runs _export_end and leaves zero partial pck or zip") {
		Ref<ConfigurableExportPlugin> failing_plugin;
		failing_plugin.instantiate();
		failing_plugin->plugin_name = "A_FailingPlugin";
		failing_plugin->fail_on_export_file_path = "res://first.txt";
		failing_plugin->export_file_error = ERR_COMPILATION_FAILED;
		env.editor_export->add_export_plugin(failing_plugin);

		String pck_path = env.output_dir.path_join("failed_export.pck");
		String zip_path = env.output_dir.path_join("failed_export.zip");

		ERR_PRINT_OFF;
		Error pck_err = env.platform->export_pack(preset, false, pck_path);
		ERR_PRINT_ON;

		CHECK(pck_err == ERR_COMPILATION_FAILED);
		CHECK(failing_plugin->export_begin_count == 1);
		CHECK(failing_plugin->export_end_count == 1);
		CHECK(failing_plugin->get_fatal_export_error() == OK);
		CHECK_FALSE(FileAccess::exists(pck_path));
		CHECK(env.list_output_files().is_empty());

		ERR_PRINT_OFF;
		Error zip_err = env.platform->export_zip(preset, false, zip_path);
		ERR_PRINT_ON;

		CHECK(zip_err == ERR_COMPILATION_FAILED);
		CHECK(failing_plugin->export_begin_count == 2);
		CHECK(failing_plugin->export_end_count == 2);
		CHECK(failing_plugin->get_fatal_export_error() == OK);
		CHECK_FALSE(FileAccess::exists(zip_path));
		CHECK(env.list_output_files().is_empty());

		env.editor_export->remove_export_plugin(failing_plugin);
	}

	SUBCASE("fail_export in _export_begin only runs _export_end on plugins whose _export_begin executed") {
		Ref<ConfigurableExportPlugin> first_plugin;
		first_plugin.instantiate();
		first_plugin->plugin_name = "A_FirstPlugin";
		first_plugin->fail_in_begin = true;
		first_plugin->begin_error = ERR_PARSE_ERROR;

		Ref<ConfigurableExportPlugin> second_plugin;
		second_plugin.instantiate();
		second_plugin->plugin_name = "B_SecondPlugin";

		env.editor_export->add_export_plugin(first_plugin);
		env.editor_export->add_export_plugin(second_plugin);

		String pck_path = env.output_dir.path_join("begin_failed.pck");
		ERR_PRINT_OFF;
		Error err = env.platform->export_pack(preset, false, pck_path);
		ERR_PRINT_ON;

		CHECK(err == ERR_PARSE_ERROR);
		CHECK(first_plugin->export_begin_count == 1);
		CHECK(first_plugin->export_end_count == 1);
		CHECK(second_plugin->export_begin_count == 0);
		CHECK(second_plugin->export_end_count == 0);
		CHECK(first_plugin->get_fatal_export_error() == OK);
		CHECK_FALSE(FileAccess::exists(pck_path));
		CHECK(env.list_output_files().is_empty());

		env.editor_export->remove_export_plugin(first_plugin);
		env.editor_export->remove_export_plugin(second_plugin);
	}

	SUBCASE("fail_export in _export_end aborts and leaves zero partial pck or zip") {
		Ref<ConfigurableExportPlugin> end_failing_plugin;
		end_failing_plugin.instantiate();
		end_failing_plugin->plugin_name = "A_EndFailingPlugin";
		end_failing_plugin->fail_in_end = true;
		end_failing_plugin->end_error = ERR_BUG;
		env.editor_export->add_export_plugin(end_failing_plugin);

		String pck_path = env.output_dir.path_join("end_failed.pck");
		String zip_path = env.output_dir.path_join("end_failed.zip");

		ERR_PRINT_OFF;
		Error pck_err = env.platform->export_pack(preset, false, pck_path);
		Error zip_err = env.platform->export_zip(preset, false, zip_path);
		ERR_PRINT_ON;

		CHECK(pck_err == ERR_BUG);
		CHECK(zip_err == ERR_BUG);
		CHECK(end_failing_plugin->export_end_count == 2);
		CHECK(end_failing_plugin->get_fatal_export_error() == OK);
		CHECK_FALSE(FileAccess::exists(pck_path));
		CHECK_FALSE(FileAccess::exists(zip_path));
		CHECK(env.list_output_files().is_empty());

		// Also verify a pre-existing output file is left untouched when _export_end() fails.
		Ref<FileAccess> existing_pck = FileAccess::open(pck_path, FileAccess::WRITE);
		REQUIRE(existing_pck.is_valid());
		existing_pck->store_string("previous_pck_contents");
		existing_pck->close();

		ERR_PRINT_OFF;
		Error overwrite_err = env.platform->export_pack(preset, false, pck_path);
		ERR_PRINT_ON;

		CHECK(overwrite_err == ERR_BUG);
		CHECK(FileAccess::get_file_as_string(pck_path) == "previous_pck_contents");
		CHECK(env.list_output_files().size() == 1);

		env.editor_export->remove_export_plugin(end_failing_plugin);
	}

	SUBCASE("Successful pack and zip export atomically commit target file with no leftover staging files") {
		String pck_path = env.output_dir.path_join("ok_export.pck");
		String zip_path = env.output_dir.path_join("ok_export.zip");

		Error pck_err = env.platform->export_pack(preset, false, pck_path);
		Error zip_err = env.platform->export_zip(preset, false, zip_path);

		CHECK(pck_err == OK);
		CHECK(zip_err == OK);
		CHECK(FileAccess::exists(pck_path));
		CHECK(FileAccess::exists(zip_path));
		CHECK(env.list_output_files().size() == 2);
	}
}

TEST_CASE("[Editor][Export] _validate_exported_file covers customized on-disk resources and in-memory extra files") {
	ExportTestEnvironment env("validate_exported_file");
	env.write_project_resource_file("sample.tres");

	Vector<String> selected_files;
	selected_files.push_back("res://sample.tres");
	Ref<EditorExportPreset> preset = env.create_selected_files_preset(selected_files);

	Vector<uint8_t> extra_bytes;
	extra_bytes.push_back(0x47);
	extra_bytes.push_back(0x44);
	extra_bytes.push_back(0x42);
	extra_bytes.push_back(0x43);

	Ref<ConfigurableExportPlugin> producer_plugin;
	producer_plugin.instantiate();
	producer_plugin->plugin_name = "A_ProducerPlugin";
	producer_plugin->customize_resources = true;
	producer_plugin->customize_target_path = "res://sample.tres";
	producer_plugin->inject_extra_trigger_path = "res://sample.tres";
	producer_plugin->inject_extra_path = "res://generated_extra.bin";
	producer_plugin->inject_extra_data = extra_bytes;

	Ref<ConfigurableExportPlugin> validator_plugin;
	validator_plugin.instantiate();
	validator_plugin->plugin_name = "B_ValidatorPlugin";

	env.editor_export->add_export_plugin(producer_plugin);
	env.editor_export->add_export_plugin(validator_plugin);

	SUBCASE("Validator observes customized on-disk path and in-memory extra file buffer") {
		String pck_path = env.output_dir.path_join("validated_ok.pck");
		Error err = env.platform->export_pack(preset, false, pck_path);
		CHECK(err == OK);
		CHECK(FileAccess::exists(pck_path));

		bool saw_customized_resource = false;
		bool saw_in_memory_extra = false;

		for (const ValidationRecord &record : validator_plugin->validation_records) {
			if (record.source_path == "res://sample.tres") {
				saw_customized_resource = true;
				CHECK_FALSE(record.has_in_memory_data);
				CHECK(record.exported_path != record.source_path);
				CHECK(record.exported_path.ends_with(".res"));
				CHECK(FileAccess::exists(record.exported_path));
			}
			if (record.source_path == "res://generated_extra.bin") {
				saw_in_memory_extra = true;
				CHECK(record.exported_path == "res://generated_extra.bin");
				CHECK(record.has_in_memory_data);
				CHECK(record.in_memory_data == extra_bytes);
			}
		}

		CHECK(saw_customized_resource);
		CHECK(saw_in_memory_extra);
	}

	SUBCASE("fail_export from _validate_exported_file on customized on-disk resource aborts export cleanly") {
		validator_plugin->fail_validate_source_path = "res://sample.tres";
		validator_plugin->fail_validate_require_on_disk = true;
		validator_plugin->validate_error = ERR_INVALID_DATA;

		String pck_path = env.output_dir.path_join("validate_disk_fail.pck");
		ERR_PRINT_OFF;
		Error err = env.platform->export_pack(preset, false, pck_path);
		ERR_PRINT_ON;

		CHECK(err == ERR_INVALID_DATA);
		CHECK(validator_plugin->export_end_count == 1);
		CHECK(producer_plugin->export_end_count == 1);
		CHECK(validator_plugin->get_fatal_export_error() == OK);
		CHECK_FALSE(FileAccess::exists(pck_path));
		CHECK(env.list_output_files().is_empty());
	}

	SUBCASE("fail_export from _validate_exported_file on in-memory extra file aborts export cleanly") {
		validator_plugin->fail_validate_source_path = "res://generated_extra.bin";
		validator_plugin->fail_validate_require_in_memory = true;
		validator_plugin->validate_error = ERR_INVALID_DATA;

		String zip_path = env.output_dir.path_join("validate_memory_fail.zip");
		ERR_PRINT_OFF;
		Error err = env.platform->export_zip(preset, false, zip_path);
		ERR_PRINT_ON;

		CHECK(err == ERR_INVALID_DATA);
		CHECK(validator_plugin->export_end_count == 1);
		CHECK(producer_plugin->export_end_count == 1);
		CHECK(validator_plugin->get_fatal_export_error() == OK);
		CHECK_FALSE(FileAccess::exists(zip_path));
		CHECK(env.list_output_files().is_empty());
	}

	env.editor_export->remove_export_plugin(producer_plugin);
	env.editor_export->remove_export_plugin(validator_plugin);
}

TEST_CASE("[Editor][Export] EditorExportPreset persists and loads MODE_SCRIPT_COMPILED_BYTECODE") {
	CHECK(static_cast<int>(EditorExportPreset::MODE_SCRIPT_TEXT) == 0);
	CHECK(static_cast<int>(EditorExportPreset::MODE_SCRIPT_BINARY_TOKENS) == 1);
	CHECK(static_cast<int>(EditorExportPreset::MODE_SCRIPT_BINARY_TOKENS_COMPRESSED) == 2);
	CHECK(static_cast<int>(EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE) == 3);
	CHECK(static_cast<int>(EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE_COMPRESSED) == 4);

	ExportTestEnvironment env("script_mode_persistence");

	Ref<EditorExportPreset> preset = env.platform->create_preset();
	preset->set_name("CompiledBytecodePreset");
	preset->set_script_export_mode(EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);
	CHECK(preset->get_script_export_mode() == EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);

	Ref<EditorExportPreset> compressed_preset = env.platform->create_preset();
	compressed_preset->set_name("CompressedCompiledBytecodePreset");
	compressed_preset->set_script_export_mode(EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE_COMPRESSED);
	CHECK(compressed_preset->get_script_export_mode() == EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE_COMPRESSED);

	env.editor_export->add_export_preset(preset);
	env.editor_export->add_export_preset(compressed_preset);

	// Trigger EditorExport's internal save timer timeout to write res://export_presets.cfg synchronously.
	REQUIRE(env.editor_export->get_child_count() > 0);
	env.editor_export->get_child(0)->emit_signal(SNAME("timeout"));
	REQUIRE(FileAccess::exists("res://export_presets.cfg"));

	// Remove the in-memory presets and reload from export_presets.cfg.
	env.editor_export->remove_export_preset(1);
	env.editor_export->remove_export_preset(0);
	CHECK(env.editor_export->get_export_preset_count() == 0);

	env.editor_export->load_config();
	REQUIRE(env.editor_export->get_export_preset_count() == 2);

	Ref<EditorExportPreset> loaded_preset = env.editor_export->get_export_preset(0);
	REQUIRE(loaded_preset.is_valid());
	CHECK(loaded_preset->get_name() == "CompiledBytecodePreset");
	CHECK(loaded_preset->get_script_export_mode() == EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);

	Ref<EditorExportPreset> loaded_compressed_preset = env.editor_export->get_export_preset(1);
	REQUIRE(loaded_compressed_preset.is_valid());
	CHECK(loaded_compressed_preset->get_name() == "CompressedCompiledBytecodePreset");
	CHECK(loaded_compressed_preset->get_script_export_mode() == EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE_COMPRESSED);
}

} // namespace TestEditorExportFatalError

#endif // TOOLS_ENABLED
