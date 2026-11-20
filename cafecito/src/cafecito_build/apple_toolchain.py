"""Xcode and iOS SDK selection and fail-closed verification for Apple CI builds."""

from __future__ import annotations

import os
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

SDK_VERSION_PATTERN = re.compile(r"^([0-9]+)(?:\.([0-9]+))?(?:\.([0-9]+))?$")
PRERELEASE_PATTERN = re.compile(r"(beta|rc|seed|preview)", re.IGNORECASE)


@dataclass(frozen=True)
class XcodeCandidate:
    developer_directory: Path
    xcode_version_text: str
    ios_sdk_version: str
    ios_sdk_tuple: tuple[int, int, int]
    is_prerelease: bool


def parse_sdk_version(raw_version: str) -> tuple[int, int, int]:
    cleaned = raw_version.strip()
    match = SDK_VERSION_PATTERN.fullmatch(cleaned)
    if match is None:
        raise ValueError(f"Malformed iPhoneOS SDK version string: {raw_version!r}")
    major = int(match.group(1))
    minor = int(match.group(2) or 0)
    patch = int(match.group(3) or 0)
    return (major, minor, patch)


def choose_best_xcode(candidates: list[XcodeCandidate], min_ios_sdk_major: int) -> XcodeCandidate:
    """Choose the best installed Xcode satisfying min_ios_sdk_major, preferring stable releases."""
    eligible = [item for item in candidates if item.ios_sdk_tuple[0] >= min_ios_sdk_major]
    if not eligible:
        raise RuntimeError(f"No Xcode installation provides iPhoneOS SDK >= {min_ios_sdk_major}")

    # Prefer stable over prerelease, then highest SDK version, then deterministic path order.
    eligible.sort(
        key=lambda item: (
            0 if not item.is_prerelease else 1,
            tuple(-part for part in item.ios_sdk_tuple),
            str(item.developer_directory),
        )
    )
    return eligible[0]


def _discover_developer_directories(applications_root: Path = Path("/Applications")) -> list[Path]:
    discovered: list[Path] = []
    seen: set[Path] = set()

    def register(path: Path) -> None:
        if not path.is_dir():
            return
        resolved = path.resolve()
        if resolved not in seen:
            seen.add(resolved)
            discovered.append(resolved)

    if applications_root.is_dir():
        for app_bundle in sorted(applications_root.glob("Xcode*.app")):
            register(app_bundle / "Contents" / "Developer")

    env_developer_dir = os.environ.get("DEVELOPER_DIR", "").strip()
    if env_developer_dir:
        register(Path(env_developer_dir))

    try:
        active = subprocess.check_output(["xcode-select", "-p"], text=True, stderr=subprocess.DEVNULL).strip()
        if active:
            register(Path(active))
    except (FileNotFoundError, subprocess.CalledProcessError):
        pass

    return discovered


def _probe_developer_directory(developer_directory: Path) -> tuple[XcodeCandidate | None, str]:
    env = dict(os.environ)
    env["DEVELOPER_DIR"] = str(developer_directory)

    try:
        xcode_version_output = subprocess.check_output(
            ["xcodebuild", "-version"],
            env=env,
            text=True,
            stderr=subprocess.STDOUT,
        ).strip()
    except (FileNotFoundError, subprocess.CalledProcessError) as error:
        return None, f"{developer_directory}: xcodebuild -version failed ({error})"

    try:
        raw_sdk_version = subprocess.check_output(
            ["xcrun", "--sdk", "iphoneos", "--show-sdk-version"],
            env=env,
            text=True,
            stderr=subprocess.STDOUT,
        ).strip()
    except (FileNotFoundError, subprocess.CalledProcessError) as error:
        return None, f"{developer_directory}: xcrun iphoneos SDK query failed ({error})"

    try:
        sdk_tuple = parse_sdk_version(raw_sdk_version)
    except ValueError as error:
        return None, f"{developer_directory}: {error}"

    first_line = xcode_version_output.splitlines()[0] if xcode_version_output else ""
    is_prerelease = bool(
        PRERELEASE_PATTERN.search(developer_directory.as_posix()) or PRERELEASE_PATTERN.search(xcode_version_output)
    )
    candidate = XcodeCandidate(
        developer_directory=developer_directory,
        xcode_version_text=first_line,
        ios_sdk_version=raw_sdk_version,
        ios_sdk_tuple=sdk_tuple,
        is_prerelease=is_prerelease,
    )
    summary = (
        f"{developer_directory} | {first_line} | iphoneos SDK {raw_sdk_version} "
        f"({'prerelease' if is_prerelease else 'stable'})"
    )
    return candidate, summary


def _append_github_file(env_var_name: str, entries: dict[str, str]) -> None:
    target_path_str = os.environ.get(env_var_name, "").strip()
    if not target_path_str:
        return
    with Path(target_path_str).open("a", encoding="utf-8") as handle:
        for key, value in entries.items():
            handle.write(f"{key}={value}\n")


def select_and_verify_xcode(min_ios_sdk_major: int) -> XcodeCandidate:
    """Select an Xcode satisfying min_ios_sdk_major, log diagnostics, and export DEVELOPER_DIR."""
    developer_directories = _discover_developer_directories()
    candidates: list[XcodeCandidate] = []
    probe_lines: list[str] = []

    for directory in developer_directories:
        candidate, summary = _probe_developer_directory(directory)
        probe_lines.append(summary)
        if candidate is not None:
            candidates.append(candidate)

    print(f"[apple-toolchain] Required minimum iPhoneOS SDK major: {min_ios_sdk_major}")
    print("[apple-toolchain] Inspected Xcode installations:")
    for line in probe_lines or ["(none found)"]:
        print(f"  - {line}")

    try:
        selected = choose_best_xcode(candidates, min_ios_sdk_major)
    except RuntimeError as error:
        for directory in developer_directories:
            env = dict(os.environ)
            env["DEVELOPER_DIR"] = str(directory)
            sdks = subprocess.run(
                ["xcodebuild", "-showsdks"],
                env=env,
                check=False,
                capture_output=True,
                text=True,
            )
            print(f"[apple-toolchain] xcodebuild -showsdks for {directory}:\n{sdks.stdout}{sdks.stderr}")
        raise RuntimeError(
            f"{error}. Inspected installations: {'; '.join(probe_lines) or 'none'}"
        ) from error

    env = dict(os.environ)
    env["DEVELOPER_DIR"] = str(selected.developer_directory)
    full_version = subprocess.check_output(["xcodebuild", "-version"], env=env, text=True).strip()
    show_sdks = subprocess.check_output(["xcodebuild", "-showsdks"], env=env, text=True).strip()

    print(f"[apple-toolchain] Selected DEVELOPER_DIR: {selected.developer_directory}")
    print(f"[apple-toolchain] xcodebuild -version:\n{full_version}")
    print(f"[apple-toolchain] xcodebuild -showsdks:\n{show_sdks}")
    print(
        f"[apple-toolchain] Verified iPhoneOS SDK {selected.ios_sdk_version} "
        f"(satisfies >= {min_ios_sdk_major})."
    )

    _append_github_file("GITHUB_ENV", {"DEVELOPER_DIR": str(selected.developer_directory)})
    _append_github_file(
        "GITHUB_OUTPUT",
        {
            "developer_dir": str(selected.developer_directory),
            "xcode_version": selected.xcode_version_text,
            "ios_sdk_version": selected.ios_sdk_version,
        },
    )
    return selected
