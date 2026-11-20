"""SCons helper that records a portable relink manifest for a built Godot target."""

from __future__ import annotations

import json
import os
import shlex
from collections.abc import Iterable
from pathlib import Path
from typing import Any


def _flatten_strings(env, value: Any) -> list[str]:
    if value is None:
        return []
    if isinstance(value, Iterable) and not isinstance(value, (str, bytes)):
        result: list[str] = []
        for item in value:
            result.extend(_flatten_strings(env, item))
        return result
    text = str(env.subst(str(value))).strip()
    if not text:
        return []
    return shlex.split(text)


def _flatten_raw_items(value: Any) -> list[Any]:
    if value is None:
        return []
    if isinstance(value, Iterable) and not isinstance(value, (str, bytes)):
        result: list[Any] = []
        for item in value:
            result.extend(_flatten_raw_items(item))
        return result
    return [value]


def _normalize_sdk_tokens(tokens: list[str], env) -> list[str]:
    vulkan_sdk = os.environ.get("VULKAN_SDK", "").rstrip("/")
    apple_toolchain = str(env.get("APPLE_TOOLCHAIN_PATH", "")).rstrip("/")
    android_ndk_root = str(env.get("ANDROID_NDK_ROOT", "")).rstrip("/")
    normalized: list[str] = []
    index = 0
    while index < len(tokens):
        token = tokens[index]
        if token == "-isysroot" and index + 1 < len(tokens):
            normalized.extend(["-isysroot", "__APPLE_SDK_ROOT__"])
            index += 2
            continue
        if token.startswith("-isysroot"):
            normalized.append("-isysroot__APPLE_SDK_ROOT__")
            index += 1
            continue
        if vulkan_sdk and vulkan_sdk in token:
            token = token.replace(vulkan_sdk, "__VULKAN_SDK__")
        if apple_toolchain and apple_toolchain in token:
            token = token.replace(apple_toolchain, "__APPLE_TOOLCHAIN_PATH__")
        if android_ndk_root and android_ndk_root in token:
            token = token.replace(android_ndk_root, "__ANDROID_NDK_ROOT__")
            for host_tag in ("darwin-x86_64", "linux-x86_64", "windows-x86_64"):
                token = token.replace(f"/prebuilt/{host_tag}/", "/prebuilt/__NDK_HOST_TAG__/")
        normalized.append(token)
        index += 1
    return normalized


def _format_cppdefines(env) -> list[str]:
    defines: list[str] = []
    for entry in _flatten_raw_items(env.get("CPPDEFINES", [])):
        if isinstance(entry, tuple) and len(entry) == 2:
            key, val = entry
            val_text = str(env.subst(str(val)))
            defines.append(f"-D{key}={val_text}")
        else:
            entry_text = str(env.subst(str(entry))).strip()
            if entry_text:
                defines.append(f"-D{entry_text}")
    return defines


def _classify_libraries(env, repo_root: Path, default_ext_lib_path: Path | None) -> tuple[list[str], list[str], str | None]:
    internal_static_libs: list[str] = []
    external_libs: list[str] = []
    default_ext_rel: str | None = None
    vulkan_sdk = os.environ.get("VULKAN_SDK", "").rstrip("/")

    for item in _flatten_raw_items(env.get("LIBS", [])):
        if hasattr(item, "abspath"):
            lib_path = Path(item.abspath).resolve()
            if default_ext_lib_path is not None and lib_path == default_ext_lib_path:
                default_ext_rel = os.path.relpath(lib_path, repo_root).replace("\\", "/")
                continue
            try:
                relative = lib_path.relative_to(repo_root).as_posix()
                internal_static_libs.append(relative)
            except ValueError:
                path_text = str(lib_path)
                if vulkan_sdk and vulkan_sdk in path_text:
                    path_text = path_text.replace(vulkan_sdk, "__VULKAN_SDK__")
                external_libs.append(path_text)
        else:
            name = str(env.subst(str(item))).strip()
            if not name:
                continue
            candidate = Path(name)
            if candidate.is_absolute() and candidate.exists():
                resolved = candidate.resolve()
                if default_ext_lib_path is not None and resolved == default_ext_lib_path:
                    default_ext_rel = os.path.relpath(resolved, repo_root).replace("\\", "/")
                    continue
                try:
                    internal_static_libs.append(resolved.relative_to(repo_root).as_posix())
                    continue
                except ValueError:
                    path_text = str(resolved)
                    if vulkan_sdk and vulkan_sdk in path_text:
                        path_text = path_text.replace(vulkan_sdk, "__VULKAN_SDK__")
                    external_libs.append(path_text)
                    continue
            external_libs.append(f"-l{name}" if not name.startswith("-") else name)

    return internal_static_libs, external_libs, default_ext_rel


