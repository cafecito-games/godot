"""Unit tests for Apple LibGodot SDK packaging and BUILD_INFO.json provenance."""

from __future__ import annotations

import hashlib
import json
import os
import stat
import tempfile
import unittest
import zipfile
from collections.abc import Mapping
from pathlib import Path
from typing import Any
from unittest.mock import patch

from cafecito_build.apple_libgodot import (
    APPLE_LIBGODOT_SLICES,
    BUILD_INFO_FILENAME,
    EXTENSION_API_FILENAME,
    SHARED_APPLE_LIBGODOT_FLAGS,
    STATIC_PACKAGE_ASSETS,
    build_apple_libgodot_build_info,
    compute_file_sha256,
    package_apple_libgodot_sdk,
    validate_build_info_identity,
)
from cafecito_build.cli import build_argument_parser
from cafecito_build.identity import BuildIdentity
from cafecito_build.scons import BuildRequest


def _create_fake_repository(root: Path, configuration: str = "release") -> None:
    bin_directory = root / "bin"
    bin_directory.mkdir(parents=True, exist_ok=True)
    _target, slices = APPLE_LIBGODOT_SLICES[configuration]
    for slice_specification in slices:
        (bin_directory / slice_specification.output_filename).write_bytes(
            f"binary:{slice_specification.output_filename}".encode("utf-8")
        )

    for relative_source in STATIC_PACKAGE_ASSETS.values():
        source_path = root / relative_source
        source_path.parent.mkdir(parents=True, exist_ok=True)
        source_path.write_text(f"asset:{relative_source}\n", encoding="utf-8")


def _write_fake_editor(editor_path: Path, payload: Mapping[str, Any]) -> None:
    editor_path.parent.mkdir(parents=True, exist_ok=True)
    script = (
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "if '--headless' not in sys.argv or '--dump-extension-api' not in sys.argv:\n"
        "    sys.exit(2)\n"
        f"pathlib.Path('{EXTENSION_API_FILENAME}').write_text("
        f"json.dumps({payload!r}, indent=2) + '\\n', encoding='utf-8')\n"
    )
    editor_path.write_text(script, encoding="utf-8")
    current_mode = editor_path.stat().st_mode
    editor_path.chmod(current_mode | stat.S_IXUSR)


class AppleLibGodotSliceMatrixTest(unittest.TestCase):
    def test_release_and_debug_matrices_and_flags(self) -> None:
        target, release_slices = APPLE_LIBGODOT_SLICES["release"]
        self.assertEqual(target, "template_release")
        self.assertEqual(
            [item.output_filename for item in release_slices],
            [
                "libgodot.macos.template_release.arm64.dylib",
                "libgodot.macos.template_release.x86_64.dylib",
                "libgodot.ios.template_release.arm64.a",
                "libgodot.ios.template_release.arm64.simulator.a",
            ],
        )

        macos_flags = release_slices[0].scons_flags(target)
        self.assertEqual(
            macos_flags,
            sorted([
                "platform=macos",
                "arch=arm64",
                "library_type=shared_library",
                "target=template_release",
                *SHARED_APPLE_LIBGODOT_FLAGS,
            ]),
        )

        ios_device_flags = release_slices[2].scons_flags(target)
        ios_simulator_flags = release_slices[3].scons_flags(target)
        self.assertIn("ios_simulator=no", ios_device_flags)
        self.assertIn("ios_simulator=yes", ios_simulator_flags)


