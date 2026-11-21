"""Build-time semantic ABI digest generator for compiled GDScript bytecode."""

from __future__ import annotations

import hashlib
import re

import methods

CPP_TYPE_TO_VARIANT_TYPE = {
    "bool": "BOOL",
    "int64_t": "INT",
    "double": "FLOAT",
    "String": "STRING",
    "Vector2": "VECTOR2",
    "Vector2i": "VECTOR2I",
    "Rect2": "RECT2",
    "Rect2i": "RECT2I",
    "Vector3": "VECTOR3",
    "Vector3i": "VECTOR3I",
    "Transform2D": "TRANSFORM2D",
    "Vector4": "VECTOR4",
    "Vector4i": "VECTOR4I",
    "Plane": "PLANE",
    "Quaternion": "QUATERNION",
    "AABB": "AABB",
    "Basis": "BASIS",
    "Transform3D": "TRANSFORM3D",
    "Projection": "PROJECTION",
    "Color": "COLOR",
    "StringName": "STRING_NAME",
    "NodePath": "NODE_PATH",
    "RID": "RID",
    "Callable": "CALLABLE",
    "Signal": "SIGNAL",
    "Dictionary": "DICTIONARY",
    "Array": "ARRAY",
    "PackedByteArray": "PACKED_BYTE_ARRAY",
    "PackedInt32Array": "PACKED_INT32_ARRAY",
    "PackedInt64Array": "PACKED_INT64_ARRAY",
    "PackedFloat32Array": "PACKED_FLOAT32_ARRAY",
    "PackedFloat64Array": "PACKED_FLOAT64_ARRAY",
    "PackedStringArray": "PACKED_STRING_ARRAY",
    "PackedVector2Array": "PACKED_VECTOR2_ARRAY",
    "PackedVector3Array": "PACKED_VECTOR3_ARRAY",
    "PackedColorArray": "PACKED_COLOR_ARRAY",
    "PackedVector4Array": "PACKED_VECTOR4_ARRAY",
}

FIXED_CONSTRUCTOR_TEMPLATES = {
    "VariantConstructNoArgsNil": "NIL",
    "VariantConstructorNil": "NIL",
    "VariantConstructNoArgsObject": "OBJECT",
    "VariantConstructorObject": "OBJECT",
    "VariantConstructorNilObject": "OBJECT",
    "VariantConstructorCallableArgs": "CALLABLE",
    "VariantConstructorSignalArgs": "SIGNAL",
    "VariantConstructorTypedDictionary": "DICTIONARY",
    "VariantConstructorTypedArray": "ARRAY",
}

FIRST_ARG_CONSTRUCTOR_TEMPLATES = {
    "VariantConstructNoArgs",
    "VariantConstructor",
    "VariantConstructorFromString",
    "VariantConstructorFromArray",
}


def strip_cpp_comments(source_text: str) -> str:
    """Remove C and C++ comments while preserving string literals and newlines."""
    result = []
    index = 0
    length = len(source_text)
    while index < length:
        character = source_text[index]
        if character in ('"', "'"):
            quote = character
            result.append(character)
            index += 1
            while index < length:
                inner = source_text[index]
                result.append(inner)
                index += 1
                if inner == "\\" and index < length:
                    result.append(source_text[index])
                    index += 1
                elif inner == quote:
                    break
            continue
        if character == "/" and index + 1 < length:
            next_character = source_text[index + 1]
            if next_character == "/":
                index += 2
                while index < length and source_text[index] != "\n":
                    index += 1
                continue
            if next_character == "*":
                index += 2
                while index + 1 < length and not (source_text[index] == "*" and source_text[index + 1] == "/"):
                    if source_text[index] == "\n":
                        result.append("\n")
                    index += 1
                if index + 1 >= length:
                    raise ValueError("Unterminated block comment while parsing C++ source.")
                index += 2
                continue
        result.append(character)
        index += 1
    return "".join(result)


