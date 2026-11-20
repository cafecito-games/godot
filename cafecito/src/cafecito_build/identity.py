"""Reproducible Git build identity derivation for the Cafecito Godot fork."""

from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

SHORT_SHA_PATTERN = re.compile(r"[0-9a-f]{9}")


@dataclass(frozen=True)
class BuildIdentity:
    commit_sha: str
    dirty: bool
    godot_version: str
    godot_status: str

    @property
    def build_name(self) -> str:
        dirty_suffix = "_dirty" if self.dirty else ""
        return f"cafecito{dirty_suffix}"

    @property
    def template_version(self) -> str:
        return f"{self.godot_version}.{self.godot_status}"


def _run_git(repository_root: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(repository_root), *arguments],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def _parse_version_py(version_file: Path) -> tuple[str, str]:
    values: dict[str, str] = {}
    for line in version_file.read_text(encoding="utf-8").splitlines():
        if " = " not in line:
            continue
        key, raw_value = line.split(" = ", 1)
        values[key.strip()] = raw_value.strip().strip('"')
    major = values["major"]
    minor = values["minor"]
    patch = values["patch"]
    status = values["status"]
    return f"{major}.{minor}.{patch}", status


def resolve_build_identity(repository_root: Path) -> BuildIdentity:
    commit_sha = _run_git(repository_root, "rev-parse", "--short=9", "HEAD")
    if SHORT_SHA_PATTERN.fullmatch(commit_sha) is None:
        raise RuntimeError(f"Expected 9-character lowercase hex SHA, got {commit_sha!r}")

    status_output = _run_git(repository_root, "status", "--porcelain", "--untracked-files=no")
    godot_version, godot_status = _parse_version_py(repository_root / "version.py")
    return BuildIdentity(
        commit_sha=commit_sha,
        dirty=bool(status_output),
        godot_version=godot_version,
        godot_status=godot_status,
    )
