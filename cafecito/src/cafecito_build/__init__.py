"""Build, packaging, and Relink Kit library for the Cafecito Godot fork."""

from cafecito_build.environment import HostEnvironment, detect_host_environment
from cafecito_build.identity import BuildIdentity, resolve_build_identity
from cafecito_build.relink_kit import package_relink_kit
from cafecito_build.scons import BuildRequest, run_scons_build

__all__ = [
    "BuildIdentity",
    "BuildRequest",
    "HostEnvironment",
    "detect_host_environment",
    "package_relink_kit",
    "resolve_build_identity",
    "run_scons_build",
]
