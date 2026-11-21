/**************************************************************************/
/*  gdscript_bytecode_verifier.h                                          */
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
#include "gdscript_function.h"

#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

class GDScriptBytecodeVerifier {
	struct Impl;

public:
	using GlobalStoreOffsetsMap = HashMap<const GDScriptFunction *, Vector<int>>;

	static Error verify_function(
			const GDScriptFunction *p_function,
			const GDScript *p_owning_script,
			const GlobalStoreOffsetsMap *p_global_store_offsets = nullptr,
			uint32_t p_lambda_depth = 0,
			String *r_error_message = nullptr);

	static Error verify_class(
			const GDScript *p_script,
			const GlobalStoreOffsetsMap *p_global_store_offsets = nullptr,
			String *r_error_message = nullptr);

	static Error verify_script_tree(
			const GDScript *p_root_script,
			const GlobalStoreOffsetsMap *p_global_store_offsets = nullptr,
			String *r_error_message = nullptr);
};
