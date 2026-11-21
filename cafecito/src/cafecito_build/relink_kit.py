"""Packages SCons relink manifests and static archives into portable Relink Kits."""

from __future__ import annotations

import json
import shutil
import subprocess
import tarfile
from pathlib import Path
from typing import Any


APPLE_PLATFORMS = frozenset({"macos", "ios"})


def _verify_apple_architecture(source_path: Path, expected_architecture: str) -> None:
    """Refuse a kit input that was not built for the architecture the manifest names.

    SCons qualifies the per-module archives by architecture but writes a platform
    directory's loose objects to unqualified paths, so building a second
    architecture into the same workspace overwrites the first one's objects. A
    relink that links the wrong architecture does not fail: the linker only warns
    about the files it ignores, and because it pulls archive members lazily, the
    ignored objects take the whole engine out of the link. The result is a valid,
    nearly empty library produced with a zero exit status, which is why this has
    to be caught where the inputs are gathered.
    """
    result = subprocess.run(
        ["lipo", "-archs", str(source_path)], capture_output=True, text=True, check=False
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or f"exit code {result.returncode}"
        raise ValueError(f"Could not read architectures from {source_path}: {detail}")
    architectures = result.stdout.split()
    if expected_architecture not in architectures:
        raise ValueError(
            f"Relink kit input {source_path} holds [{' '.join(architectures) or 'no architecture'}], "
            f"but the manifest names arch {expected_architecture}. The workspace most likely built "
            f"another architecture after this one, overwriting its objects."
        )


def _copy_relative_file(
    repository_root: Path,
    relative_path: str,
    destination_root: Path,
    *,
    expected_architecture: str | None = None,
) -> Path:
    source_path = repository_root / relative_path
    if not source_path.is_file():
        raise FileNotFoundError(f"Relink kit input missing: {source_path}")
    if expected_architecture is not None:
        _verify_apple_architecture(source_path, expected_architecture)
    destination_path = destination_root / relative_path
    destination_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source_path, destination_path)
    return destination_path


def derive_kit_name(manifest: dict[str, Any]) -> str:
    compile_section = manifest.get("compile", {})
    defines = set(compile_section.get("defines", []))
    parts = [
        "relink-kit",
        str(manifest["platform"]),
        str(manifest["target"]),
        str(manifest["arch"]),
    ]
    if compile_section.get("ios_simulator") or ".simulator." in str(manifest.get("output_filename", "")):
        parts.append("simulator")
    if "-DLIBGODOT_ENABLED" in defines:
        parts.append("libgodot")
    parts.append(str(manifest["library_type"]))
    return "-".join(parts)


def package_relink_kit(
    repository_root: Path,
    manifest_path: Path,
    output_directory: Path,
    *,
    create_archive: bool = True,
) -> Path:
    """Create a self-contained Relink Kit directory (and optional .tar.zst archive)."""
    manifest: dict[str, Any] = json.loads(manifest_path.read_text(encoding="utf-8"))
    kit_name = derive_kit_name(manifest)
    kit_directory = output_directory / kit_name
    if kit_directory.exists():
        shutil.rmtree(kit_directory)
    kit_directory.mkdir(parents=True, exist_ok=True)

    # `lipo` reads Mach-O only, so the architecture guard applies to the Apple kits —
    # which is also where unqualified object paths are shared between architectures.
    expected_architecture = (
        str(manifest["arch"]) if str(manifest.get("platform")) in APPLE_PLATFORMS else None
    )

    for header_relative in manifest.get("headers", []):
        _copy_relative_file(repository_root, header_relative, kit_directory / "include")

    for object_relative in manifest.get("object_files", []):
        if Path(object_relative).is_absolute():
            continue
        _copy_relative_file(
            repository_root, object_relative, kit_directory, expected_architecture=expected_architecture
        )

    for library_relative in manifest.get("static_libraries", []):
        if Path(library_relative).is_absolute():
            continue
        _copy_relative_file(
            repository_root, library_relative, kit_directory, expected_architecture=expected_architecture
        )

    (kit_directory / "relink_manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
    )

    if not create_archive:
        return kit_directory

    zstd_binary = shutil.which("zstd")
    if zstd_binary is not None:
        tar_path = output_directory / f"{kit_name}.tar"
        archive_path = output_directory / f"{kit_name}.tar.zst"
        with tarfile.open(tar_path, "w") as tar_handle:
            tar_handle.add(kit_directory, arcname=kit_name)
        subprocess.run([zstd_binary, "-T0", "-9", "--rm", "-f", str(tar_path), "-o", str(archive_path)], check=True)
        shutil.rmtree(kit_directory)
        return archive_path

    archive_path = output_directory / f"{kit_name}.tar.gz"
    with tarfile.open(archive_path, "w:gz") as tar_handle:
        tar_handle.add(kit_directory, arcname=kit_name)
    shutil.rmtree(kit_directory)
    return archive_path
