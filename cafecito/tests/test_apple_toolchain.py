"""Unit tests for fail-closed Xcode / iOS SDK selection."""

from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from cafecito_build.apple_toolchain import (
    XcodeCandidate,
    _append_github_path,
    choose_best_xcode,
    parse_sdk_version,
)


class AppleToolchainTest(unittest.TestCase):
    def test_parse_sdk_version_rejects_malformed_and_parses_valid(self) -> None:
        self.assertEqual(parse_sdk_version("27.0"), (27, 0, 0))
        self.assertEqual(parse_sdk_version("27.2.1"), (27, 2, 1))
        self.assertEqual(parse_sdk_version("28"), (28, 0, 0))
        for invalid in ("", "   ", "iphoneos27.0", "27.0-beta", "abc"):
            with self.assertRaises(ValueError):
                parse_sdk_version(invalid)

    def test_rejects_below_floor_and_accepts_at_or_above_floor(self) -> None:
        sdk26 = XcodeCandidate(
            developer_directory=Path("/Applications/Xcode_26.5.app/Contents/Developer"),
            xcode_version_text="Xcode 26.5",
            ios_sdk_version="26.5",
            ios_sdk_tuple=(26, 5, 0),
            is_prerelease=False,
        )
        with self.assertRaises(RuntimeError):
            choose_best_xcode([sdk26], 27)

        sdk27_beta = XcodeCandidate(
            developer_directory=Path("/Applications/Xcode_27.1_beta.app/Contents/Developer"),
            xcode_version_text="Xcode 27.1 Beta",
            ios_sdk_version="27.1",
            ios_sdk_tuple=(27, 1, 0),
            is_prerelease=True,
        )
        sdk27_stable = XcodeCandidate(
            developer_directory=Path("/Applications/Xcode_27.0.app/Contents/Developer"),
            xcode_version_text="Xcode 27.0",
            ios_sdk_version="27.0",
            ios_sdk_tuple=(27, 0, 0),
            is_prerelease=False,
        )
        chosen = choose_best_xcode([sdk26, sdk27_beta, sdk27_stable], 27)
        self.assertEqual(chosen, sdk27_stable)

        sdk28_stable = XcodeCandidate(
            developer_directory=Path("/Applications/Xcode_28.0.app/Contents/Developer"),
            xcode_version_text="Xcode 28.0",
            ios_sdk_version="28.0",
            ios_sdk_tuple=(28, 0, 0),
            is_prerelease=False,
        )
        self.assertEqual(choose_best_xcode([sdk27_stable, sdk28_stable], 27), sdk28_stable)

    def test_append_github_path_writes_existing_toolchain_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_root:
            root = Path(temporary_root)
            github_path_file = root / "github_path.txt"
            toolchain_bin = root / "Xcode.app" / "Contents" / "Developer" / "Toolchains" / "usr" / "bin"
            toolchain_bin.mkdir(parents=True)

            with patch.dict(os.environ, {"GITHUB_PATH": str(github_path_file)}):
                _append_github_path(toolchain_bin)
                _append_github_path(root / "missing")

            self.assertEqual(github_path_file.read_text(encoding="utf-8"), f"{toolchain_bin}\n")


if __name__ == "__main__":
    unittest.main()