class AppleLibGodotBuildInfoTest(unittest.TestCase):
    def test_build_info_uses_cafecito_godot_commit_and_excludes_secrets(self) -> None:
        identity = BuildIdentity(
            commit_sha="0ae46154b",
            dirty=False,
            godot_version="4.7.2",
            godot_status="stable",
        )
        target, slices = APPLE_LIBGODOT_SLICES["release"]
        file_hashes = {"bin/libgodot.macos.template_release.arm64.dylib": "abc123"}

        build_info = build_apple_libgodot_build_info(identity, target, slices, file_hashes)

        self.assertEqual(build_info["cafecito_godot_commit"], "0ae46154b")
        self.assertEqual(build_info["godot_commit"], "0ae46154b")
        self.assertNotIn("custom_godot_commit", build_info)
        self.assertEqual(build_info["godot_version"], "4.7.2.stable")
        self.assertEqual(build_info["build_name"], "cafecito")
        self.assertEqual(build_info["target"], "template_release")
        self.assertEqual(build_info["files"], file_hashes)
        self.assertEqual(len(build_info["slices"]), 4)
        self.assertEqual(build_info["slices"][0]["platform"], "macos")
        serialized = json.dumps(build_info).lower()
        self.assertNotIn("aes", serialized)
        self.assertNotIn("encryption", serialized)

    def test_validate_build_info_identity_accepts_valid_and_rejects_drift(self) -> None:
        identity = BuildIdentity("0ae46154b", False, "4.7.2", "stable")
        with tempfile.TemporaryDirectory() as temporary_root:
            source_file = Path(temporary_root) / "file.bin"
            source_file.write_bytes(b"hello")
            valid_info = {
                "cafecito_godot_commit": "0ae46154b",
                "godot_commit": "0ae46154b",
                "godot_version": "4.7.2.stable",
                "build_name": "cafecito",
                "files": {"bin/file.bin": compute_file_sha256(source_file)},
            }
            validate_build_info_identity(valid_info, identity, {"bin/file.bin": source_file})

            wrong_commit = dict(valid_info, cafecito_godot_commit="111111111")
            with self.assertRaises(ValueError):
                validate_build_info_identity(wrong_commit, identity, {"bin/file.bin": source_file})

            wrong_hash = dict(valid_info, files={"bin/file.bin": "deadbeef"})
            with self.assertRaises(ValueError):
                validate_build_info_identity(wrong_hash, identity, {"bin/file.bin": source_file})

            extra_entry = dict(
                valid_info,
                files={
                    "bin/file.bin": compute_file_sha256(source_file),
                    "LICENSE.txt": "0000",
                },
            )
            with self.assertRaises(ValueError):
                validate_build_info_identity(extra_entry, identity, {"bin/file.bin": source_file})


