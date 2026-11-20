"""Apple LibGodot SDK slice builder for SwiftGodotKit with Relink Kit support."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

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

    def extra_flags(self) -> tuple[str, ...]:
        flags = list(SHARED_APPLE_LIBGODOT_FLAGS)
        if self.godot_platform == "ios":
            flags.append(f"ios_simulator={'yes' if self.simulator else 'no'}")
        return tuple(flags)


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


def build_apple_libgodot_slices(
    repository_root: Path,
    configuration: str,
    *,
    relink_kit: bool = True,
    jobs: int | None = None,
) -> list[Path]:
    """Build all four Apple LibGodot slices for the requested configuration."""
    if configuration not in APPLE_LIBGODOT_SLICES:
        raise ValueError(f"Unsupported Apple LibGodot configuration: {configuration!r}")

    target, slices = APPLE_LIBGODOT_SLICES[configuration]
    built_outputs: list[Path] = []
    for slice_spec in slices:
        run_scons_build(
            BuildRequest(
                repository_root=repository_root,
                godot_platform=slice_spec.godot_platform,
                target=target,
                architecture=slice_spec.architecture,
                library_type=slice_spec.library_type,
                relink_kit=relink_kit,
                jobs=jobs,
                extra_flags=slice_spec.extra_flags(),
            )
        )
        built_outputs.append(repository_root / "bin" / slice_spec.output_filename)
    return built_outputs
