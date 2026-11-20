"""SCons build orchestration for the Cafecito Godot fork."""

from __future__ import annotations

import shutil
import subprocess
from dataclasses import dataclass, field
from pathlib import Path

from cafecito_build.environment import build_process_environment, detect_host_environment
from cafecito_build.identity import resolve_build_identity

DEFAULT_SCONS_FLAGS: tuple[str, ...] = (
    "scu_build=yes",
    "disable_path_overrides=no",
    "module_mono_enabled=no",
    "module_mobile_vr_enabled=no",
    "module_openxr_enabled=no",
    "module_webxr_enabled=no",
    "module_csg_enabled=no",
    "module_gridmap_enabled=no",
    "module_raycast_enabled=no",
    "module_lightmapper_rd_enabled=no",
    "module_xatlas_unwrap_enabled=no",
    "module_meshoptimizer_enabled=no",
)


@dataclass(frozen=True)
class BuildRequest:
    repository_root: Path
    godot_platform: str
    target: str
    architecture: str
    library_type: str = "executable"
    relink_kit: bool = True
    extension_library: Path | None = None
    debug_symbols: bool = False
    tests: bool = False
    use_default_module_profile: bool = True
    jobs: int | None = None
    extra_flags: tuple[str, ...] = field(default_factory=tuple)


def _find_scons_executable() -> str:
    scons_path = shutil.which("scons")
    if scons_path is None:
        raise RuntimeError("SCons executable was not found on PATH. Install it with `uv tool install scons`.")
    return scons_path


def _uses_vulkan(request: BuildRequest) -> bool:
    for flag in request.extra_flags:
        if flag.strip() == "vulkan=no":
            return False
    return request.godot_platform == "macos"


def run_scons_build(request: BuildRequest) -> None:
    host_environment = detect_host_environment()
    identity = resolve_build_identity(request.repository_root)
    process_environment = build_process_environment(
        host_environment,
        require_vulkan_on_macos=_uses_vulkan(request),
    )
    process_environment["BUILD_NAME"] = identity.build_name

    job_count = request.jobs or host_environment.cpu_count
    command = [
        _find_scons_executable(),
        f"platform={request.godot_platform}",
        f"target={request.target}",
        f"arch={request.architecture}",
        f"library_type={request.library_type}",
        f"debug_symbols={'yes' if request.debug_symbols else 'no'}",
        f"cafecito_relink_kit={'yes' if request.relink_kit else 'no'}",
        f"-j{job_count}",
    ]

    if request.use_default_module_profile:
        command.extend(DEFAULT_SCONS_FLAGS)
    if request.tests:
        command.append("tests=yes")
    if request.extension_library is not None:
        command.append(f"cafecito_ext_library={request.extension_library.resolve()}")
    command.extend(request.extra_flags)

    print(f"[cafecito-build] BUILD_NAME={identity.build_name}")
    print(f"[cafecito-build] Running: {' '.join(command)}")
    subprocess.run(command, cwd=request.repository_root, env=process_environment, check=True)
