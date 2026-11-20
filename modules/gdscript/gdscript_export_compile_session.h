/**************************************************************************/
/*  gdscript_export_compile_session.h                                     */
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

#if defined(TOOLS_ENABLED) || defined(TESTS_ENABLED)

#include "gdscript.h"
#include "gdscript_cache.h"

#include "core/io/resource.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

class GDScriptExportCompileSession {
	bool release_profile = true;
	bool emit_debug_opcodes = false;
	bool emit_line_opcodes = false;
	bool autoloads_as_runtime_globals = true;

	HashMap<String, Ref<GDScriptParserRef>> parser_map;
	HashMap<String, Ref<GDScript>> shallow_scripts;
	HashMap<String, Ref<GDScript>> full_scripts;
	HashMap<String, HashSet<String>> dependencies;
	HashMap<String, bool> annotated_static_unload_flags;
	HashMap<String, Ref<Resource>> preloaded_resources;
	HashSet<String> in_progress_preloads;
	HashMap<String, Ref<Resource>> preload_ref_overrides;
	HashSet<String> compiling_scripts;

	String last_error_message;
	String last_error_path;
	int last_error_line = -1;
	int last_error_column = -1;

	void _record_parser_error(const String &p_path, const Ref<GDScriptParserRef> &p_parser_ref);
	void _clear_last_error();

public:
	explicit GDScriptExportCompileSession(bool p_release_profile = true);
	~GDScriptExportCompileSession();

	GDScriptExportCompileSession(const GDScriptExportCompileSession &) = delete;
	GDScriptExportCompileSession &operator=(const GDScriptExportCompileSession &) = delete;

	void clear();

	bool is_release_profile() const { return release_profile; }
	bool should_emit_debug_opcodes() const { return emit_debug_opcodes; }
	bool should_emit_line_opcodes() const { return emit_line_opcodes; }
	bool should_compile_autoloads_as_runtime_globals() const { return autoloads_as_runtime_globals; }

	Ref<GDScript> get_or_compile(const String &p_path, Error &r_error);

	Ref<GDScriptParserRef> get_parser(const String &p_path, GDScriptParserRef::Status p_status, Error &r_error, const String &p_owner = String());
	bool has_parser(const String &p_path) const;
	void remove_parser(const String &p_path);
	Ref<GDScript> get_shallow_script(const String &p_path, Error &r_error, const String &p_owner = String());
	Ref<GDScript> get_full_script(const String &p_path, Error &r_error, const String &p_owner = String());
	Ref<GDScript> get_cached_script(const String &p_path) const;
	Error finish_compiling(const String &p_owner);

	Ref<Resource> resolve_preload_resource(const String &p_resolved_path, const String &p_resource_type, Error &r_error);

	bool has_annotated_static_unload(const String &p_path) const;
	String get_last_error_message() const { return last_error_message; }
	String get_last_error_path() const { return last_error_path; }
	int get_last_error_line() const { return last_error_line; }
	int get_last_error_column() const { return last_error_column; }
};

#endif // defined(TOOLS_ENABLED) || defined(TESTS_ENABLED)
