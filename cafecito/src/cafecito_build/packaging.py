"""Packaging utilities for macOS template bundles and release artifacts."""

from __future__ import annotations

import shutil
import zipfile
from pathlib import Path


def package_macos_template_bundle(
    repository_root: Path,
    template_binary: Path,
    output_zip_path: Path,
    *,
    debug: bool = False,
) -> Path:
    """Package a macOS template executable into a Godot-compatible godot_macos.zip."""
    skeleton_directory = repository_root / "misc" / "dist" / "macos_template.app"
    if not skeleton_directory.is_dir():
        raise FileNotFoundError(f"macOS template skeleton not found: {skeleton_directory}")
    if not template_binary.is_file():
        raise FileNotFoundError(f"macOS template binary not found: {template_binary}")

    staging_directory = output_zip_path.parent / "macos_template.app"
    if staging_directory.exists():
        shutil.rmtree(staging_directory)
    shutil.copytree(skeleton_directory, staging_directory)

    macos_executable_directory = staging_directory / "Contents" / "MacOS"
    macos_executable_directory.mkdir(parents=True, exist_ok=True)
    executable_name = "godot_macos_debug.universal" if debug else "godot_macos_release.universal"
    destination_binary = macos_executable_directory / executable_name
    shutil.copy2(template_binary, destination_binary)
    destination_binary.chmod(0o755)

    output_zip_path.parent.mkdir(parents=True, exist_ok=True)
    if output_zip_path.exists():
        output_zip_path.unlink()

    with zipfile.ZipFile(output_zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for item in sorted(staging_directory.rglob("*")):
            archive_name = item.relative_to(staging_directory.parent).as_posix()
            if item.is_dir():
                continue
            archive.write(item, arcname=archive_name)

    shutil.rmtree(staging_directory)
    return output_zip_path
