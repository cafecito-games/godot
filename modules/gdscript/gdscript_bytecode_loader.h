/**************************************************************************/
/*  gdscript_bytecode_loader.h                                            */
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

#include "gdscript.h"
#include "gdscript_bytecode_format.h"

#include "core/io/resource.h"
#include "core/templates/vector.h"

class GDScriptBytecodeExternalResolver {
public:
	virtual ~GDScriptBytecodeExternalResolver() = default;

	virtual Ref<Resource> resolve_resource(const String &p_path, const String &p_type_hint, Error &r_error) = 0;
	virtual Ref<GDScript> resolve_script(const String &p_path, const String &p_fully_qualified_name, bool &r_is_local_class, Error &r_error) = 0;
};

class GDScriptBytecodeLoader {
	class LoadContext;

public:
	struct HeaderInfo {
		uint32_t format_version = 0;
		uint8_t abi_id[16] = {};
		uint8_t real_t_size = 0;
		uint32_t opcode_end = 0;
		String diag_version_full_config;
		String diag_version_hash;
		uint32_t script_flags = 0;
	};

	static Error check_header(
			const Vector<uint8_t> &p_buffer,
			HeaderInfo *r_header = nullptr,
			String *r_error_message = nullptr);

	static Error read_dependencies(
			const Vector<uint8_t> &p_buffer,
			Vector<String> &r_dependencies,
			String *r_error_message = nullptr);

	static Error load_skeleton(
			const Vector<uint8_t> &p_buffer,
			const Ref<GDScript> &p_target_script,
			const String &p_logical_path,
			HeaderInfo *r_header = nullptr,
			String *r_error_message = nullptr);

	static Error load_full(
			const Vector<uint8_t> &p_buffer,
			const Ref<GDScript> &p_target_script,
			const String &p_logical_path,
			GDScriptBytecodeExternalResolver *p_resolver,
			HeaderInfo *r_header = nullptr,
			String *r_error_message = nullptr);

	static Error initialize_static_data(const Ref<GDScript> &p_script);
};
