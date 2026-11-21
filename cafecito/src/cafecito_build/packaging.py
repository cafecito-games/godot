"""Packaging utilities for macOS, iOS, Android, and release checksum artifacts."""

from __future__ import annotations

import hashlib
import plistlib
import shutil
import tempfile
import zipfile
from pathlib import Path

from cafecito_build.environment import detect_vulkan_sdk_macos


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


def _zip_directory_contents(source_directory: Path, output_zip: Path) -> Path:
    output_zip.parent.mkdir(parents=True, exist_ok=True)
    if output_zip.exists():
        output_zip.unlink()
    with zipfile.ZipFile(output_zip, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for item in sorted(source_directory.rglob("*")):
            if item.is_dir():
                continue
            archive.write(item, arcname=item.relative_to(source_directory).as_posix())
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


def _detect_moltenvk_xcframework(explicit_path: Path | None = None) -> Path:
    candidates: list[Path] = []
    if explicit_path is not None:
        candidates.append(explicit_path)
    else:
        vulkan_sdk_macos = detect_vulkan_sdk_macos()
        if vulkan_sdk_macos is not None:
            candidates.append(vulkan_sdk_macos / "lib" / "MoltenVK.xcframework")
            candidates.append(vulkan_sdk_macos.parent / "MoltenVK" / "MoltenVK.xcframework")
        candidates.extend(
            [
                Path("/opt/homebrew/Frameworks/MoltenVK.xcframework"),
                Path("/usr/local/homebrew/Frameworks/MoltenVK.xcframework"),
                Path("/opt/local/Frameworks/MoltenVK.xcframework"),
            ]
        )

    for candidate in candidates:
        if (candidate / "ios-arm64" / "libMoltenVK.a").is_file():
            return candidate
    raise FileNotFoundError(
        "MoltenVK.xcframework with ios-arm64/libMoltenVK.a not found. "
        "Install the Vulkan SDK or pass --moltenvk-xcframework."
    )


def _normalize_ios_simulator_xcframework(xcframework_directory: Path) -> None:
    legacy_simulator_directory = xcframework_directory / "ios-arm64_x86_64-simulator"
    single_arch_directory = xcframework_directory / "ios-arm64-simulator"
    if legacy_simulator_directory.is_dir():
        if single_arch_directory.exists():
            shutil.rmtree(single_arch_directory)
        legacy_simulator_directory.rename(single_arch_directory)

    info_plist_path = xcframework_directory / "Info.plist"
    if info_plist_path.is_file():
        plist = plistlib.loads(info_plist_path.read_bytes())
        for library in plist.get("AvailableLibraries", []):
            if library.get("LibraryIdentifier") == "ios-arm64_x86_64-simulator":
                library["LibraryIdentifier"] = "ios-arm64-simulator"
                library["SupportedArchitectures"] = ["arm64"]
        info_plist_path.write_bytes(plistlib.dumps(plist))


def package_ios_template_bundle(
    repository_root: Path,
    bin_directory: Path,
    output_ios_zip: Path,
    *,
    moltenvk_xcframework: Path | None = None,
    single_arch_arm64_simulator: bool = True,
) -> Path:
    """Assemble exporter-ready ios.zip directly from prebuilt iOS static slices and MoltenVK."""
    skeleton_directory = repository_root / "misc" / "dist" / "apple_embedded_xcode"
    if not skeleton_directory.is_dir():
        raise FileNotFoundError(f"iOS template skeleton not found: {skeleton_directory}")

    resolved_moltenvk = _detect_moltenvk_xcframework(moltenvk_xcframework)
    required_libraries = (
        ("libgodot.ios.template_release.arm64.a", "libgodot.ios.release.xcframework", "ios-arm64"),
        ("libgodot.ios.template_debug.arm64.a", "libgodot.ios.debug.xcframework", "ios-arm64"),
        (
            "libgodot.ios.template_release.arm64.simulator.a",
            "libgodot.ios.release.xcframework",
            "ios-arm64_x86_64-simulator",
        ),
        (
            "libgodot.ios.template_debug.arm64.simulator.a",
            "libgodot.ios.debug.xcframework",
            "ios-arm64_x86_64-simulator",
        ),
    )
    for filename, _, _ in required_libraries:
        candidate = bin_directory / filename
        if not candidate.is_file():
            raise FileNotFoundError(f"Required iOS static library missing: {candidate}")

    with tempfile.TemporaryDirectory() as temporary_root:
        staging_directory = Path(temporary_root) / "ios_xcode"
        shutil.copytree(skeleton_directory, staging_directory)

        for entry in list(staging_directory.iterdir()):
            if (entry.name.startswith("libgodot.") or entry.name.startswith("libgodot_")) and entry.name.endswith(
                ".xcframework"
            ):
                parts = entry.name.split(".")
                if len(parts) >= 3 and parts[1] != "ios":
                    shutil.rmtree(entry)

        for filename, framework_name, slice_directory in required_libraries:
            destination = staging_directory / framework_name / slice_directory / "libgodot.a"
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(bin_directory / filename, destination)

        for entry in sorted(staging_directory.iterdir()):
            if not (entry.name.startswith("libgodot_") and entry.name.endswith(".xcframework")):
                continue
            parts = entry.name.split(".")
            if len(parts) < 4 or parts[1] != "ios":
                continue
            module_stem = parts[0]
            build_kind = parts[2]
            scons_target = "template_release" if build_kind == "release" else "template_debug"
            for suffix, slice_directory in (
                ("", "ios-arm64"),
                (".simulator", "ios-arm64_x86_64-simulator"),
            ):
                source_library = bin_directory / f"{module_stem}.ios.{scons_target}.arm64{suffix}.a"
                if source_library.is_file():
                    destination = entry / slice_directory / f"{module_stem}.a"
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(source_library, destination)

        if single_arch_arm64_simulator:
            for framework_name in ("libgodot.ios.release.xcframework", "libgodot.ios.debug.xcframework"):
                _normalize_ios_simulator_xcframework(staging_directory / framework_name)

        moltenvk_destination = staging_directory / "MoltenVK.xcframework"
        moltenvk_destination.mkdir(parents=True, exist_ok=True)
        shutil.copytree(resolved_moltenvk / "ios-arm64", moltenvk_destination / "ios-arm64", dirs_exist_ok=True)
        simulator_moltenvk = resolved_moltenvk / "ios-arm64_x86_64-simulator"
        if simulator_moltenvk.is_dir():
            shutil.copytree(
                simulator_moltenvk,
                moltenvk_destination / "ios-arm64_x86_64-simulator",
                dirs_exist_ok=True,
            )
        shutil.copy2(resolved_moltenvk / "Info.plist", moltenvk_destination / "Info.plist")

        return _zip_directory_contents(staging_directory, output_ios_zip)


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
