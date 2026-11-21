"""Apple LibGodot SDK slice builder and packager for SwiftGodotKit with Relink Kit support."""

from __future__ import annotations

import hashlib
import json
import subprocess
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from cafecito_build.environment import detect_architecture
from cafecito_build.identity import BuildIdentity, resolve_build_identity
from cafecito_build.relink_kit import package_relink_kit
from cafecito_build.scons import BuildRequest, run_scons_build

SHARED_APPLE_LIBGODOT_FLAGS: tuple[str, ...] = (
    "vulkan=no",
    "metal=yes",
    "accesskit=no",
    "angle=no",
    "disable_path_overrides=no",
)


@dataclass(frozen=True)
class AppleLibGodotSlice:
    godot_platform: str
    architecture: str
    library_type: str
    output_filename: str
    simulator: bool = False

    @property
    def slice_name(self) -> str:
        if self.godot_platform == "ios":
            variant = "simulator" if self.simulator else "device"
            return f"ios-{self.architecture}-{variant}"
        return f"{self.godot_platform}-{self.architecture}"

    def extra_flags(self) -> tuple[str, ...]:
        flags = list(SHARED_APPLE_LIBGODOT_FLAGS)
        if self.godot_platform == "ios":
            flags.append(f"ios_simulator={'yes' if self.simulator else 'no'}")
        return tuple(flags)

    def scons_flags(self, target: str) -> list[str]:
        flags = [
            f"platform={self.godot_platform}",
            f"arch={self.architecture}",
            f"library_type={self.library_type}",
            f"target={target}",
            *self.extra_flags(),
        ]
        return sorted(set(flags))


APPLE_LIBGODOT_SLICES: dict[str, tuple[str, tuple[AppleLibGodotSlice, ...]]] = {
    "release": (
        "template_release",
        (
            AppleLibGodotSlice("macos", "arm64", "shared_library", "libgodot.macos.template_release.arm64.dylib"),
            AppleLibGodotSlice("macos", "x86_64", "shared_library", "libgodot.macos.template_release.x86_64.dylib"),
            AppleLibGodotSlice("ios", "arm64", "static_library", "libgodot.ios.template_release.arm64.a"),
            AppleLibGodotSlice(
                "ios",
                "arm64",
                "static_library",
                "libgodot.ios.template_release.arm64.simulator.a",
                simulator=True,
            ),
        ),
    ),
    "debug": (
        "template_debug",
        (
            AppleLibGodotSlice("macos", "arm64", "shared_library", "libgodot.macos.template_debug.arm64.dylib"),
            AppleLibGodotSlice("macos", "x86_64", "shared_library", "libgodot.macos.template_debug.x86_64.dylib"),
            AppleLibGodotSlice("ios", "arm64", "static_library", "libgodot.ios.template_debug.arm64.a"),
            AppleLibGodotSlice(
                "ios",
                "arm64",
                "static_library",
                "libgodot.ios.template_debug.arm64.simulator.a",
                simulator=True,
            ),
        ),
    ),
}

STATIC_PACKAGE_ASSETS: dict[str, str] = {
    "include/libgodot.h": "core/extension/libgodot.h",
    "include/gdextension_interface.gen.h": "core/extension/gdextension_interface.gen.h",
    "LICENSE.txt": "LICENSE.txt",
    "COPYRIGHT.txt": "COPYRIGHT.txt",
}

BUILD_INFO_FILENAME = "BUILD_INFO.json"
EXTENSION_API_FILENAME = "extension_api.json"


def compute_file_sha256(file_path: Path) -> str:
    """Compute the lowercase hexadecimal SHA-256 digest of a file."""
    digest = hashlib.sha256()
    with file_path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _validate_extension_api_file(extension_api_path: Path) -> Path:
    if not extension_api_path.is_file():
        raise FileNotFoundError(f"Extension API file missing: {extension_api_path}")
    try:
        parsed = json.loads(extension_api_path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError) as error:
        raise ValueError(f"Invalid extension API JSON at {extension_api_path}: {error}") from error
    if not isinstance(parsed, dict):
        raise ValueError(f"Expected JSON object in {extension_api_path}")
    return extension_api_path


