"""Unit tests for iOS template bundle assembly."""

from __future__ import annotations

import plistlib
import tempfile
import unittest
import zipfile
from pathlib import Path

from cafecito_build.cli import build_argument_parser
from cafecito_build.packaging import package_ios_template_bundle


def _create_fake_ios_skeleton(repo_root: Path) -> None:
    skeleton = repo_root / "misc" / "dist" / "apple_embedded_xcode"
    for framework_name in (
        "libgodot.ios.release.xcframework",
        "libgodot.ios.debug.xcframework",
        "libgodot_camera.ios.release.xcframework",
        "libgodot_camera.ios.debug.xcframework",
        "libgodot.visionos.release.xcframework",
    ):
        framework_dir = skeleton / framework_name
        (framework_dir / "ios-arm64").mkdir(parents=True, exist_ok=True)
        (framework_dir / "ios-arm64_x86_64-simulator").mkdir(parents=True, exist_ok=True)
        plist_payload = {
            "AvailableLibraries": [
                {
                    "LibraryIdentifier": "ios-arm64",
                    "SupportedArchitectures": ["arm64"],
                },
                {
                    "LibraryIdentifier": "ios-arm64_x86_64-simulator",
                    "SupportedArchitectures": ["arm64", "x86_64"],
                },
            ]
        }
        (framework_dir / "Info.plist").write_bytes(plistlib.dumps(plist_payload))
    (skeleton / "data.pck").write_bytes(b"")


def _create_fake_moltenvk(root: Path) -> Path:
    moltenvk = root / "MoltenVK.xcframework"
    (moltenvk / "ios-arm64").mkdir(parents=True, exist_ok=True)
    (moltenvk / "ios-arm64_x86_64-simulator").mkdir(parents=True, exist_ok=True)
    (moltenvk / "ios-arm64" / "libMoltenVK.a").write_bytes(b"mvk-device")
    (moltenvk / "ios-arm64_x86_64-simulator" / "libMoltenVK.a").write_bytes(b"mvk-sim")
    (moltenvk / "Info.plist").write_bytes(plistlib.dumps({"CFBundlePackageType": "XFWK"}))
    return moltenvk


def _create_fake_ios_slices(bin_dir: Path) -> None:
    bin_dir.mkdir(parents=True, exist_ok=True)
    for filename in (
        "libgodot.ios.template_release.arm64.a",
        "libgodot.ios.template_debug.arm64.a",
        "libgodot.ios.template_release.arm64.simulator.a",
        "libgodot.ios.template_debug.arm64.simulator.a",
        "libgodot_camera.ios.template_release.arm64.a",
        "libgodot_camera.ios.template_debug.arm64.a",
    ):
        (bin_dir / filename).write_bytes(f"data:{filename}".encode("utf-8"))


class PackageIosTemplateBundleTest(unittest.TestCase):
    def test_packages_slices_moltenvk_and_normalizes_arm64_simulator(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_root:
            root = Path(temporary_root)
            repo_root = root / "repo"
            bin_dir = repo_root / "bin"
            _create_fake_ios_skeleton(repo_root)
            _create_fake_ios_slices(bin_dir)
            moltenvk = _create_fake_moltenvk(root)
            output_zip = root / "out" / "ios.zip"

            result = package_ios_template_bundle(
                repo_root,
                bin_dir,
                output_zip,
                moltenvk_xcframework=moltenvk,
            )

            self.assertEqual(result, output_zip)
            with zipfile.ZipFile(output_zip, "r") as archive:
                names = set(archive.namelist())
                self.assertIn("libgodot.ios.release.xcframework/ios-arm64/libgodot.a", names)
                self.assertIn("libgodot.ios.release.xcframework/ios-arm64-simulator/libgodot.a", names)
                self.assertIn("libgodot.ios.debug.xcframework/ios-arm64/libgodot.a", names)
                self.assertIn("libgodot.ios.debug.xcframework/ios-arm64-simulator/libgodot.a", names)
                self.assertIn("libgodot_camera.ios.release.xcframework/ios-arm64/libgodot_camera.a", names)
                self.assertIn("libgodot_camera.ios.debug.xcframework/ios-arm64/libgodot_camera.a", names)
                self.assertIn("MoltenVK.xcframework/ios-arm64/libMoltenVK.a", names)
                self.assertIn("MoltenVK.xcframework/ios-arm64_x86_64-simulator/libMoltenVK.a", names)
                self.assertIn("MoltenVK.xcframework/Info.plist", names)
                self.assertFalse(any("visionos" in name for name in names))
                self.assertFalse(any("libgodot.ios." in name and "ios-arm64_x86_64-simulator" in name for name in names))

                release_plist = plistlib.loads(archive.read("libgodot.ios.release.xcframework/Info.plist"))
                sim_entry = release_plist["AvailableLibraries"][1]
                self.assertEqual(sim_entry["LibraryIdentifier"], "ios-arm64-simulator")
                self.assertEqual(sim_entry["SupportedArchitectures"], ["arm64"])

    def test_fails_closed_when_required_slice_is_missing(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_root:
            root = Path(temporary_root)
            repo_root = root / "repo"
            bin_dir = repo_root / "bin"
            _create_fake_ios_skeleton(repo_root)
            _create_fake_ios_slices(bin_dir)
            (bin_dir / "libgodot.ios.template_debug.arm64.simulator.a").unlink()
            moltenvk = _create_fake_moltenvk(root)

            with self.assertRaises(FileNotFoundError):
                package_ios_template_bundle(
                    repo_root,
                    bin_dir,
                    root / "out" / "ios.zip",
                    moltenvk_xcframework=moltenvk,
                )

    def test_cli_parses_package_ios_template_arguments(self) -> None:
        parser = build_argument_parser()
        arguments = parser.parse_args([
            "package-ios-template",
            "--bin-dir",
            "bin",
            "--output-zip",
            "release-out/ios.zip",
            "--moltenvk-xcframework",
            "/tmp/MoltenVK.xcframework",
        ])
        self.assertEqual(arguments.bin_dir, "bin")
        self.assertEqual(arguments.output_zip, "release-out/ios.zip")
        self.assertEqual(arguments.moltenvk_xcframework, "/tmp/MoltenVK.xcframework")
        self.assertFalse(arguments.keep_x86_64_simulator)


if __name__ == "__main__":
    unittest.main()
