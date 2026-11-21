/**************************************************************************/
/*  gdscript_editor_export_plugin.h                                       */
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

#ifdef TOOLS_ENABLED

#include "editor/export/editor_export_plugin.h"

class GDScriptExportCompileSession;

class EditorExportGDScript : public EditorExportPlugin {
	GDCLASS(EditorExportGDScript, EditorExportPlugin);

	static constexpr EditorExportPreset::ScriptExportMode DEFAULT_SCRIPT_MODE = EditorExportPreset::MODE_SCRIPT_BINARY_TOKENS_COMPRESSED;

	EditorExportPreset::ScriptExportMode script_mode = DEFAULT_SCRIPT_MODE;
	bool export_debug = false;
	GDScriptExportCompileSession *compile_session = nullptr;

	void _reset_compile_session();
	String _format_compile_error(const String &p_path, Error p_fallback_error) const;
	void _export_file_compiled_bytecode(const String &p_path);

	static bool _is_gdscript_class_name(const StringName &p_class_name);
	static bool _classes_contain_gdscript(const HashSet<StringName> &p_classes);
	static bool _contains_embedded_gdscript(const String &p_exported_path, const Vector<uint8_t> *p_in_memory_data);

protected:
	virtual void _export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) override;
	virtual void _export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features) override;
	virtual void _validate_exported_file(const String &p_source_path, const String &p_exported_path, const String &p_type, const Vector<uint8_t> *p_in_memory_data = nullptr) override;
	virtual void _export_end() override;

public:
	virtual String get_name() const override { return "GDScript"; }

	virtual ~EditorExportGDScript();
};

#endif // TOOLS_ENABLED