def _collect_compile_metadata(env, target_filename: str = "") -> dict[str, Any]:
    raw_cxx = str(env.subst("$CXX")).strip()
    cxx_tokens = _normalize_sdk_tokens(shlex.split(raw_cxx), env) if raw_cxx else []
    cxx = " ".join(cxx_tokens)
    ccflags = _normalize_sdk_tokens(_flatten_strings(env, env.get("CCFLAGS", [])), env)
    cxxflags = _normalize_sdk_tokens(_flatten_strings(env, env.get("CXXFLAGS", [])), env)

    # Filter out warning flags and repository-local include flags that are unnecessary
    # when compiling self-contained extension units against the exported ABI headers.
    kept_flags: list[str] = []
    skip_next = False
    for token in ccflags + cxxflags:
        if skip_next:
            skip_next = False
            continue
        if token in ("-include", "-imacros", "-I", "-isystem"):
            skip_next = True
            continue
        if token.startswith(("-W", "-I", "-D")):
            continue
        if token in ("-c", "-g", "-g1", "-g2", "-g3"):
            continue
        kept_flags.append(token)

    android_ndk_root = str(env.get("ANDROID_NDK_ROOT", "")).rstrip("/")
    android_ndk_version = Path(android_ndk_root).name if android_ndk_root else ""
    is_ios_simulator = bool(
        env.get("simulator")
        or env.get("ios_simulator")
        or ".simulator." in target_filename
    )

    return {
        "cxx": cxx,
        "flags": kept_flags,
        "defines": _format_cppdefines(env),
        "ios_simulator": is_ios_simulator,
        "android_ndk_version": android_ndk_version,
    }


