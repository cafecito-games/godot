/**************************************************************************/
/*  gdscript_bytecode_export.h                                            */
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

#include "gdscript_bytecode_format.h"

#ifdef GDSCRIPT_BYTECODE_EXPORT_ENABLED

#include "gdscript.h"

#include "core/string/string_name.h"
#include "core/templates/vector.h"

class GDScriptBytecodeExporter {
	class ExportContext;

public:
	struct Options {
		bool has_static_data = false;
		bool annotated_static_unload = false;
		bool release_profile = false;
		bool emit_line_opcodes = true;
		bool auto_detect_static_data = true;
	};

	static bool detect_static_data(const Ref<GDScript> &p_script);
	static Vector<StringName> collect_unsupported_named_globals(const Ref<GDScript> &p_script);

	static Error serialize(
			const Ref<GDScript> &p_script,
			Vector<uint8_t> &r_buffer,
			const Options &p_options,
			String *r_error_message = nullptr);

	static Error serialize(
			const Ref<GDScript> &p_script,
			Vector<uint8_t> &r_buffer,
			String *r_error_message = nullptr) {
		return serialize(p_script, r_buffer, Options(), r_error_message);
	}
};

#endif // GDSCRIPT_BYTECODE_EXPORT_ENABLED
