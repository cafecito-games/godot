"""Unit tests for Relink Kit packaging order and its architecture guard."""

from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from cafecito_build.apple_libgodot import APPLE_LIBGODOT_SLICES, build_apple_libgodot_slices
from cafecito_build.cli import build_argument_parser
from cafecito_build.relink_kit import package_relink_kit


def _lipo_result(architectures: str) -> subprocess.CompletedProcess[str]:
    return subprocess.CompletedProcess(
        args=["lipo", "-archs"], returncode=0, stdout=f"{architectures}\n", stderr=""
    )


class RelinkKitArchitectureGuardTest(unittest.TestCase):
    """The guard that fails a kit whose inputs are not the architecture it names."""

    def _write_kit_inputs(self, root: Path, platform: str, architecture: str) -> Path:
        object_relative = "bin/obj/platform/macos/os_macos.os"
        library_relative = f"bin/obj/main/libmain.macos.template_release.{architecture}.a"
        for relative in (object_relative, library_relative):
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"\xcf\xfa\xed\xfe")

        manifest = {
            "platform": platform,
            "arch": architecture,
            "target": "template_release",
            "library_type": "shared_library",
            "target_kind": "shared_library",
            "output_filename": f"libgodot.macos.template_release.{architecture}.dylib",
            "compile": {"defines": ["-DLIBGODOT_ENABLED"]},
            "link": {"linker": "clang++"},
            "object_files": [object_relative],
            "static_libraries": [library_relative],
            "headers": [],
        }
        manifest_path = root / "bin" / "libgodot.relink.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        return manifest_path

    def test_packaging_refuses_an_object_built_for_another_architecture(self) -> None:
        """The exact failure that shipped an engine-free libgodot: x86_64 objects in an arm64 kit.

        The linker only warns about a wrong-architecture object and pulls archive
        members lazily, so the relink produced a valid, nearly empty dylib and
        exited zero. Nothing downstream could tell, which is why packaging has to
        refuse the input.
        """
        with tempfile.TemporaryDirectory() as root_name:
            root = Path(root_name)
            manifest_path = self._write_kit_inputs(root, "macos", "arm64")
            with patch(
                "cafecito_build.relink_kit.subprocess.run", return_value=_lipo_result("x86_64")
            ):
                with self.assertRaises(ValueError) as raised:
                    package_relink_kit(root, manifest_path, root / "out", create_archive=False)
        message = str(raised.exception)
        self.assertIn("x86_64", message)
        self.assertIn("arm64", message)

    def test_packaging_accepts_inputs_matching_the_manifest_architecture(self) -> None:
        with tempfile.TemporaryDirectory() as root_name:
            root = Path(root_name)
            manifest_path = self._write_kit_inputs(root, "macos", "arm64")
            with patch(
                "cafecito_build.relink_kit.subprocess.run", return_value=_lipo_result("arm64")
            ):
                kit_directory = package_relink_kit(
                    root, manifest_path, root / "out", create_archive=False
                )
            self.assertTrue((kit_directory / "bin/obj/platform/macos/os_macos.os").is_file())
            self.assertTrue(
                (kit_directory / "bin/obj/main/libmain.macos.template_release.arm64.a").is_file()
            )
            self.assertTrue((kit_directory / "relink_manifest.json").is_file())

    def test_non_apple_kits_are_not_inspected_with_lipo(self) -> None:
        """`lipo` reads Mach-O only, so a Linux or Android kit must not be passed to it."""
        with tempfile.TemporaryDirectory() as root_name:
            root = Path(root_name)
            manifest_path = self._write_kit_inputs(root, "linuxbsd", "x86_64")
            with patch("cafecito_build.relink_kit.subprocess.run") as run_mock:
                package_relink_kit(root, manifest_path, root / "out", create_archive=False)
        run_mock.assert_not_called()


class AppleLibGodotKitPackagingOrderTest(unittest.TestCase):
    """Each slice's kit must be packaged before the next slice overwrites its objects."""

    def test_each_kit_is_packaged_immediately_after_its_own_slice_builds(self) -> None:
        target, slices = APPLE_LIBGODOT_SLICES["release"]
        events: list[tuple[str, str]] = []

        def record_build(request: object) -> None:
            events.append(("build", f"{request.godot_platform}-{request.architecture}"))  # type: ignore[attr-defined]

        def record_package(repository_root: Path, manifest_path: Path, output_directory: Path) -> Path:
            events.append(("package", manifest_path.name))
            return output_directory / "kit.tar.zst"

        with tempfile.TemporaryDirectory() as root_name:
            root = Path(root_name)
            with (
                patch("cafecito_build.apple_libgodot.run_scons_build", side_effect=record_build),
                patch("cafecito_build.apple_libgodot.package_relink_kit", side_effect=record_package),
            ):
                build_apple_libgodot_slices(
                    root, "release", kit_output_directory=root / "release-out"
                )

        self.assertEqual(len(events), 2 * len(slices))
        for index, slice_specification in enumerate(slices):
            build_event, package_event = events[2 * index], events[2 * index + 1]
            self.assertEqual(build_event[0], "build")
            self.assertEqual(package_event[0], "package")
            self.assertEqual(
                package_event[1], f"{slice_specification.output_filename}.relink.json"
            )
        self.assertEqual(target, "template_release")

    def test_no_kits_are_packaged_without_an_output_directory(self) -> None:
        with tempfile.TemporaryDirectory() as root_name:
            root = Path(root_name)
            with (
                patch("cafecito_build.apple_libgodot.run_scons_build"),
                patch("cafecito_build.apple_libgodot.package_relink_kit") as package_mock,
            ):
                build_apple_libgodot_slices(root, "release")
        package_mock.assert_not_called()


class AppleLibGodotKitCliTest(unittest.TestCase):
    def test_build_apple_libgodot_accepts_a_kit_output_directory(self) -> None:
        parser = build_argument_parser()
        arguments = parser.parse_args(
            ["build-apple-libgodot", "--configuration", "release", "--kit-output-dir", "release-out"]
        )
        self.assertEqual(arguments.kit_output_dir, "release-out")
        self.assertIsNone(
            parser.parse_args(["build-apple-libgodot"]).kit_output_dir,
            "packaging must stay opt-in so a plain build does not write kits",
        )


if __name__ == "__main__":
    unittest.main()
