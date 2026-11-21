/**************************************************************************/
/*  gdscript_editor_export_plugin.cpp                                     */
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

#include "gdscript_editor_export_plugin.h"

#ifdef TOOLS_ENABLED

#include "../gdscript.h"
#include "../gdscript_bytecode_export.h"
#include "../gdscript_cache.h"
#include "../gdscript_export_compile_session.h"
#include "../gdscript_tokenizer_buffer.h"

#include "core/io/file_access.h"
#include "core/io/file_access_memory.h"
#include "core/io/resource_format_binary.h"
#include "core/object/class_db.h"
#include "scene/resources/resource_format_text.h"

namespace {

class ExportValidationMemoryFile : public FileAccessMemory {
	GDSOFTCLASS(ExportValidationMemoryFile, FileAccessMemory);

public:
	virtual uint64_t get_buffer(uint8_t *p_dst, uint64_t p_length) const override {
		if (p_length == 0 || !is_open()) {
			return 0;
		}
		const uint64_t file_length = get_length();
		const uint64_t current_position = get_position();
		if (current_position >= file_length) {
			return 0;
		}
		const uint64_t clamped_length = MIN(p_length, file_length - current_position);
		return FileAccessMemory::get_buffer(p_dst, clamped_length);
	}
};

} // namespace

EditorExportGDScript::~EditorExportGDScript() {
	_reset_compile_session();
}

void EditorExportGDScript::_reset_compile_session() {
	if (compile_session != nullptr) {
		memdelete(compile_session);
		compile_session = nullptr;
	}
}

String EditorExportGDScript::_format_compile_error(const String &p_path, Error p_fallback_error) const {
	if (compile_session != nullptr) {
		const String error_message = compile_session->get_last_error_message();
		if (!error_message.is_empty()) {
			const String error_path = compile_session->get_last_error_path();
			const int error_line = compile_session->get_last_error_line();
			if (error_line >= 0) {
				if (!error_path.is_empty() && error_path != p_path) {
					return vformat("%s (%s:%d)", error_message, error_path, error_line);
				}
				return vformat("%s (line %d)", error_message, error_line);
			}
			return error_message;
		}
	}
	if (p_fallback_error != OK) {
		return String(error_names[p_fallback_error]);
	}
	return TTR("script is not valid");
}

bool EditorExportGDScript::_is_compiled_bytecode_mode(EditorExportPreset::ScriptExportMode p_mode) {
	return p_mode == EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE || p_mode == EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE_COMPRESSED;
}

bool EditorExportGDScript::_is_gdscript_class_name(const StringName &p_class_name) {
	if (p_class_name == StringName()) {
		return false;
	}
	if (p_class_name == SNAME("GDScript")) {
		return true;
	}
	return ClassDB::class_exists(p_class_name) && ClassDB::is_parent_class(p_class_name, SNAME("GDScript"));
}

bool EditorExportGDScript::_classes_contain_gdscript(const HashSet<StringName> &p_classes) {
	for (const StringName &class_name : p_classes) {
		if (_is_gdscript_class_name(class_name)) {
			return true;
		}
	}
	return false;
}

bool EditorExportGDScript::_contains_embedded_gdscript(const String &p_exported_path, const Vector<uint8_t> *p_in_memory_data) {
	Ref<FileAccess> file;
	if (p_in_memory_data != nullptr) {
		if (p_in_memory_data->is_empty()) {
			return false;
		}
		Ref<ExportValidationMemoryFile> memory_file;
		memory_file.instantiate();
		if (memory_file->open_custom(p_in_memory_data->ptr(), p_in_memory_data->size()) != OK) {
			return false;
		}
		file = memory_file;
	} else {
		file = FileAccess::open(p_exported_path, FileAccess::READ);
		if (file.is_null()) {
			return false;
		}
	}

	bool is_binary_resource = false;
	if (file->get_length() >= 4) {
		uint8_t magic[4] = {};
		if (file->get_buffer(magic, 4) == 4 && magic[0] == 'R' && magic[1] == 'S' &&
				((magic[2] == 'R' && magic[3] == 'C') || (magic[2] == 'C' && magic[3] == 'C'))) {
			is_binary_resource = true;
		}
		file->seek(0);
	}

	if (is_binary_resource) {
		HashSet<StringName> classes_used;
		ResourceLoaderBinary binary_loader;
		binary_loader.get_classes_used(file, &classes_used);
		return _classes_contain_gdscript(classes_used);
	}

	const String extension = p_exported_path.get_extension().to_lower();
	if (extension != "tscn" && extension != "tres") {
		return false;
	}

	uint64_t text_start_offset = 0;
	if (file->get_length() >= 3) {
		uint8_t bom[3] = {};
		if (file->get_buffer(bom, 3) == 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF) {
			text_start_offset = 3;
		}
		file->seek(text_start_offset);
	}

	ResourceLoaderText type_recognizer;
	const String top_level_type = type_recognizer.recognize(file);
	if (top_level_type.is_empty()) {
		return false;
	}
	if (_is_gdscript_class_name(StringName(top_level_type))) {
		return true;
	}

	file->seek(0);
	HashSet<StringName> classes_used;
	ResourceLoaderText text_loader;
	text_loader.open(file);
	text_loader.get_classes_used(&classes_used);
	return _classes_contain_gdscript(classes_used);
}

