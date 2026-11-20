"""Command-line interface for cafecito-build."""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

from cafecito_build.environment import detect_host_environment
from cafecito_build.identity import resolve_build_identity
from cafecito_build.packaging import package_macos_template_bundle
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
        Path(arguments.binary).resolve(),
        Path(arguments.output).resolve(),
        debug=arguments.debug,
    )
    print(f"[cafecito-build] Packaged macOS template bundle: {output_path}")
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
    build_parser.add_argument("--library-type", default="executable", choices=("executable", "shared_library", "static_library"))
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

    macos_zip_parser = subparsers.add_parser("package-macos-template", help="Create godot_macos.zip from a built template")
    macos_zip_parser.add_argument("--binary", required=True, help="Path to macOS template binary")
    macos_zip_parser.add_argument("--output", default="bin/godot_macos.zip", help="Output zip path")
    macos_zip_parser.add_argument("--debug", action="store_true", help="Package as debug template")
    macos_zip_parser.set_defaults(handler=_command_package_macos_template)

    clean_parser = subparsers.add_parser("clean", help="Remove bin/ build outputs")
    clean_parser.set_defaults(handler=_command_clean)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_argument_parser()
    arguments = parser.parse_args(argv)
    return arguments.handler(arguments)


if __name__ == "__main__":
    sys.exit(main())
