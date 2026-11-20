/**************************************************************************/
/*  gdscript_export_compile_session.cpp                                   */
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

#include "gdscript_export_compile_session.h"

#if defined(TOOLS_ENABLED) || defined(TESTS_ENABLED)

#include "gdscript_compiler.h"
#include "gdscript_parser.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/templates/vector.h"

namespace {

struct ScopedPathSetEntry {
	HashSet<String> &set;
	String path;

	ScopedPathSetEntry(HashSet<String> &p_set, const String &p_path) :
			set(p_set), path(p_path) {
		set.insert(path);
	}

	~ScopedPathSetEntry() {
		set.erase(path);
	}
};

struct ScopedPreloadCleanup {
	HashSet<String> &in_progress_set;
	HashMap<String, Ref<Resource>> &ref_override_map;
	String path;

	ScopedPreloadCleanup(HashSet<String> &p_in_progress_set, HashMap<String, Ref<Resource>> &p_ref_override_map, const String &p_path) :
			in_progress_set(p_in_progress_set), ref_override_map(p_ref_override_map), path(p_path) {
		in_progress_set.insert(path);
	}

	~ScopedPreloadCleanup() {
		in_progress_set.erase(path);
		ref_override_map.erase(path);
	}
};

} // namespace

GDScriptExportCompileSession::GDScriptExportCompileSession(bool p_release_profile) :
		release_profile(p_release_profile),
		emit_debug_opcodes(!p_release_profile),
		autoloads_as_runtime_globals(true) {
	bool always_track_call_stacks = false;
	if (ProjectSettings::get_singleton() != nullptr && ProjectSettings::get_singleton()->has_setting("debug/settings/gdscript/always_track_call_stacks")) {
		always_track_call_stacks = GLOBAL_GET("debug/settings/gdscript/always_track_call_stacks");
	}
	emit_line_opcodes = !release_profile || always_track_call_stacks;
}

GDScriptExportCompileSession::~GDScriptExportCompileSession() {
	clear();
}

void GDScriptExportCompileSession::clear() {
	for (KeyValue<String, Ref<GDScriptParserRef>> &entry : parser_map) {
		if (entry.value.is_valid()) {
			entry.value->clear();
		}
	}
	parser_map.clear();

	Vector<Ref<GDScript>> scripts_to_clear;
	scripts_to_clear.reserve(shallow_scripts.size() + full_scripts.size());
	for (const KeyValue<String, Ref<GDScript>> &entry : shallow_scripts) {
		if (entry.value.is_valid()) {
			scripts_to_clear.push_back(entry.value);
		}
	}
	for (const KeyValue<String, Ref<GDScript>> &entry : full_scripts) {
		if (entry.value.is_valid()) {
			scripts_to_clear.push_back(entry.value);
		}
	}

	shallow_scripts.clear();
	full_scripts.clear();
	dependencies.clear();
	annotated_static_unload_flags.clear();
	preload_ref_overrides.clear();
	in_progress_preloads.clear();
	preloaded_resources.clear();
	compiling_scripts.clear();
	_clear_last_error();

	for (Ref<GDScript> &script : scripts_to_clear) {
		script->clear();
	}
	scripts_to_clear.clear();
}

void GDScriptExportCompileSession::_clear_last_error() {
	last_error_message = String();
	last_error_path = String();
	last_error_line = -1;
	last_error_column = -1;
}

void GDScriptExportCompileSession::_record_parser_error(const String &p_path, const Ref<GDScriptParserRef> &p_parser_ref) {
	if (!last_error_message.is_empty()) {
		return;
	}
	last_error_path = p_path;
	if (p_parser_ref.is_valid() && p_parser_ref->get_parser() != nullptr && !p_parser_ref->get_parser()->get_errors().is_empty()) {
		const GDScriptParser::ParserError &first_error = p_parser_ref->get_parser()->get_errors().front()->get();
		last_error_message = first_error.message;
		last_error_line = first_error.start_line;
		last_error_column = first_error.start_column;
	}
}

Ref<GDScript> GDScriptExportCompileSession::get_or_compile(const String &p_path, Error &r_error) {
	_clear_last_error();
	GDScriptCache::ActiveSessionGuard session_guard(this);
	return get_full_script(p_path, r_error);
}