def ensure_macos_editor(
    repository_root: Path,
    *,
    editor_binary: Path | None = None,
    jobs: int | None = None,
    extra_flags: tuple[str, ...] = (),
) -> Path:
    """Return an existing macOS editor binary or build one for dumping extension_api.json."""
    if editor_binary is not None:
        if not editor_binary.is_file():
            raise FileNotFoundError(f"macOS editor binary missing: {editor_binary}")
        return editor_binary

    host_architecture = detect_architecture()
    default_binary = repository_root / "bin" / f"godot.macos.editor.{host_architecture}"
    if default_binary.is_file():
        return default_binary

    run_scons_build(
        BuildRequest(
            repository_root=repository_root,
            godot_platform="macos",
            target="editor",
            architecture=host_architecture,
            relink_kit=False,
            jobs=jobs,
            extra_flags=(*SHARED_APPLE_LIBGODOT_FLAGS, "optimize=none", *extra_flags),
        )
    )
    if not default_binary.is_file():
        raise FileNotFoundError(f"Built macOS editor binary missing: {default_binary}")
    return default_binary


def dump_extension_api(editor_binary: Path, working_directory: Path) -> Path:
    """Invoke the Godot editor in headless mode to dump extension_api.json."""
    resolved_editor = editor_binary.resolve()
    if not resolved_editor.is_file():
        raise FileNotFoundError(f"macOS editor binary missing: {resolved_editor}")

    command = [str(resolved_editor), "--headless", "--dump-extension-api"]
    result = subprocess.run(
        command,
        cwd=working_directory,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or f"exit code {result.returncode}"
        raise RuntimeError(f"Failed to dump extension_api.json using {resolved_editor}: {detail}")

    return _validate_extension_api_file(working_directory / EXTENSION_API_FILENAME)


def build_apple_libgodot_build_info(
    identity: BuildIdentity,
    target: str,
    slices: tuple[AppleLibGodotSlice, ...],
    file_hashes: dict[str, str],
) -> dict[str, Any]:
    """Construct the BUILD_INFO.json provenance and checksum payload."""
    slice_records = [
        {
            "output": slice_specification.output_filename,
            "platform": slice_specification.godot_platform,
            "arch": slice_specification.architecture,
            "library_type": slice_specification.library_type,
            "scons_flags": slice_specification.scons_flags(target),
        }
        for slice_specification in slices
    ]
    return {
        "cafecito_godot_commit": identity.commit_sha,
        "godot_commit": identity.commit_sha,
        "godot_version": identity.template_version,
        "build_name": identity.build_name,
        "target": target,
        "slices": slice_records,
        "files": dict(sorted(file_hashes.items())),
    }


def validate_build_info_identity(
    build_info: dict[str, Any],
    identity: BuildIdentity,
    staged_files: dict[str, Path],
) -> None:
    """Verify BUILD_INFO.json matches the checked-out identity and staged file digests."""
    expected_identity = {
        "cafecito_godot_commit": identity.commit_sha,
        "godot_commit": identity.commit_sha,
        "godot_version": identity.template_version,
        "build_name": identity.build_name,
    }
    for key, expected_value in expected_identity.items():
        actual_value = build_info.get(key)
        if actual_value != expected_value:
            raise ValueError(f"{BUILD_INFO_FILENAME} {key} is {actual_value!r}; expected {expected_value!r}")

    recorded_files = build_info.get("files")
    if not isinstance(recorded_files, dict):
        raise ValueError(f"{BUILD_INFO_FILENAME} is missing its per-file SHA-256 map")

    expected_paths = {archive_path for archive_path in staged_files if archive_path != BUILD_INFO_FILENAME}
    recorded_paths = set(recorded_files.keys())
    if recorded_paths != expected_paths:
        missing = sorted(expected_paths - recorded_paths)
        unexpected = sorted(recorded_paths - expected_paths)
        raise ValueError(f"{BUILD_INFO_FILENAME} file list mismatch (missing={missing}, unexpected={unexpected})")

    for archive_path in sorted(expected_paths):
        actual_hash = compute_file_sha256(staged_files[archive_path])
        recorded_hash = recorded_files.get(archive_path)
        if recorded_hash != actual_hash:
            raise ValueError(
                f"{BUILD_INFO_FILENAME} checksum mismatch for {archive_path}: "
                f"recorded {recorded_hash!r}, actual {actual_hash!r}"
            )


def build_apple_libgodot_slices(
    repository_root: Path,
    configuration: str,
    *,
    relink_kit: bool = True,
    jobs: int | None = None,
    kit_output_directory: Path | None = None,
    extra_flags: tuple[str, ...] = (),
    slice_names: tuple[str, ...] | None = None,
) -> list[Path]:
    """Build Apple LibGodot slices for the requested configuration.

    Every slice builds into one workspace, and a platform directory's loose
    objects live at paths SCons does not qualify by architecture. The macOS
    slices therefore overwrite each other's `platform/macos` objects, and a kit
    packaged after the whole loop carries whichever architecture built last.
    Passing `kit_output_directory` packages each slice's Relink Kit immediately
    after that slice builds, which is the only point at which the objects its
    manifest names are still the ones it was built from.
    """
    if configuration not in APPLE_LIBGODOT_SLICES:
        raise ValueError(f"Unsupported Apple LibGodot configuration: {configuration!r}")

    target, all_slices = APPLE_LIBGODOT_SLICES[configuration]
    if slice_names:
        slices_by_name = {item.slice_name: item for item in all_slices}
        unknown = [name for name in slice_names if name not in slices_by_name]
        if unknown:
            valid = ", ".join(sorted(slices_by_name))
            raise ValueError(f"Unsupported Apple LibGodot slice(s): {', '.join(unknown)} (valid: {valid})")
        selected_slices = tuple(slices_by_name[name] for name in slice_names)
    else:
        selected_slices = all_slices

    bin_directory = repository_root / "bin"
    built_outputs: list[Path] = []
    for slice_specification in selected_slices:
        run_scons_build(
            BuildRequest(
                repository_root=repository_root,
                godot_platform=slice_specification.godot_platform,
                target=target,
                architecture=slice_specification.architecture,
                library_type=slice_specification.library_type,
                relink_kit=relink_kit,
                jobs=jobs,
                extra_flags=(*slice_specification.extra_flags(), *extra_flags),
            )
        )
        built_outputs.append(bin_directory / slice_specification.output_filename)
        if relink_kit and kit_output_directory is not None:
            manifest_path = bin_directory / f"{slice_specification.output_filename}.relink.json"
            kit_path = package_relink_kit(repository_root, manifest_path, kit_output_directory)
            print(f"[cafecito-build] Packaged {kit_path}")
    return built_outputs


def package_apple_libgodot_sdk(
    repository_root: Path,
    configuration: str,
    output_zip: Path,
    *,
    editor_binary: Path | None = None,
    extension_api_path: Path | None = None,
    jobs: int | None = None,
    extra_flags: tuple[str, ...] = (),
) -> Path:
    """Package built Apple LibGodot slices, headers, API dump, license files, and BUILD_INFO.json."""
    if configuration not in APPLE_LIBGODOT_SLICES:
        raise ValueError(f"Unsupported Apple LibGodot configuration: {configuration!r}")

    target, slices = APPLE_LIBGODOT_SLICES[configuration]
    identity = resolve_build_identity(repository_root)
    bin_directory = repository_root / "bin"

    staged_files: dict[str, Path] = {}
    for slice_specification in slices:
        binary_path = bin_directory / slice_specification.output_filename
        if not binary_path.is_file():
            raise FileNotFoundError(f"Apple LibGodot slice missing: {binary_path}")
        staged_files[f"bin/{slice_specification.output_filename}"] = binary_path

    for archive_path, relative_source in STATIC_PACKAGE_ASSETS.items():
        source_file = repository_root / relative_source
        if not source_file.is_file():
            raise FileNotFoundError(f"Required Apple LibGodot SDK asset missing: {source_file}")
        staged_files[archive_path] = source_file

    with tempfile.TemporaryDirectory() as temporary_root:
        temporary_directory = Path(temporary_root)
        if extension_api_path is not None:
            staged_files[EXTENSION_API_FILENAME] = _validate_extension_api_file(extension_api_path)
        else:
            resolved_editor = ensure_macos_editor(
                repository_root,
                editor_binary=editor_binary,
                jobs=jobs,
                extra_flags=extra_flags,
            )
            staged_files[EXTENSION_API_FILENAME] = dump_extension_api(resolved_editor, temporary_directory)

        file_hashes = {
            archive_path: compute_file_sha256(source_path) for archive_path, source_path in sorted(staged_files.items())
        }
        build_info = build_apple_libgodot_build_info(identity, target, slices, file_hashes)
        build_info_path = temporary_directory / BUILD_INFO_FILENAME
        build_info_path.write_text(json.dumps(build_info, indent=2, sort_keys=True) + "\n", encoding="utf-8")

        parsed_build_info = json.loads(build_info_path.read_text(encoding="utf-8"))
        validate_build_info_identity(parsed_build_info, identity, staged_files)
        staged_files[BUILD_INFO_FILENAME] = build_info_path

        output_zip.parent.mkdir(parents=True, exist_ok=True)
        if output_zip.exists():
            output_zip.unlink()

        with zipfile.ZipFile(output_zip, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for archive_path in sorted(staged_files):
                archive.write(staged_files[archive_path], arcname=archive_path)

    return output_zip
