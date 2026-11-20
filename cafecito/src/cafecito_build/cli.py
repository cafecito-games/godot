"""Command-line interface for cafecito-build."""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

from cafecito_build.apple_libgodot import build_apple_libgodot_slices, package_apple_libgodot_sdk
from cafecito_build.environment import detect_host_environment
from cafecito_build.identity import resolve_build_identity
from cafecito_build.packaging import (
    finalize_ios_template_zip,
    package_android_templates_zip,
    package_macos_editor_bundle,
    package_macos_template_bundle,
    write_sha256sums,
)
from cafecito_build.relink_kit import package_relink_kit
from cafecito_build.scons import BuildRequest, run_scons_build


def _default_repository_root() -> Path:
    return Path(__file__).resolve().parents[3]


def _command_info(arguments: argparse.Namespace) -> int:
    repository_root = Path(arguments.repo_root).resolve()
    host = detect_host_environment()
    identity = resolve_build_identity(repository_root)
    print(f"Repository:       {repository_root}")
    print(f"Godot version:    {identity.template_version}")
    print(f"Build identity:   {identity.build_name}")
    print(f"Host platform:    {host.godot_platform}")
    print(f"Host arch:        {host.architecture}")
    print(f"CPU count:        {host.cpu_count}")
    print(f"Vulkan SDK:       {host.vulkan_sdk_path or 'not detected'}")
    return 0


def _command_build(arguments: argparse.Namespace) -> int:
    repository_root = Path(arguments.repo_root).resolve()
    host = detect_host_environment()
    request = BuildRequest(
        repository_root=repository_root,
        godot_platform=arguments.platform or host.godot_platform,
        target=arguments.target,
        architecture=arguments.arch or host.architecture,
        library_type=arguments.library_type,
        relink_kit=not arguments.no_relink_kit,
        extension_library=Path(arguments.ext_library) if arguments.ext_library else None,
        debug_symbols=arguments.debug_symbols,
        tests=arguments.tests,
        use_default_module_profile=not arguments.all_modules,
        jobs=arguments.jobs,
        extra_flags=tuple(arguments.extra_flag or ()),
    )
    run_scons_build(request)
    return 0


def _command_package_relink_kit(arguments: argparse.Namespace) -> int:
    repository_root = Path(arguments.repo_root).resolve()
    manifest_path = Path(arguments.manifest).resolve()
    output_directory = Path(arguments.output_dir).resolve()
    result_path = package_relink_kit(
        repository_root,
        manifest_path,
        output_directory,
        create_archive=not arguments.directory_only,
    )
    print(f"[cafecito-build] Relink Kit written to: {result_path}")
    return 0


def _command_package_macos_template(arguments: argparse.Namespace) -> int:
    repository_root = Path(arguments.repo_root).resolve()
    output_path = package_macos_template_bundle(
        repository_root,
        Path(arguments.release_binary).resolve(),
        Path(arguments.output).resolve(),
        debug_binary=Path(arguments.debug_binary).resolve() if arguments.debug_binary else None,
    )
    print(f"[cafecito-build] Packaged macOS template bundle: {output_path}")
    return 0


def _command_package_macos_editor(arguments: argparse.Namespace) -> int:
    repository_root = Path(arguments.repo_root).resolve()
    output_path = package_macos_editor_bundle(
        repository_root,
        Path(arguments.editor_binary).resolve(),
        Path(arguments.output).resolve(),
    )
    print(f"[cafecito-build] Packaged macOS editor bundle: {output_path}")
    return 0


def _command_finalize_ios_template(arguments: argparse.Namespace) -> int:
    output_path = finalize_ios_template_zip(
        Path(arguments.input_zip).resolve(),
        Path(arguments.output_zip).resolve(),
        single_arch_arm64_simulator=not arguments.keep_x86_64_simulator,
    )
    print(f"[cafecito-build] Finalized iOS template bundle: {output_path}")
    return 0


def _command_package_android_templates(arguments: argparse.Namespace) -> int:
    output_path = package_android_templates_zip(
        Path(arguments.bin_dir).resolve(),
        Path(arguments.output_zip).resolve(),
        required_abis=tuple(arguments.abi or ("arm64-v8a", "x86_64")),
    )
    print(f"[cafecito-build] Packaged Android templates bundle: {output_path}")
    return 0


def _command_build_apple_libgodot(arguments: argparse.Namespace) -> int:
    repository_root = Path(arguments.repo_root).resolve()
    outputs = build_apple_libgodot_slices(
        repository_root,
        arguments.configuration,
        relink_kit=not arguments.no_relink_kit,
        jobs=arguments.jobs,
    )
    for output_path in outputs:
        print(f"[cafecito-build] Built Apple LibGodot slice: {output_path}")
    return 0


def _command_package_apple_libgodot(arguments: argparse.Namespace) -> int:
    repository_root = Path(arguments.repo_root).resolve()
    output = package_apple_libgodot_sdk(
        repository_root,
        arguments.configuration,
        Path(arguments.output).resolve(),
    )
    print(f"[cafecito-build] Packaged Apple LibGodot SDK: {output}")
    return 0


def _command_checksums(arguments: argparse.Namespace) -> int:
    checksums_path = write_sha256sums(Path(arguments.directory).resolve())
    print(f"[cafecito-build] Wrote checksums: {checksums_path}")
    return 0