Ref<GDScriptParserRef> GDScriptExportCompileSession::get_parser(const String &p_path, GDScriptParserRef::Status p_status, Error &r_error, const String &p_owner) {
	Ref<GDScriptParserRef> parser_ref;
	if (!p_owner.is_empty() && p_path != p_owner) {
		dependencies[p_owner].insert(p_path);
	}

	HashMap<String, Ref<GDScriptParserRef>>::Iterator existing = parser_map.find(p_path);
	if (existing) {
		parser_ref = existing->value;
		if (parser_ref.is_null()) {
			r_error = ERR_INVALID_DATA;
			return parser_ref;
		}
	} else {
		String remapped_path = ResourceLoader::path_remap(p_path);
		if (!FileAccess::exists(remapped_path)) {
			r_error = ERR_FILE_NOT_FOUND;
			return parser_ref;
		}
		parser_ref.instantiate();
		parser_ref->path = p_path;
		parser_ref->detached = true;
		parser_map[p_path] = parser_ref;
	}

	r_error = parser_ref->raise_status(p_status);
	if (r_error != OK) {
		_record_parser_error(p_path, parser_ref);
	}
	return parser_ref;
}

bool GDScriptExportCompileSession::has_parser(const String &p_path) const {
	return parser_map.has(p_path);
}

void GDScriptExportCompileSession::remove_parser(const String &p_path) {
	parser_map.erase(p_path);
}

Ref<GDScript> GDScriptExportCompileSession::get_shallow_script(const String &p_path, Error &r_error, const String &p_owner) {
	if (!p_owner.is_empty() && p_path != p_owner) {
		dependencies[p_owner].insert(p_path);
	}

	HashMap<String, Ref<GDScript>>::Iterator full_entry = full_scripts.find(p_path);
	if (full_entry) {
		r_error = OK;
		return full_entry->value;
	}

	HashMap<String, Ref<GDScript>>::Iterator shallow_entry = shallow_scripts.find(p_path);
	if (shallow_entry) {
		r_error = OK;
		return shallow_entry->value;
	}

	const String remapped_path = ResourceLoader::path_remap(p_path);

	Ref<GDScript> script;
	script.instantiate();
	script->set_detached_export_script(true);
	// Detached scripts use set_path_cache() exclusively so global ResourceCache is never modified.
	script->set_path_cache(p_path);

	if (remapped_path.has_extension("gdc")) {
		Vector<uint8_t> binary_buffer = GDScriptCache::get_binary_tokens(remapped_path);
		if (binary_buffer.is_empty()) {
			r_error = ERR_FILE_CANT_READ;
		} else {
			r_error = OK;
			script->set_binary_tokens_source(binary_buffer);
		}
	} else {
		r_error = script->load_source_code(remapped_path);
	}

	if (r_error != OK) {
		return Ref<GDScript>();
	}

	Ref<GDScriptParserRef> parser_ref = get_parser(p_path, GDScriptParserRef::PARSED, r_error);
	if (r_error != OK || parser_ref.is_null()) {
		_record_parser_error(p_path, parser_ref);
		return Ref<GDScript>();
	}

	GDScriptCompiler::make_scripts(script.ptr(), parser_ref->get_parser()->get_tree(), false);
	shallow_scripts[p_path] = script;
	return script;
}

Ref<GDScript> GDScriptExportCompileSession::get_full_script(const String &p_path, Error &r_error, const String &p_owner) {
	if (!p_owner.is_empty() && p_path != p_owner) {
		dependencies[p_owner].insert(p_path);
	}

	r_error = OK;
	HashMap<String, Ref<GDScript>>::Iterator full_entry = full_scripts.find(p_path);
	if (full_entry) {
		return full_entry->value;
	}

	Ref<GDScript> script = get_shallow_script(p_path, r_error);
	if (script.is_null() || r_error != OK) {
		return Ref<GDScript>();
	}

	if (compiling_scripts.has(p_path)) {
		return script;
	}

	ScopedPathSetEntry compiling_guard(compiling_scripts, p_path);

	Ref<GDScriptParserRef> parser_ref = get_parser(p_path, GDScriptParserRef::FULLY_SOLVED, r_error);
	if (parser_ref.is_null() || r_error != OK) {
		_record_parser_error(p_path, parser_ref);
		full_scripts.erase(p_path);
		shallow_scripts.erase(p_path);
		return Ref<GDScript>();
	}

	const GDScriptParser::ClassNode *root_class = parser_ref->get_parser()->get_tree();
	if (root_class != nullptr) {
		annotated_static_unload_flags[p_path] = root_class->annotated_static_unload;
	}

	GDScriptCompiler compiler;
	r_error = compiler.compile(parser_ref->get_parser(), script.ptr(), false);
	if (r_error != OK) {
		if (last_error_message.is_empty()) {
			last_error_path = p_path;
			last_error_message = compiler.get_error();
			last_error_line = compiler.get_error_line();
			last_error_column = compiler.get_error_column();
		}
		full_scripts.erase(p_path);
		shallow_scripts.erase(p_path);
		return Ref<GDScript>();
	}

	full_scripts[p_path] = script;
	shallow_scripts.erase(p_path);
	return script;
}

