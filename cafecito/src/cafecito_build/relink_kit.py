"""Packages SCons relink manifests and static archives into portable Relink Kits."""

from __future__ import annotations

import json
import shutil
import subprocess
import tarfile
from pathlib import Path
from typing import Any


def _copy_relative_file(repository_root: Path, relative_path: str, destination_root: Path) -> Path:
    source_path = repository_root / relative_path
    if not source_path.is_file():
        raise FileNotFoundError(f"Relink kit input missing: {source_path}")
    destination_path = destination_root / relative_path
    destination_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source_path, destination_path)
    return destination_path


def package_relink_kit(
    repository_root: Path,
    manifest_path: Path,
    output_directory: Path,
    *,
    create_archive: bool = True,
) -> Path:
    """Create a self-contained Relink Kit directory (and optional .tar.zst archive)."""
    manifest: dict[str, Any] = json.loads(manifest_path.read_text(encoding="utf-8"))
    kit_name = (
        f"relink-kit-{manifest['platform']}-{manifest['target']}-"
        f"{manifest['arch']}-{manifest['library_type']}"
    )
    kit_directory = output_directory / kit_name
    if kit_directory.exists():
        shutil.rmtree(kit_directory)
    kit_directory.mkdir(parents=True, exist_ok=True)

    for header_relative in manifest.get("headers", []):
        _copy_relative_file(repository_root, header_relative, kit_directory / "include")

    for object_relative in manifest.get("object_files", []):
        if Path(object_relative).is_absolute():
            continue
        _copy_relative_file(repository_root, object_relative, kit_directory)

    for library_relative in manifest.get("static_libraries", []):
        if Path(library_relative).is_absolute():
            continue
        _copy_relative_file(repository_root, library_relative, kit_directory)

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
        return archive_path

    archive_path = output_directory / f"{kit_name}.tar.gz"
    with tarfile.open(archive_path, "w:gz") as tar_handle:
        tar_handle.add(kit_directory, arcname=kit_name)
    return archive_path