void EditorExportGDScript::_export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) {
	_reset_compile_session();
	export_debug = p_debug;
	script_mode = DEFAULT_SCRIPT_MODE;

	const Ref<EditorExportPreset> &preset = get_export_preset();
	if (preset.is_valid()) {
		script_mode = preset->get_script_export_mode();
	}

	if (_is_compiled_bytecode_mode(script_mode)) {
		compile_session = memnew(GDScriptExportCompileSession(!export_debug));
	}
}

void EditorExportGDScript::_export_file_compiled_bytecode(const String &p_path) {
	if (p_path.get_extension().to_lower() != "gd") {
		return;
	}

	if (compile_session == nullptr) {
		fail_export(ERR_UNCONFIGURED, TTR("Compiled Script Export"), vformat(TTR("Cannot export \"%s\": compiled bytecode export session is not initialized."), p_path));
		return;
	}

	Error compile_error = OK;
	Ref<GDScript> compiled_script = compile_session->get_or_compile(p_path, compile_error);
	DEV_ASSERT(!GDScriptCache::has_active_export_session());

	if (compile_error != OK || compiled_script.is_null() || !compiled_script->is_valid()) {
		const Error reported_error = (compile_error != OK) ? compile_error : ERR_COMPILATION_FAILED;
		const String detail = _format_compile_error(p_path, compile_error);
		fail_export(reported_error, TTR("Compiled Script Export"), vformat(TTR("Script \"%s\" failed to compile for bytecode export: %s"), p_path, detail));
		return;
	}

	const Vector<StringName> unsupported_named_globals = GDScriptBytecodeExporter::collect_unsupported_named_globals(compiled_script);
	if (!unsupported_named_globals.is_empty()) {
		Vector<String> global_names;
		global_names.resize(unsupported_named_globals.size());
		for (int index = 0; index < unsupported_named_globals.size(); index++) {
			global_names.write[index] = String(unsupported_named_globals[index]);
		}
		fail_export(
				ERR_INVALID_DATA,
				TTR("Compiled Script Export"),
				vformat(TTR("Script \"%s\" references editor-only or unsupported named globals that are unavailable in exported bytecode: %s."), p_path, String(", ").join(global_names)));
		return;
	}

	GDScriptBytecodeExporter::Options export_options;
	export_options.annotated_static_unload = compile_session->has_annotated_static_unload(p_path);
	export_options.release_profile = compile_session->is_release_profile();
	export_options.emit_line_opcodes = compile_session->should_emit_line_opcodes();
	export_options.auto_detect_static_data = true;
	export_options.compression_mode = (script_mode == EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE_COMPRESSED)
			? GDScriptBytecodeFormat::COMPRESSION_ZSTD
			: GDScriptBytecodeFormat::COMPRESSION_NONE;

	Vector<uint8_t> bytecode_buffer;
	String serialize_error_message;
	Error serialize_error = GDScriptBytecodeExporter::serialize(compiled_script, bytecode_buffer, export_options, &serialize_error_message);
	if (serialize_error != OK || bytecode_buffer.is_empty()) {
		const Error reported_error = (serialize_error != OK) ? serialize_error : ERR_INVALID_DATA;
		const String detail = !serialize_error_message.is_empty()
				? serialize_error_message
				: (serialize_error == OK ? TTR("serializer produced no data") : String(error_names[serialize_error]));
		fail_export(reported_error, TTR("Compiled Script Export"), vformat(TTR("Script \"%s\" could not be serialized to compiled bytecode: %s"), p_path, detail));
		return;
	}

	add_file(p_path.get_basename() + ".gdb", bytecode_buffer, true);
}

void EditorExportGDScript::_export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features) {
	if (_is_compiled_bytecode_mode(script_mode)) {
		_export_file_compiled_bytecode(p_path);
		return;
	}

	if (p_path.get_extension() != "gd" || script_mode == EditorExportPreset::MODE_SCRIPT_TEXT) {
		return;
	}

	Vector<uint8_t> file = FileAccess::get_file_as_bytes(p_path);
	if (file.is_empty()) {
		return;
	}

	String source = String::utf8(reinterpret_cast<const char *>(file.ptr()), file.size());
	GDScriptTokenizerBuffer::CompressMode compress_mode = script_mode == EditorExportPreset::MODE_SCRIPT_BINARY_TOKENS_COMPRESSED ? GDScriptTokenizerBuffer::COMPRESS_ZSTD : GDScriptTokenizerBuffer::COMPRESS_NONE;
	file = GDScriptTokenizerBuffer::parse_code_string(source, compress_mode);
	if (file.is_empty()) {
		return;
	}

	add_file(p_path.get_basename() + ".gdc", file, true);
}

void EditorExportGDScript::_validate_exported_file(const String &p_source_path, const String &p_exported_path, const String &p_type, const Vector<uint8_t> *p_in_memory_data) {
	if (!_is_compiled_bytecode_mode(script_mode)) {
		return;
	}

	if (_contains_embedded_gdscript(p_exported_path, p_in_memory_data)) {
		fail_export(
				ERR_INVALID_DATA,
				TTR("Compiled Script Export"),
				vformat(TTR("\"%s\" contains a built-in GDScript, which cannot be exported as compiled bytecode. Save the script to its own .gd file."), p_source_path));
	}
}

void EditorExportGDScript::_export_end() {
	_reset_compile_session();
	export_debug = false;
	script_mode = DEFAULT_SCRIPT_MODE;
}

#endif // TOOLS_ENABLED