def strip_preprocessor_lines(source_text: str) -> str:
    """Remove preprocessor directive lines (including backslash-continued lines)."""
    output_lines = []
    skipping_continuation = False
    for line in source_text.splitlines():
        if skipping_continuation:
            skipping_continuation = line.rstrip().endswith("\\")
            continue
        if line.lstrip().startswith("#"):
            skipping_continuation = line.rstrip().endswith("\\")
            continue
        output_lines.append(line)
    return "\n".join(output_lines)


def normalize_whitespace(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def find_balanced_span(text: str, open_index: int, open_char: str, close_char: str) -> int:
    """Return index of matching close_char for open_char at open_index."""
    depth = 0
    index = open_index
    length = len(text)
    while index < length:
        character = text[index]
        if character in ('"', "'"):
            quote = character
            index += 1
            while index < length:
                inner = text[index]
                index += 1
                if inner == "\\" and index < length:
                    index += 1
                elif inner == quote:
                    break
            continue
        if character == open_char:
            depth += 1
        elif character == close_char:
            depth -= 1
            if depth == 0:
                return index
        index += 1
    raise ValueError(f"Unbalanced '{open_char}'...'{close_char}' starting at offset {open_index}.")


def split_top_level(text: str, delimiter: str = ",") -> list[str]:
    """Split text by delimiter outside parentheses, angle brackets, and quotes."""
    parts = []
    current = []
    paren_depth = 0
    angle_depth = 0
    index = 0
    length = len(text)
    while index < length:
        character = text[index]
        if character in ('"', "'"):
            quote = character
            current.append(character)
            index += 1
            while index < length:
                inner = text[index]
                current.append(inner)
                index += 1
                if inner == "\\" and index < length:
                    current.append(text[index])
                    index += 1
                elif inner == quote:
                    break
            continue
        if character == "(":
            paren_depth += 1
        elif character == ")":
            paren_depth -= 1
        elif character == "<":
            angle_depth += 1
        elif character == ">":
            angle_depth -= 1
        elif character == delimiter and paren_depth == 0 and angle_depth == 0:
            parts.append("".join(current).strip())
            current = []
            index += 1
            continue
        current.append(character)
        index += 1
    tail = "".join(current).strip()
    if tail or parts:
        parts.append(tail)
    return parts


def extract_opcode_layout(opcode_layout_text: str) -> list[str]:
    """Extract canonical macro descriptors from gdscript_opcode_layout.inc."""
    cleaned = strip_preprocessor_lines(strip_cpp_comments(opcode_layout_text))
    descriptors = []
    index = 0
    length = len(cleaned)
    while index < length:
        while index < length and cleaned[index].isspace():
            index += 1
        if index >= length:
            break
        match = re.match(r"(GDSCRIPT_OPCODE_FIXED|GDSCRIPT_OPCODE_VARARG)\s*\(", cleaned[index:])
        if not match:
            snippet = cleaned[index : index + 60].replace("\n", " ")
            raise ValueError(f"Unrecognized statement in gdscript_opcode_layout.inc near: {snippet!r}")
        macro_name = match.group(1)
        open_paren = index + match.end() - 1
        close_paren = find_balanced_span(cleaned, open_paren, "(", ")")
        raw_args = cleaned[open_paren + 1 : close_paren]
        normalized_args = normalize_whitespace(raw_args)
        descriptors.append(f"{macro_name}({normalized_args})")
        index = close_paren + 1

    if not descriptors:
        raise ValueError("No opcode macros found in gdscript_opcode_layout.inc.")
    if not descriptors[-1].startswith("GDSCRIPT_OPCODE_FIXED(OPCODE_END,"):
        raise ValueError("Expected OPCODE_END as the final entry in gdscript_opcode_layout.inc.")
    return descriptors


def extract_enum_members(cleaned_text: str, enum_name: str, scope_hint: str | None = None) -> list[str]:
    """Extract canonical ordered member descriptors from a C++ enum definition."""
    search_text = cleaned_text
    if scope_hint is not None:
        scope_match = re.search(scope_hint, search_text)
        if not scope_match:
            raise ValueError(f"Could not find scope matching {scope_hint!r} for enum {enum_name}.")
        search_text = search_text[scope_match.start() :]

    pattern = re.compile(rf"\benum\s+(?:class\s+)?{re.escape(enum_name)}\b(?:\s*:\s*[^{{]+)?\s*\{{")
    match = pattern.search(search_text)
    if not match:
        raise ValueError(f"Could not find enum definition '{enum_name}'.")
    open_brace = match.end() - 1
    close_brace = find_balanced_span(search_text, open_brace, "{", "}")
    body = search_text[open_brace + 1 : close_brace]

    members = []
    for raw_item in split_top_level(body, ","):
        item = normalize_whitespace(raw_item)
        if not item:
            continue
        members.append(item)
    if not members:
        raise ValueError(f"Enum '{enum_name}' has no members.")
    return members


def extract_format_schema(format_header_text: str) -> list[str]:
    """Extract wire-format constants and enum schemas from gdscript_bytecode_format.h."""
    cleaned = strip_preprocessor_lines(strip_cpp_comments(format_header_text))
    descriptors = []

    for constant_name in ("FORMAT_VERSION", "OPERATOR_INLINE_CACHE_PTR_WORDS"):
        constant_match = re.search(
            rf"\b{re.escape(constant_name)}\s*=\s*([^;]+);",
            cleaned,
        )
        if not constant_match:
            raise ValueError(f"Missing required constant '{constant_name}' in gdscript_bytecode_format.h.")
        descriptors.append(f"CONST:{constant_name}={normalize_whitespace(constant_match.group(1))}")

    for enum_name in (
        "ScriptFlags",
        "CompressionMode",
        "ClassFlags",
        "ScriptRefLocality",
        "BaseClassKind",
        "SectionId",
        "VariantTag",
        "FixupTable",
    ):
        members = extract_enum_members(cleaned, enum_name)
        descriptors.append(f"ENUM:{enum_name}:" + ",".join(members))

    return descriptors


def extract_variant_enums(variant_header_text: str) -> tuple[list[str], list[str]]:
    """Extract ordered Variant::Type and Variant::Operator member names from core/variant/variant.h."""
    cleaned = strip_preprocessor_lines(strip_cpp_comments(variant_header_text))
    type_members = extract_enum_members(cleaned, "Type", scope_hint=r"\bclass\s+Variant\b")
    operator_members = extract_enum_members(cleaned, "Operator", scope_hint=r"\bclass\s+Variant\b")

    type_names = [member.split("=")[0].strip() for member in type_members]
    operator_names = [member.split("=")[0].strip() for member in operator_members]

    if not type_names or type_names[0] != "NIL" or type_names[-1] != "VARIANT_MAX":
        raise ValueError("Unexpected Variant::Type enum bounds in core/variant/variant.h.")
    if not operator_names or operator_names[-1] != "OP_MAX":
        raise ValueError("Unexpected Variant::Operator enum bounds in core/variant/variant.h.")

    return type_names, operator_names


def resolve_constructor_base_type(template_argument: str) -> str:
    """Resolve the Variant::Type name registered by an add_constructor<...> template class."""
    normalized = normalize_whitespace(template_argument)
    if normalized in FIXED_CONSTRUCTOR_TEMPLATES:
        return FIXED_CONSTRUCTOR_TEMPLATES[normalized]

    angle_open = normalized.find("<")
    if angle_open == -1 or not normalized.endswith(">"):
        raise ValueError(f"Unrecognized Variant constructor template: {template_argument!r}")

    template_name = normalized[:angle_open].strip()
    inner_args_text = normalized[angle_open + 1 : -1].strip()
    template_args = [arg.strip() for arg in split_top_level(inner_args_text, ",") if arg.strip()]
    if not template_args:
        raise ValueError(f"Empty template arguments in Variant constructor: {template_argument!r}")

    if template_name == "VariantConstructorToArray":
        return "ARRAY"

    if template_name in FIRST_ARG_CONSTRUCTOR_TEMPLATES:
        base_cpp_type = template_args[0].lstrip(":").strip()
        if base_cpp_type not in CPP_TYPE_TO_VARIANT_TYPE:
            raise ValueError(
                f"Unrecognized C++ base type {base_cpp_type!r} in Variant constructor {template_argument!r}"
            )
        return CPP_TYPE_TO_VARIANT_TYPE[base_cpp_type]

    raise ValueError(f"Unrecognized Variant constructor template class {template_name!r} in {template_argument!r}")


def extract_variant_constructors(
    variant_construct_text: str,
    variant_type_names: list[str],
) -> tuple[list[str], dict[str, int]]:
    """Extract ordered add_constructor registrations inside Variant::_register_variant_constructors()."""
    cleaned = strip_cpp_comments(variant_construct_text)
    fn_match = re.search(r"\bvoid\s+Variant::_register_variant_constructors\s*\(\s*\)\s*\{", cleaned)
    if not fn_match:
        raise ValueError("Could not find Variant::_register_variant_constructors() in core/variant/variant_construct.cpp.")

    open_brace = fn_match.end() - 1
    close_brace = find_balanced_span(cleaned, open_brace, "{", "}")
    body = strip_preprocessor_lines(cleaned[open_brace + 1 : close_brace])

    valid_type_set = set(variant_type_names[:-1])  # Exclude VARIANT_MAX.
    constructor_counts = {type_name: 0 for type_name in variant_type_names[:-1]}
    descriptors = []

    index = 0
    length = len(body)
    while index < length:
        while index < length and body[index].isspace():
            index += 1
        if index >= length:
            break

        if not body[index:].startswith("add_constructor"):
            snippet = body[index : index + 80].replace("\n", " ")
            raise ValueError(
                f"Unrecognized statement inside Variant::_register_variant_constructors(): {snippet!r}"
            )

        after_name = index + len("add_constructor")
        while after_name < length and body[after_name].isspace():
            after_name += 1
        if after_name >= length or body[after_name] != "<":
            snippet = body[index : index + 80].replace("\n", " ")
            raise ValueError(f"Malformed add_constructor statement (missing '<'): {snippet!r}")

        angle_close = find_balanced_span(body, after_name, "<", ">")
        template_arg = normalize_whitespace(body[after_name + 1 : angle_close])

        after_angle = angle_close + 1
        while after_angle < length and body[after_angle].isspace():
            after_angle += 1
        if after_angle >= length or body[after_angle] != "(":
            snippet = body[index : index + 80].replace("\n", " ")
            raise ValueError(f"Malformed add_constructor statement (missing '('): {snippet!r}")

        paren_close = find_balanced_span(body, after_angle, "(", ")")
        call_arg = normalize_whitespace(body[after_angle + 1 : paren_close])

        after_paren = paren_close + 1
        while after_paren < length and body[after_paren].isspace():
            after_paren += 1
        if after_paren >= length or body[after_paren] != ";":
            snippet = body[index : index + 80].replace("\n", " ")
            raise ValueError(f"Malformed add_constructor statement (missing ';'): {snippet!r}")

        if not re.match(r"^sarray\s*\(.*\)$", call_arg):
            raise ValueError(
                f"Expected sarray(...) argument in add_constructor statement, got: {call_arg!r}"
            )

        base_variant_type = resolve_constructor_base_type(template_arg)
        if base_variant_type not in valid_type_set:
            raise ValueError(
                f"Resolved Variant::Type {base_variant_type!r} not found in Variant::Type enum."
            )

        constructor_index = constructor_counts[base_variant_type]
        constructor_counts[base_variant_type] = constructor_index + 1
        descriptors.append(f"CTOR:{base_variant_type}[{constructor_index}]={template_arg}({call_arg})")

        index = after_paren + 1

    if not descriptors:
        raise ValueError("Zero add_constructor statements found in Variant::_register_variant_constructors().")

    return descriptors, constructor_counts


def compute_gdscript_bytecode_abi(
    opcode_layout_text: str,
    format_header_text: str,
    variant_header_text: str,
    variant_construct_text: str,
    precision_flag: str,
    deprecated_flag: bool,
) -> tuple[bytes, list[tuple[str, int]]]:
    """Compute the 16-byte semantic ABI digest and ordered per-Variant::Type constructor counts."""
    opcode_descriptors = extract_opcode_layout(opcode_layout_text)
    format_descriptors = extract_format_schema(format_header_text)
    variant_type_names, variant_operator_names = extract_variant_enums(variant_header_text)
    constructor_descriptors, constructor_counts = extract_variant_constructors(
        variant_construct_text,
        variant_type_names,
    )

    canonical_lines = [
        f"ENV:precision={precision_flag}",
        f"ENV:deprecated={1 if deprecated_flag else 0}",
        "VARIANT_TYPES:" + ",".join(variant_type_names),
        "VARIANT_OPERATORS:" + ",".join(variant_operator_names),
    ]
    canonical_lines.extend(format_descriptors)
    canonical_lines.extend(opcode_descriptors)
    canonical_lines.extend(constructor_descriptors)

    canonical_payload = "\n".join(canonical_lines).encode("utf-8")
    digest_prefix = hashlib.sha256(canonical_payload).digest()[:16]
    ordered_counts = [(type_name, constructor_counts[type_name]) for type_name in variant_type_names[:-1]]
    return digest_prefix, ordered_counts


def make_gdscript_bytecode_abi_header(target, source, env):
    opcode_layout_path = str(source[0])
    format_header_path = str(source[1])
    variant_header_path = str(source[2])
    variant_construct_path = str(source[3])
    # source[4] is gdscript_bytecode_abi_builder.py (explicit SCons dependency).
    precision_flag = str(source[5].read())
    deprecated_flag = bool(source[6].read())

    with open(opcode_layout_path, "rt", encoding="utf-8") as file:
        opcode_layout_text = file.read()
    with open(format_header_path, "rt", encoding="utf-8") as file:
        format_header_text = file.read()
    with open(variant_header_path, "rt", encoding="utf-8") as file:
        variant_header_text = file.read()
    with open(variant_construct_path, "rt", encoding="utf-8") as file:
        variant_construct_text = file.read()

    abi_digest, ordered_counts = compute_gdscript_bytecode_abi(
        opcode_layout_text=opcode_layout_text,
        format_header_text=format_header_text,
        variant_header_text=variant_header_text,
        variant_construct_text=variant_construct_text,
        precision_flag=precision_flag,
        deprecated_flag=deprecated_flag,
    )

    abi_hex_bytes = ", ".join(f"0x{byte:02x}" for byte in abi_digest)
    constructor_count_lines = "\n".join(
        f"\t{count}, // Variant::{type_name}" for type_name, count in ordered_counts
    )

    with methods.generated_wrapper(str(target[0])) as file:
        file.write(
            f"""\
#include <cstdint>

#ifdef TESTS_ENABLED
#include "core/variant/variant.h"
#endif

inline constexpr uint8_t GDSCRIPT_BYTECODE_ABI_ID[16] = {{ {abi_hex_bytes} }};

#ifdef TESTS_ENABLED
inline constexpr uint32_t GDSCRIPT_BYTECODE_ABI_CONSTRUCTOR_COUNTS[Variant::VARIANT_MAX] = {{
{constructor_count_lines}
}};
#endif
"""
        )