def _command_clean(arguments: argparse.Namespace) -> int:
    repository_root = Path(arguments.repo_root).resolve()
    bin_directory = repository_root / "bin"
    if bin_directory.exists():
        shutil.rmtree(bin_directory)
        print(f"[cafecito-build] Removed {bin_directory}")
    return 0


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="cafecito-build", description="Cafecito Godot build CLI")
    parser.add_argument("--repo-root", default=str(_default_repository_root()), help="Path to Godot fork root")
    subparsers = parser.add_subparsers(dest="command", required=True)

    info_parser = subparsers.add_parser("info", help="Print host, version, and build identity information")
    info_parser.set_defaults(handler=_command_info)

    build_parser = subparsers.add_parser("build", help="Compile Godot via SCons")
    build_parser.add_argument("--platform", default=None, help="Godot platform (macos, linuxbsd, windows, ios, android)")
    build_parser.add_argument("--target", default="editor", choices=("editor", "template_release", "template_debug"))
    build_parser.add_argument("--arch", default=None, help="Target architecture (arm64, x86_64, universal)")
    build_parser.add_argument(
        "--library-type",
        default="executable",
        choices=("executable", "shared_library", "static_library"),
    )
    build_parser.add_argument("--no-relink-kit", action="store_true", help="Disable emitting .relink.json manifests")
    build_parser.add_argument("--ext-library", default=None, help="Path to private extension .a replacing default")
    build_parser.add_argument("--debug-symbols", action="store_true", help="Enable debug symbols")
    build_parser.add_argument("--tests", action="store_true", help="Build C++ doctest suite")
    build_parser.add_argument("--all-modules", action="store_true", help="Skip trimmed 2D module profile")
    build_parser.add_argument("-j", "--jobs", type=int, default=None, help="Parallel compile jobs")
    build_parser.add_argument("--extra-flag", action="append", help="Additional SCons flag (repeatable)")
    build_parser.set_defaults(handler=_command_build)

    kit_parser = subparsers.add_parser("package-relink-kit", help="Package a Relink Kit from a .relink.json manifest")
    kit_parser.add_argument("--manifest", required=True, help="Path to <binary>.relink.json")
    kit_parser.add_argument("--output-dir", default="bin/relink-kits", help="Destination directory for Relink Kit")
    kit_parser.add_argument("--directory-only", action="store_true", help="Leave kit uncompressed as a directory")
    kit_parser.set_defaults(handler=_command_package_relink_kit)

    macos_zip_parser = subparsers.add_parser("package-macos-template", help="Create macos.zip from built templates")
    macos_zip_parser.add_argument("--release-binary", required=True, help="Path to macOS release template binary")
    macos_zip_parser.add_argument("--debug-binary", default=None, help="Optional macOS debug template binary")
    macos_zip_parser.add_argument("--output", default="bin/macos.zip", help="Output zip path")
    macos_zip_parser.set_defaults(handler=_command_package_macos_template)

    macos_ed_parser = subparsers.add_parser("package-macos-editor", help="Create macOS editor app zip")
    macos_ed_parser.add_argument("--editor-binary", required=True, help="Path to macOS editor binary")
    macos_ed_parser.add_argument("--output", default="bin/godot_macos_editor.arm64.zip", help="Output zip path")
    macos_ed_parser.set_defaults(handler=_command_package_macos_editor)

    ios_zip_parser = subparsers.add_parser("finalize-ios-template", help="Normalize godot_ios.zip into ios.zip")
    ios_zip_parser.add_argument("--input-zip", default="bin/godot_ios.zip")
    ios_zip_parser.add_argument("--output-zip", default="bin/ios.zip")
    ios_zip_parser.add_argument("--keep-x86-64-simulator", action="store_true")
    ios_zip_parser.set_defaults(handler=_command_finalize_ios_template)

    android_zip_parser = subparsers.add_parser("package-android-templates", help="Bundle and verify android.zip")
    android_zip_parser.add_argument("--bin-dir", default="bin")
    android_zip_parser.add_argument("--output-zip", default="bin/android.zip")
    android_zip_parser.add_argument("--abi", action="append", help="Required ABI (repeatable)")
    android_zip_parser.set_defaults(handler=_command_package_android_templates)

    apple_libgodot_parser = subparsers.add_parser(
        "build-apple-libgodot",
        help="Build Apple LibGodot SDK slices (macOS dylibs + iOS static libraries)",
    )
    apple_libgodot_parser.add_argument("--configuration", default="release", choices=("release", "debug"))
    apple_libgodot_parser.add_argument("--no-relink-kit", action="store_true")
    apple_libgodot_parser.add_argument("-j", "--jobs", type=int, default=None)
    apple_libgodot_parser.set_defaults(handler=_command_build_apple_libgodot)

    apple_pkg_parser = subparsers.add_parser(
        "package-apple-libgodot",
        help="Package Apple LibGodot SDK archive",
    )
    apple_pkg_parser.add_argument("--configuration", default="release", choices=("release", "debug"))
    apple_pkg_parser.add_argument("--output", default="bin/apple-libgodot-release.zip")
    apple_pkg_parser.set_defaults(handler=_command_package_apple_libgodot)

    checksums_parser = subparsers.add_parser("checksums", help="Write SHA256SUMS.txt for a release directory")
    checksums_parser.add_argument("--directory", required=True)
    checksums_parser.set_defaults(handler=_command_checksums)

    clean_parser = subparsers.add_parser("clean", help="Remove bin/ build outputs")
    clean_parser.set_defaults(handler=_command_clean)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_argument_parser()
    arguments = parser.parse_args(argv)
    return arguments.handler(arguments)


if __name__ == "__main__":
    sys.exit(main())