def write_relink_manifest(target, source, env) -> None:
    """SCons PostAction writing `<binary>.relink.json` for executables and shared libraries."""
    repo_root = Path(env.Dir("#").abspath).resolve()
    target_path = Path(target[0].abspath).resolve()
    default_ext_node = env.get("CAFECITO_EXT_DEFAULT_LIB")
    default_ext_path = Path(default_ext_node.abspath).resolve() if default_ext_node is not None else None

    object_files: list[str] = []
    for node in source:
        node_path = Path(node.abspath).resolve()
        try:
            object_files.append(node_path.relative_to(repo_root).as_posix())
        except ValueError:
            object_files.append(str(node_path))

    internal_static_libs, external_libs, default_ext_rel = _classify_libraries(env, repo_root, default_ext_path)

    is_msvc = bool(getattr(env, "msvc", False))
    raw_cxx_driver = str(env.subst("$CXX")).strip()
    cxx_driver = " ".join(_normalize_sdk_tokens(shlex.split(raw_cxx_driver), env)) if raw_cxx_driver else ""
    library_type = str(env.get("library_type", "executable"))
    if str(target_path).endswith((".dylib", ".so", ".dll")):
        target_kind = "shared_library"
        raw_linker = str(env.subst("$SHLINK")).strip() if is_msvc else (cxx_driver or str(env.subst("$SHLINK")).strip())
        linker = " ".join(_normalize_sdk_tokens(shlex.split(raw_linker), env)) if raw_linker else ""
        raw_shlinkflags = env.get("SHLINKFLAGS", [])
        shlink_tokens = _flatten_strings(env, raw_shlinkflags)
        if any("$LINKFLAGS" in str(item) for item in _flatten_raw_items(raw_shlinkflags)):
            raw_link_flags = shlink_tokens
        else:
            raw_link_flags = _flatten_strings(env, env.get("LINKFLAGS", [])) + shlink_tokens
        link_flags = _normalize_sdk_tokens(raw_link_flags, env)
    else:
        target_kind = "executable" if library_type == "executable" else library_type
        raw_linker = str(env.subst("$LINK")).strip() if is_msvc else (cxx_driver or str(env.subst("$LINK")).strip())
        linker = " ".join(_normalize_sdk_tokens(shlex.split(raw_linker), env)) if raw_linker else ""
        link_flags = _normalize_sdk_tokens(_flatten_strings(env, env.get("LINKFLAGS", [])), env)

    frameworks = [str(env.subst(str(fw))).strip() for fw in _flatten_raw_items(env.get("FRAMEWORKS", [])) if str(fw).strip()]
    framework_paths = _normalize_sdk_tokens(_flatten_strings(env, env.get("FRAMEWORKPATH", [])), env)
    lib_paths = _normalize_sdk_tokens(_flatten_strings(env, env.get("LIBPATH", [])), env)

    manifest = {
        "schema_version": 1,
        "platform": str(env.get("platform", "")),
        "target": str(env.get("target", "")),
        "arch": str(env.get("arch", "")),
        "library_type": library_type,
        "target_kind": target_kind,
        "output_filename": target_path.name,
        "default_extension_library": default_ext_rel,
        "headers": [
            "core/crypto/cafecito_security.h",
            "core/extension/cafecito_extensions.h",
        ],
        "object_files": object_files,
        "static_libraries": internal_static_libs,
        "external_libs": external_libs,
        "compile": _collect_compile_metadata(env, target_path.name),
        "link": {
            "linker": linker,
            "flags": link_flags,
            "frameworks": frameworks,
            "framework_paths": framework_paths,
            "lib_paths": lib_paths,
            "is_msvc": bool(getattr(env, "msvc", False)),
        },
    }

    manifest_path = Path(f"{target_path}.relink.json")
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def write_apple_embedded_relink_kit(target, source, env, libtool_cmd: str) -> None:
    """Create a pre-combined static archive without default ext lib + relink manifest for iOS."""
    repo_root = Path(env.Dir("#").abspath).resolve()
    target_path = Path(target[0].abspath).resolve()
    default_ext_node = env.get("CAFECITO_EXT_DEFAULT_LIB")
    default_ext_path = Path(default_ext_node.abspath).resolve() if default_ext_node is not None else None

    filtered_sources = []
    default_ext_rel: str | None = None
    for lib_node in source:
        lib_path = Path(lib_node.srcnode().abspath).resolve()
        if default_ext_path is not None and lib_path == default_ext_path:
            default_ext_rel = os.path.relpath(lib_path, repo_root).replace("\\", "/")
            continue
        filtered_sources.append(lib_node)

    relink_archive_path = Path(f"{target_path}.relink.a")
    quoted_sources = " ".join(f'"{lib.srcnode().abspath}"' for lib in filtered_sources)
    result = env.Execute(f'{libtool_cmd} -static -o "{relink_archive_path}" {quoted_sources}')
    if result != 0:
        raise RuntimeError(f"Failed to build Apple embedded relink archive: {relink_archive_path}")

    manifest = {
        "schema_version": 1,
        "platform": str(env.get("platform", "")),
        "target": str(env.get("target", "")),
        "arch": str(env.get("arch", "")),
        "library_type": "static_library",
        "target_kind": "static_library",
        "output_filename": target_path.name,
        "default_extension_library": default_ext_rel,
        "headers": [
            "core/crypto/cafecito_security.h",
            "core/extension/cafecito_extensions.h",
        ],
        "object_files": [],
        "static_libraries": [os.path.relpath(relink_archive_path, repo_root).replace("\\", "/")],
        "external_libs": [],
        "compile": _collect_compile_metadata(env, target_path.name),
        "link": {
            "linker": "libtool",
            "flags": ["-static"],
            "frameworks": [],
            "framework_paths": [],
            "lib_paths": [],
            "is_msvc": False,
        },
    }

    manifest_path = Path(f"{target_path}.relink.json")
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
