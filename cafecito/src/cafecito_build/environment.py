"""Host platform, architecture, CPU, and SDK environment detection."""

from __future__ import annotations

import os
import platform
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class HostEnvironment:
    godot_platform: str
    architecture: str
    cpu_count: int
    vulkan_sdk_path: Path | None


def detect_godot_platform() -> str:
    system_name = platform.system().lower()
    if system_name == "darwin":
        return "macos"
    if system_name == "linux":
        return "linuxbsd"
    if system_name == "windows":
        return "windows"
    raise RuntimeError(f"Unsupported host platform: {system_name}")


def detect_architecture() -> str:
    machine = platform.machine().lower()
    if machine in ("arm64", "aarch64"):
        return "arm64"
    if machine in ("x86_64", "amd64"):
        return "x86_64"
    return machine


def detect_vulkan_sdk_macos() -> Path | None:
    configured = os.environ.get("VULKAN_SDK", "").strip()
    if configured:
        candidate = Path(configured)
        if candidate.is_dir():
            return candidate

    sdk_root = Path.home() / "VulkanSDK"
    if not sdk_root.is_dir():
        return None

    versions = sorted(
        (entry for entry in sdk_root.iterdir() if entry.is_dir()),
        key=lambda entry: [int(part) if part.isdigit() else part for part in entry.name.split(".")],
    )
    for version_directory in reversed(versions):
        macos_directory = version_directory / "macOS"
        if macos_directory.is_dir():
            return macos_directory
    return None


def detect_android_sdk() -> Path | None:
    for variable_name in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        configured = os.environ.get(variable_name, "").strip()
        if configured:
            candidate = Path(configured)
            if candidate.is_dir():
                return candidate

    for candidate in (
        Path.home() / "Library" / "Android" / "sdk",
        Path.home() / "Android" / "Sdk",
    ):
        if candidate.is_dir():
            return candidate
    return None


def detect_host_environment() -> HostEnvironment:
    godot_platform = detect_godot_platform()
    vulkan_sdk = detect_vulkan_sdk_macos() if godot_platform == "macos" else None
    return HostEnvironment(
        godot_platform=godot_platform,
        architecture=detect_architecture(),
        cpu_count=os.cpu_count() or 4,
        vulkan_sdk_path=vulkan_sdk,
    )


def build_process_environment(
    host_environment: HostEnvironment,
    *,
    require_vulkan_on_macos: bool = False,
    scons_cache_directory: Path | None = None,
    scons_cache_limit_mb: int | None = None,
) -> dict[str, str]:
    process_environment = dict(os.environ)

    if host_environment.godot_platform == "macos":
        if host_environment.vulkan_sdk_path is not None:
            sdk_path = str(host_environment.vulkan_sdk_path)
            process_environment.setdefault("VULKAN_SDK", sdk_path)
            process_environment.setdefault(
                "VK_ICD_FILENAMES",
                str(host_environment.vulkan_sdk_path / "share" / "vulkan" / "icd.d" / "MoltenVK_icd.json"),
            )
            process_environment.setdefault(
                "VK_LAYER_PATH",
                str(host_environment.vulkan_sdk_path / "share" / "vulkan" / "explicit_layer.d"),
            )
        elif require_vulkan_on_macos:
            raise RuntimeError(
                "VULKAN_SDK is not set and ~/VulkanSDK was not found. "
                "Install the Vulkan SDK for macOS before building with Vulkan enabled."
            )

    android_sdk = detect_android_sdk()
    if android_sdk is not None:
        process_environment.setdefault("ANDROID_HOME", str(android_sdk))
        process_environment.setdefault("ANDROID_SDK_ROOT", str(android_sdk))

    if scons_cache_directory is not None:
        process_environment["SCONS_CACHE"] = str(scons_cache_directory)
    if scons_cache_limit_mb is not None:
        process_environment["SCONS_CACHE_LIMIT"] = str(scons_cache_limit_mb)

    return process_environment
