"""Packaging utilities for macOS, iOS, Android, and release checksum artifacts."""

from __future__ import annotations

import hashlib
import plistlib
import shutil
import zipfile
from pathlib import Path


def _zip_directory(source_directory: Path, output_zip: Path) -> Path:
    output_zip.parent.mkdir(parents=True, exist_ok=True)
    if output_zip.exists():
        output_zip.unlink()
    with zipfile.ZipFile(output_zip, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for item in sorted(source_directory.rglob("*")):
            if item.is_dir():
                continue
            archive.write(item, arcname=item.relative_to(source_directory.parent).as_posix())
    return output_zip


def package_macos_template_bundle(
    repository_root: Path,
    release_binary: Path,
    output_zip_path: Path,
    *,
    debug_binary: Path | None = None,
) -> Path:
    """Package macOS release (and optional debug) template binaries into macos.zip."""
    skeleton_directory = repository_root / "misc" / "dist" / "macos_template.app"
    if not skeleton_directory.is_dir():
        raise FileNotFoundError(f"macOS template skeleton not found: {skeleton_directory}")
    if not release_binary.is_file():
        raise FileNotFoundError(f"macOS release template binary not found: {release_binary}")

    staging_directory = output_zip_path.parent / "macos_template.app"
    if staging_directory.exists():
        shutil.rmtree(staging_directory)
    shutil.copytree(skeleton_directory, staging_directory)

    macos_executable_directory = staging_directory / "Contents" / "MacOS"
    macos_executable_directory.mkdir(parents=True, exist_ok=True)

    release_destination = macos_executable_directory / "godot_macos_release.universal"
    shutil.copy2(release_binary, release_destination)
    release_destination.chmod(0o755)

    if debug_binary is not None:
        if not debug_binary.is_file():
            raise FileNotFoundError(f"macOS debug template binary not found: {debug_binary}")
        debug_destination = macos_executable_directory / "godot_macos_debug.universal"
        shutil.copy2(debug_binary, debug_destination)
        debug_destination.chmod(0o755)

    result = _zip_directory(staging_directory, output_zip_path)
    shutil.rmtree(staging_directory)
    return result


def package_macos_editor_bundle(
    repository_root: Path,
    editor_binary: Path,
    output_zip_path: Path,
) -> Path:
    """Package a macOS editor binary into godot_macos_editor.<arch>.zip."""
    skeleton_directory = repository_root / "misc" / "dist" / "macos_tools.app"
    if not skeleton_directory.is_dir():
        raise FileNotFoundError(f"macOS editor skeleton not found: {skeleton_directory}")
    if not editor_binary.is_file():
        raise FileNotFoundError(f"macOS editor binary not found: {editor_binary}")

    staging_directory = output_zip_path.parent / "godot_macos_editor.app"
    if staging_directory.exists():
        shutil.rmtree(staging_directory)
    shutil.copytree(skeleton_directory, staging_directory)

    macos_executable_directory = staging_directory / "Contents" / "MacOS"
    macos_executable_directory.mkdir(parents=True, exist_ok=True)
    destination_binary = macos_executable_directory / "Godot"
    shutil.copy2(editor_binary, destination_binary)
    destination_binary.chmod(0o755)

    result = _zip_directory(staging_directory, output_zip_path)
    shutil.rmtree(staging_directory)
    return result


def finalize_ios_template_zip(
    source_godot_ios_zip: Path,
    output_ios_zip: Path,
    *,
    single_arch_arm64_simulator: bool = True,
) -> Path:
    """Normalize Godot's generated godot_ios.zip into exporter-ready ios.zip and verify MoltenVK."""
    if not source_godot_ios_zip.is_file():
        raise FileNotFoundError(f"Generated iOS template bundle not found: {source_godot_ios_zip}")

    output_ios_zip.parent.mkdir(parents=True, exist_ok=True)
    if output_ios_zip.exists():
        output_ios_zip.unlink()

    moltenvk_device_present = False
    with zipfile.ZipFile(source_godot_ios_zip, "r") as source_zip, zipfile.ZipFile(
        output_ios_zip, "w", compression=zipfile.ZIP_DEFLATED
    ) as target_zip:
        for info in source_zip.infolist():
            name = info.filename
            data = source_zip.read(name)

            if name == "MoltenVK.xcframework/ios-arm64/libMoltenVK.a":
                moltenvk_device_present = True

            if single_arch_arm64_simulator:
                if name.startswith("libgodot.ios.") and "/ios-arm64_x86_64-simulator/" in name:
                    name = name.replace("/ios-arm64_x86_64-simulator/", "/ios-arm64-simulator/", 1)
                elif name in (
                    "libgodot.ios.release.xcframework/Info.plist",
                    "libgodot.ios.debug.xcframework/Info.plist",
                ):
                    plist = plistlib.loads(data)
                    for library in plist.get("AvailableLibraries", []):
                        if library.get("LibraryIdentifier") == "ios-arm64_x86_64-simulator":
                            library["LibraryIdentifier"] = "ios-arm64-simulator"
                            library["SupportedArchitectures"] = ["arm64"]
                    data = plistlib.dumps(plist)

            new_info = zipfile.ZipInfo(name, date_time=info.date_time)
            new_info.external_attr = info.external_attr
            target_zip.writestr(new_info, data)

    if not moltenvk_device_present:
        raise RuntimeError(
            f"MoltenVK.xcframework/ios-arm64/libMoltenVK.a is missing from {source_godot_ios_zip}."
        )
    return output_ios_zip


def package_android_templates_zip(
    bin_directory: Path,
    output_zip_path: Path,
    *,
    required_abis: tuple[str, ...] = ("arm64-v8a", "x86_64"),
) -> Path:
    """Bundle android_debug.apk, android_release.apk, and android_source.zip and verify ABIs."""
    required_files = ("android_debug.apk", "android_release.apk", "android_source.zip")
    for filename in required_files:
        candidate = bin_directory / filename
        if not candidate.is_file():
            raise FileNotFoundError(f"Expected Android template file missing: {candidate}")

    for apk_name in ("android_debug.apk", "android_release.apk"):
        apk_path = bin_directory / apk_name
        with zipfile.ZipFile(apk_path, "r") as apk_zip:
            entries = set(apk_zip.namelist())
            for abi in required_abis:
                expected = f"lib/{abi}/libgodot_android.so"
                if expected not in entries:
                    raise RuntimeError(f"{apk_name} is missing required native library {expected}")

    output_zip_path.parent.mkdir(parents=True, exist_ok=True)
    if output_zip_path.exists():
        output_zip_path.unlink()
    with zipfile.ZipFile(output_zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for filename in required_files:
            archive.write(bin_directory / filename, arcname=filename)
    return output_zip_path


def write_sha256sums(directory: Path, output_filename: str = "SHA256SUMS.txt") -> Path:
    """Compute SHA-256 checksums for all regular files in directory."""
    lines: list[str] = []
    for item in sorted(directory.iterdir()):
        if not item.is_file() or item.name == output_filename:
            continue
        digest = hashlib.sha256(item.read_bytes()).hexdigest()
        lines.append(f"{digest}  {item.name}")
    checksums_path = directory / output_filename
    checksums_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return checksums_path