Ref<GDScript> GDScriptExportCompileSession::get_cached_script(const String &p_path) const {
	HashMap<String, Ref<GDScript>>::ConstIterator full_entry = full_scripts.find(p_path);
	if (full_entry) {
		return full_entry->value;
	}

	HashMap<String, Ref<GDScript>>::ConstIterator shallow_entry = shallow_scripts.find(p_path);
	if (shallow_entry) {
		return shallow_entry->value;
	}

	return Ref<GDScript>();
}

Error GDScriptExportCompileSession::finish_compiling(const String &p_owner) {
	Ref<GDScript> script = get_cached_script(p_owner);
	if (script.is_valid()) {
		full_scripts[p_owner] = script;
		shallow_scripts.erase(p_owner);
	}

	HashSet<String> owner_dependencies;
	HashMap<String, HashSet<String>>::Iterator dependency_entry = dependencies.find(p_owner);
	if (dependency_entry) {
		owner_dependencies = dependency_entry->value;
		dependencies.erase(p_owner);
	}

	Error first_error = OK;
	for (const String &dependency_path : owner_dependencies) {
		Error dependency_error = OK;
		get_full_script(dependency_path, dependency_error);
		if (dependency_error != OK && first_error == OK) {
			first_error = dependency_error;
		}
	}

	return first_error;
}

Ref<Resource> GDScriptExportCompileSession::resolve_preload_resource(const String &p_resolved_path, const String &p_resource_type, Error &r_error) {
	r_error = OK;
	const String normalized_path = ResourceLoader::validate_local_path(p_resolved_path);
	if (normalized_path.is_empty()) {
		r_error = ERR_INVALID_PARAMETER;
		return Ref<Resource>();
	}

	HashMap<String, Ref<Resource>>::Iterator cached_session_entry = preloaded_resources.find(normalized_path);
	if (cached_session_entry) {
		return cached_session_entry->value;
	}

	// Reuse already-cached editor resources read-only without modifying global ResourceCache.
	Ref<Resource> live_cached_resource = ResourceCache::get_ref(normalized_path);
	if (live_cached_resource.is_valid()) {
		return live_cached_resource;
	}

	// Cycle case: instantiate (or reuse) a typed placeholder populated in place by the outer load.
	if (in_progress_preloads.has(normalized_path)) {
		HashMap<String, Ref<Resource>>::Iterator existing_override = preload_ref_overrides.find(normalized_path);
		if (existing_override) {
			return existing_override->value;
		}

		Object *instance = ClassDB::instantiate(p_resource_type);
		if (instance == nullptr) {
			r_error = ERR_CANT_CREATE;
			return Ref<Resource>();
		}

		Ref<Resource> placeholder(Object::cast_to<Resource>(instance));
		if (placeholder.is_null()) {
			memdelete(instance);
			r_error = ERR_INVALID_DATA;
			return Ref<Resource>();
		}

		placeholder->set_path_cache(normalized_path);
		preload_ref_overrides[normalized_path] = placeholder;
		return placeholder;
	}

	ScopedPreloadCleanup preload_cleanup(in_progress_preloads, preload_ref_overrides, normalized_path);
	ResourceLoader::ExternalRefOverrideGuard ref_override_guard(&preload_ref_overrides);

	Ref<ResourceLoader::LoadToken> load_token = ResourceLoader::_load_start(
			normalized_path,
			p_resource_type,
			ResourceLoader::LOAD_THREAD_FROM_CURRENT,
			ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP);
	if (load_token.is_null()) {
		r_error = FAILED;
		return Ref<Resource>();
	}

	Ref<Resource> loaded_resource = ResourceLoader::_load_complete(*load_token.ptr(), &r_error);
	if (loaded_resource.is_null()) {
		if (r_error == OK) {
			r_error = FAILED;
		}
		return Ref<Resource>();
	}

	loaded_resource->set_path_cache(normalized_path);
	preloaded_resources[normalized_path] = loaded_resource;
	return loaded_resource;
}

bool GDScriptExportCompileSession::has_annotated_static_unload(const String &p_path) const {
	HashMap<String, bool>::ConstIterator entry = annotated_static_unload_flags.find(p_path);
	return entry ? entry->value : false;
}

#endif // defined(TOOLS_ENABLED) || defined(TESTS_ENABLED)
