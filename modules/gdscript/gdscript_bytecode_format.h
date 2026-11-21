/**************************************************************************/
/*  gdscript_bytecode_format.h                                            */
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

#include <cstdint>

#if (defined(TOOLS_ENABLED) || defined(TESTS_ENABLED)) && !defined(GDSCRIPT_NO_FRONTEND)
#define GDSCRIPT_BYTECODE_EXPORT_ENABLED
#endif

namespace GDScriptBytecodeFormat {

inline constexpr uint8_t MAGIC[4] = { 'G', 'D', 'B', 'C' };
inline constexpr uint32_t FORMAT_VERSION = 1;

// Fixed cross-architecture word count reserved for the cached evaluator pointer in OPCODE_OPERATOR.
inline constexpr int OPERATOR_INLINE_CACHE_PTR_WORDS = 2;

enum ScriptFlags : uint32_t {
	SCRIPT_FLAG_HAS_STATIC_DATA = 1u << 0,
	SCRIPT_FLAG_ANNOTATED_STATIC_UNLOAD = 1u << 1,
	SCRIPT_FLAG_KNOWN_MASK = SCRIPT_FLAG_HAS_STATIC_DATA | SCRIPT_FLAG_ANNOTATED_STATIC_UNLOAD,
};

enum CompressionMode : uint8_t {
	COMPRESSION_NONE = 0,
	COMPRESSION_ZSTD = 1,
	COMPRESSION_MAX = 2,
};

enum ClassFlags : uint8_t {
	CLASS_FLAG_TOOL = 1u << 0,
	CLASS_FLAG_ABSTRACT = 1u << 1,
	CLASS_FLAG_KNOWN_MASK = CLASS_FLAG_TOOL | CLASS_FLAG_ABSTRACT,
};

enum ScriptRefLocality : uint8_t {
	SCRIPT_REF_EXTERNAL = 0,
	SCRIPT_REF_LOCAL = 1,
};

enum BaseClassKind : uint8_t {
	BASE_CLASS_NATIVE_ONLY = 0,
	BASE_CLASS_LOCAL = 1,
	BASE_CLASS_EXTERNAL = 2,
};

enum SectionId : uint32_t {
	SECTION_STRING_TABLE = 0,
	SECTION_DEPENDENCIES = 1,
	SECTION_SKELETON = 2,
	SECTION_CLASS_BODIES = 3,
	SECTION_COUNT = 4,
};

enum VariantTag : uint8_t {
	TAG_INLINE_VARIANT = 0,
	TAG_ARRAY = 1,
	TAG_DICTIONARY = 2,
	TAG_SCRIPT_REF = 3,
	TAG_EXTERNAL_SCRIPT = 4,
	TAG_EXTERNAL_RESOURCE = 5,
	TAG_NATIVE_CLASS = 6,
	TAG_ENGINE_SINGLETON = 7,
	TAG_UTILITY_CALLABLE = 8,
	TAG_NULL_OBJECT = 9,
	TAG_DEFAULT_VALUE = 10,
	TAG_MAX = 11,
};

enum FixupTable : uint32_t {
	FIXUP_OPERATOR = 0,
	FIXUP_SETTER = 1,
	FIXUP_GETTER = 2,
	FIXUP_KEYED_SETTER = 3,
	FIXUP_KEYED_GETTER = 4,
	FIXUP_INDEXED_SETTER = 5,
	FIXUP_INDEXED_GETTER = 6,
	FIXUP_BUILTIN_METHOD = 7,
	FIXUP_CONSTRUCTOR = 8,
	FIXUP_UTILITY = 9,
	FIXUP_GDS_UTILITY = 10,
	FIXUP_METHOD_BIND = 11,
	FIXUP_TABLE_COUNT = 12,
};

// VM execution and bytecode decoding ceilings.
inline constexpr int32_t MAX_STACK_SIZE = 4096;
inline constexpr int32_t MAX_INSTRUCTION_ARGS_SIZE = 4096;
inline constexpr int32_t MAX_ARGUMENT_COUNT = 256;
inline constexpr uint32_t MAX_CODE_WORDS = 1u << 20;
inline constexpr uint32_t MAX_FRAME_ALLOCA_BYTES = 256 * 1024;
inline constexpr uint32_t MAX_CLASS_DEPTH = 64;
inline constexpr uint32_t MAX_TYPE_DEPTH = 32;
inline constexpr uint32_t MAX_LAMBDA_DEPTH = 32;
inline constexpr uint32_t MAX_UNCOMPRESSED_PAYLOAD_BYTES = 64u * 1024u * 1024u;
inline constexpr uint64_t MIN_DECOMPRESSION_ALLOWANCE = 64u * 1024u;
inline constexpr uint64_t MAX_DECOMPRESSION_RATIO = 64u;

inline constexpr int MAX_OPCODE_OPERANDS = 8;

enum OpcodeOperandKind : uint8_t {
	OPERAND_NONE = 0,
	OPERAND_ADDRESS,
	OPERAND_JUMP_TARGET,
	OPERAND_GLOBAL_NAME,
	OPERAND_VARIANT_TYPE,
	OPERAND_VARIANT_OPERATOR,
	OPERAND_OPERATOR_CACHE_ZERO,
	OPERAND_STATIC_VARIABLE_INDEX,
	OPERAND_GLOBAL_INDEX,
	OPERAND_LINE_NUMBER,
	OPERAND_LAMBDA_INDEX,
	OPERAND_VARARG_COUNT,
	OPERAND_FIXUP_OPERATOR,
	OPERAND_FIXUP_SETTER,
	OPERAND_FIXUP_GETTER,
	OPERAND_FIXUP_KEYED_SETTER,
	OPERAND_FIXUP_KEYED_GETTER,
	OPERAND_FIXUP_INDEXED_SETTER,
	OPERAND_FIXUP_INDEXED_GETTER,
	OPERAND_FIXUP_BUILTIN_METHOD,
	OPERAND_FIXUP_CONSTRUCTOR,
	OPERAND_FIXUP_UTILITY,
	OPERAND_FIXUP_GDS_UTILITY,
	OPERAND_FIXUP_METHOD_BIND,
};

struct OpcodeLayout {
	bool is_vararg = false;
	// For fixed opcodes: total instruction words including the opcode word itself.
	// For vararg opcodes: trailing word count after the variable Address array.
	uint8_t word_count = 0;
	// For vararg opcodes: trailing word offset holding the argument/pair/capture count.
	uint8_t count_tail_offset = 0;
	// For vararg opcodes: instr_arg_count == count * count_multiplier + count_addend.
	uint8_t count_multiplier = 0;
	uint8_t count_addend = 0;
	// Number of valid operand entries in `operands`.
	uint8_t operand_count = 0;
	OpcodeOperandKind operands[MAX_OPCODE_OPERANDS] = {};
};

inline constexpr bool validate_opcode_layout_table(const OpcodeLayout *p_table, int p_count) {
	for (int index = 0; index < p_count; index++) {
		const OpcodeLayout &entry = p_table[index];
		if (!entry.is_vararg) {
			if (entry.word_count < 1 || entry.operand_count != entry.word_count - 1 || entry.operand_count > MAX_OPCODE_OPERANDS) {
				return false;
			}
		} else {
			if (entry.word_count < 1 || entry.operand_count != entry.word_count || entry.operand_count > MAX_OPCODE_OPERANDS) {
				return false;
			}
			if (entry.count_tail_offset >= entry.operand_count || entry.operands[entry.count_tail_offset] != OPERAND_VARARG_COUNT) {
				return false;
			}
			if (entry.count_multiplier == 0) {
				return false;
			}
		}
		for (int operand_index = 0; operand_index < entry.operand_count; operand_index++) {
			if (entry.operands[operand_index] == OPERAND_NONE) {
				return false;
			}
		}
		for (int operand_index = entry.operand_count; operand_index < MAX_OPCODE_OPERANDS; operand_index++) {
			if (entry.operands[operand_index] != OPERAND_NONE) {
				return false;
			}
		}
	}
	return true;
}

} // namespace GDScriptBytecodeFormat
