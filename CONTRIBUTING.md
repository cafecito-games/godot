# Reporting issues and contributing

This repository is Cafecito Games' production fork of Godot. Public bug reports
are welcome, but **we do not accept outside pull requests**. The fork exists to
carry the small set of engine and build changes required by our games, and we
keep that delta narrow so it can be rebased onto new upstream releases.

## Where to report a problem

- If the bug also occurs in an official Godot build, report it to the
  [upstream Godot issue tracker](https://github.com/godotengine/godot/issues/new/choose).
- If you want to propose a general engine feature, use
  [Godot Proposals](https://github.com/godotengine/godot-proposals/issues/new/choose).
- If the problem is in the official documentation, report it to
  [godot-docs](https://github.com/godotengine/godot-docs/issues/new/choose).
- If you need help using Godot, use the
  [Godot community channels](https://godotengine.org/community).
- If the bug is caused by this fork's engine changes, build tooling, release
  packaging, extension/security seams, or Apple embedding support,
  [open an issue here](https://github.com/cafecito-games/godot/issues/new/choose).

If you are unsure whether a bug is fork-specific, compare the Cafecito build
with the corresponding official Godot release and include the result. It is fine
to open an issue here when a comparison is not practical, as long as you explain
what you could test.

## Filing a useful fork issue

Please include:

- the full Cafecito build identity shown by the editor or project manager, such
  as `cafecito.1a2b3c4d5` or `cafecito.1a2b3c4d5.extension-id`;
- the official Godot versions used for comparison;
- operating system, architecture, and relevant hardware or driver details;
- exact reproduction steps and any relevant build flags;
- a minimal reproduction project, manifest, or log when applicable; and
- errors and logs as searchable text, with secrets and proprietary data removed.

Open one issue per bug and search existing open and closed issues first.

## Pull requests

Outside pull requests are closed unreviewed. If you have a general engine fix,
please contribute it to [upstream Godot](https://github.com/godotengine/godot/pulls),
where it can benefit the whole community and reach this fork in a future rebase.
If the fix only applies to this fork, open an issue with the details so the
Cafecito Games team can evaluate it. Since this repository is MIT-licensed, you
are also free to maintain the change in your own fork.

Upstream Godot has its own contribution requirements. Read its
[contributor documentation](https://contributing.godotengine.org/) before
opening an upstream issue or pull request, including its current AI disclosure
policy.

## Cafecito Games team workflow

Team changes land through a pull request targeting `main` and are squash-merged.
Every fork-authored commit must use this format:

```text
cafecito(<topic>): <description>
```

Use a kebab-case topic, keep the change self-contained and cherry-pickable, and
include tests or a clear manual verification procedure. Do not re-prefix commits
cherry-picked or merged from upstream. Changes to the extension/security seam
must preserve the existing ABI; add a hook instead of changing an existing
signature.

Common verification commands are documented in the [README](README.md). The
most frequently used checks are:

```bash
task build:editor -- --tests
bin/godot.macos.editor.arm64 --headless --test --force-colors
uv run --project cafecito --with pytest pytest cafecito/tests -q
prek run --all-files
```

State whether testing used the default trimmed Cafecito module profile or
`--all-modules`, and note any platforms that were not tested.