class AppleLibGodotPackagingTest(unittest.TestCase):
    def setUp(self) -> None:
        self.identity = BuildIdentity(
            commit_sha="e33683412",
            dirty=False,
            godot_version="4.7.2",
            godot_status="stable",
        )

    def test_package_produces_exact_ten_entries_and_matching_checksums(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_root:
            root = Path(temporary_root)
            repository_root = root / "repo"
            output_directory = root / "release-out"
            _create_fake_repository(repository_root, "release")

            extension_api = root / "pre_dumped_extension_api.json"
            extension_api.write_text('{"classes": []}\n', encoding="utf-8")
            output_zip = output_directory / "apple-libgodot-release.zip"

            with patch(
                "cafecito_build.apple_libgodot.resolve_build_identity",
                return_value=self.identity,
            ):
                packaged = package_apple_libgodot_sdk(
                    repository_root,
                    "release",
                    output_zip,
                    extension_api_path=extension_api,
                )

            self.assertEqual(packaged, output_zip)
            self.assertEqual(sorted(item.name for item in output_directory.iterdir()), ["apple-libgodot-release.zip"])

            expected_entries = sorted([
                "bin/libgodot.macos.template_release.arm64.dylib",
                "bin/libgodot.macos.template_release.x86_64.dylib",
                "bin/libgodot.ios.template_release.arm64.a",
                "bin/libgodot.ios.template_release.arm64.simulator.a",
                "include/libgodot.h",
                "include/gdextension_interface.gen.h",
                "extension_api.json",
                "LICENSE.txt",
                "COPYRIGHT.txt",
                "BUILD_INFO.json",
            ])
            with zipfile.ZipFile(output_zip, "r") as archive:
                names = sorted(archive.namelist())
                self.assertEqual(len(names), 10)
                self.assertEqual(names, expected_entries)
                self.assertNotIn("manifest.json", names)

                build_info = json.loads(archive.read(BUILD_INFO_FILENAME).decode("utf-8"))
                self.assertEqual(build_info["cafecito_godot_commit"], "e33683412")
                self.assertEqual(build_info["godot_commit"], "e33683412")
                self.assertEqual(build_info["godot_version"], "4.7.2.stable")
                self.assertEqual(build_info["build_name"], "cafecito")
                self.assertEqual(build_info["target"], "template_release")
                self.assertEqual(len(build_info["files"]), 9)
                for entry_name, recorded_digest in build_info["files"].items():
                    actual_digest = hashlib.sha256(archive.read(entry_name)).hexdigest()
                    self.assertEqual(recorded_digest, actual_digest)

    def test_package_dumps_extension_api_from_editor_binary(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_root:
            root = Path(temporary_root)
            repository_root = root / "repo"
            _create_fake_repository(repository_root, "release")

            editor_binary = repository_root / "bin" / "godot.macos.editor.arm64"
            expected_api = {"header": {"version_major": 4}, "classes": [{"name": "Window"}]}
            _write_fake_editor(editor_binary, expected_api)

            output_zip = root / "out" / "apple-libgodot-release.zip"
            with patch(
                "cafecito_build.apple_libgodot.resolve_build_identity",
                return_value=self.identity,
            ):
                package_apple_libgodot_sdk(
                    repository_root,
                    "release",
                    output_zip,
                    editor_binary=editor_binary,
                )

            with zipfile.ZipFile(output_zip, "r") as archive:
                dumped = json.loads(archive.read(EXTENSION_API_FILENAME).decode("utf-8"))
                self.assertEqual(dumped, expected_api)

    def test_package_builds_editor_when_missing(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_root:
            root = Path(temporary_root)
            repository_root = root / "repo"
            _create_fake_repository(repository_root, "release")

            def fake_scons_build(request: BuildRequest) -> None:
                editor_path = repository_root / "bin" / f"godot.macos.editor.{request.architecture}"
                _write_fake_editor(editor_path, {"classes": []})

            output_zip = root / "out" / "apple-libgodot-release.zip"
            with (
                patch("cafecito_build.apple_libgodot.resolve_build_identity", return_value=self.identity),
                patch("cafecito_build.apple_libgodot.detect_architecture", return_value="arm64"),
                patch("cafecito_build.apple_libgodot.run_scons_build", side_effect=fake_scons_build) as mock_build,
            ):
                package_apple_libgodot_sdk(
                    repository_root,
                    "release",
                    output_zip,
                    jobs=6,
                    extra_flags=("cache_path=/tmp/scons-cache",),
                )

            mock_build.assert_called_once()
            build_request = mock_build.call_args.args[0]
            self.assertEqual(build_request.godot_platform, "macos")
            self.assertEqual(build_request.target, "editor")
            self.assertEqual(build_request.architecture, "arm64")
            self.assertFalse(build_request.relink_kit)
            self.assertEqual(build_request.jobs, 6)
            self.assertEqual(
                build_request.extra_flags,
                (*SHARED_APPLE_LIBGODOT_FLAGS, "optimize=none", "cache_path=/tmp/scons-cache"),
            )

    def test_package_fails_closed_when_required_static_asset_missing(self) -> None:
        for relative_source in STATIC_PACKAGE_ASSETS.values():
            with self.subTest(missing=relative_source):
                with tempfile.TemporaryDirectory() as temporary_root:
                    root = Path(temporary_root)
                    repository_root = root / "repo"
                    _create_fake_repository(repository_root, "release")
                    os.unlink(repository_root / relative_source)
                    extension_api = root / "extension_api.json"
                    extension_api.write_text('{"classes": []}\n', encoding="utf-8")

                    with patch(
                        "cafecito_build.apple_libgodot.resolve_build_identity",
                        return_value=self.identity,
                    ):
                        with self.assertRaises(FileNotFoundError):
                            package_apple_libgodot_sdk(
                                repository_root,
                                "release",
                                root / "apple-libgodot-release.zip",
                                extension_api_path=extension_api,
                            )

    def test_package_rejects_invalid_extension_api_json(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_root:
            root = Path(temporary_root)
            repository_root = root / "repo"
            _create_fake_repository(repository_root, "release")
            bad_api = root / "extension_api.json"
            bad_api.write_text("{not-json", encoding="utf-8")

            with patch(
                "cafecito_build.apple_libgodot.resolve_build_identity",
                return_value=self.identity,
            ):
                with self.assertRaises(ValueError):
                    package_apple_libgodot_sdk(
                        repository_root,
                        "release",
                        root / "apple-libgodot-release.zip",
                        extension_api_path=bad_api,
                    )


class AppleLibGodotCliTest(unittest.TestCase):
    def test_build_apple_libgodot_arguments(self) -> None:
        parser = build_argument_parser()
        arguments = parser.parse_args([
            "build-apple-libgodot",
            "--configuration",
            "release",
            "--extra-flag",
            "cache_path=/tmp/cache",
            "--extra-flag",
            "cache_limit=7",
        ])
        self.assertEqual(arguments.configuration, "release")
        self.assertEqual(arguments.extra_flag, ["cache_path=/tmp/cache", "cache_limit=7"])

    def test_package_apple_libgodot_arguments(self) -> None:
        parser = build_argument_parser()
        arguments = parser.parse_args([
            "package-apple-libgodot",
            "--configuration",
            "debug",
            "--output",
            "release-out/apple-libgodot-debug.zip",
            "--editor-binary",
            "bin/godot.macos.editor.arm64",
            "--extension-api",
            "tmp/extension_api.json",
            "--jobs",
            "8",
            "--extra-flag",
            "cache_path=/tmp/cache",
        ])
        self.assertEqual(arguments.configuration, "debug")
        self.assertEqual(arguments.output, "release-out/apple-libgodot-debug.zip")
        self.assertEqual(arguments.editor_binary, "bin/godot.macos.editor.arm64")
        self.assertEqual(arguments.extension_api, "tmp/extension_api.json")
        self.assertEqual(arguments.jobs, 8)
        self.assertEqual(arguments.extra_flag, ["cache_path=/tmp/cache"])


if __name__ == "__main__":
    unittest.main()
