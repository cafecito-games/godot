# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository is

This is Cafecito Games' fork of the Godot Engine, currently based on upstream
`4.7.2-stable`. It is the engine used to build and ship Cafecito Games titles.
Changes here exist because a real project hit a real problem: a device crash, a
threading deadlock, a missing platform integration, or a build step that needed
automating.

`cafecito-games/godot` is a **standalone repository**, not a registered GitHub
fork of `godotengine/godot`. The upstream relationship exists only as a git
remote:

```bash
git remote -v   # origin = cafecito-games/godot, upstream = godotengine/godot
```

Because of that, GitHub shows no fork-comparison view. The fork's delta against
upstream is discovered entirely from commit messages — which is why the commit
convention below is load-bearing, not cosmetic.

## Commit convention (required)

**Every commit authored for this fork must be prefixed `cafecito(<topic>):`.**

```
cafecito(android-java-class-boolean-return): Delivers Java boolean and Boolean method returns as Variant::BOOL instead of Variant::INT
cafecito(build): add SCons Relink Kit emitter, cafecito-build Python library, and Taskfile
cafecito(docs): rewrite README for the Cafecito Games fork
```

- `<topic>` is a kebab-case slug naming the area or the specific fix
  (`ios-scene-url-forwarding`, `classdb-default-cache-thread-safety`, `ci`,
  `version`, `extensions`, `docs`).
- This applies to **all** fork-authored work — engine fixes, build tooling, CI,
  and documentation alike. There is no separate `build(...)` or `fix(...)`
  prefix; a non-`cafecito` prefix makes the commit invisible to the delta query
  and to anyone auditing what this fork changes.
- Commits **cherry-picked or merged from upstream keep their original upstream
  messages** — never re-prefix them. The prefix is precisely what distinguishes
  "ours" from "theirs".

The query this convention exists to serve:

```bash
git log --oneline --grep "^cafecito"           # the complete fork delta
git log --oneline upstream/4.7..HEAD           # same thing, when a base ref is handy
```

Keep each change self-contained and cherry-pickable. The fork is rebased onto
new upstream stable releases rather than maintained as a long divergent branch,
so a change that touches one subsystem and nothing else survives a rebase; a
sprawling one does not.

## Build and test commands

`task` (Taskfile.dev) + `uv` wrap SCons. Use them rather than raw SCons unless
you specifically need an upstream-default build — see the module profile warning
below.

```bash
task                         # list all tasks
task info                    # host, Godot version, resolved build identity
task build:editor            # editor for the current host
task build:template-release  # release export template
task build:template-debug    # debug export template
task build:all               # editor + both templates
task build:libgodot:release  # Apple LibGodot slices (macOS dylibs + iOS static libs)
task clean                   # rm -rf bin/
```

Pass extra arguments through after `--`:

```bash
task build:editor -- --jobs 8 --debug-symbols --all-modules
task package:relink-kit -- --manifest bin/godot.macos.editor.arm64.relink.json
```

The underlying CLI is `uv run --project cafecito cafecito-build <command>`;
`cafecito-build --help` lists the packaging subcommands the CI release workflow
drives (`package-macos-template`, `finalize-ios-template`,
`package-android-templates`, `package-apple-libgodot-sdk`, `checksums`,
`select-xcode`).

### C++ unit tests (doctest)

Tests are compiled into the binary, not a separate target:

```bash
task build:editor -- --tests          # adds tests=yes
bin/godot.macos.editor.arm64 --headless --test --force-colors
bin/godot.macos.editor.arm64 --headless --test --test-case="*Variant*"
bin/godot.macos.editor.arm64 --headless --test --test-suite="*String*"
```

`--test` hands the remaining arguments to doctest, so any doctest filter flag
works. CI runs the unfiltered `--test` on Linux, macOS, and Windows.

### Python tooling tests

```bash
uv run --project cafecito --with pytest pytest cafecito/tests -q
```

### Lint and format

CI runs the `.pre-commit-config.yaml` hooks via `prek`; both `prek` and
`pre-commit` read the same config.

```bash
prek run --all-files        # or: prek run --files <paths>
```

That covers clang-format, clang-tidy, ruff (check + format), mypy, codespell,
header guards, copyright headers, and the doc/XML validators. Python style is
ruff with `line-length = 120`, configured in the root `pyproject.toml`.

## Architecture

### Upstream Godot

`core/`, `scene/`, `servers/`, `drivers/`, `editor/`, `modules/`, `platform/`,
`main/`, and `thirdparty/` are upstream Godot and documented upstream — see the
[engine development docs](https://docs.godotengine.org/en/latest/engine_details/development/).
Nothing in this fork re-architects them. When touching engine code, match the
surrounding upstream style and keep the diff minimal enough to rebase.

### Fork-specific layers

**`cafecito/` — two Python surfaces that must not be confused:**

1. `cafecito/src/cafecito_build/` is a `uv`-managed package (`cafecito-build`
   console script) invoked *outside* SCons. It owns host detection
   (`environment.py`), build identity (`identity.py`), the SCons invocation
   (`scons.py`), Relink Kit packaging (`relink_kit.py`), release artifact
   packaging (`packaging.py`), and Apple toolchain/LibGodot logic
   (`apple_toolchain.py`, `apple_libgodot.py`).
2. `cafecito/relink_manifest.py` is imported *from inside the SCons build*, by
   `methods.py` and `platform_methods.py`. It is deliberately dependency-free
   and not part of the `cafecito_build` package, because SCons runs under its
   own interpreter. Do not add imports from `cafecito_build` to it.

**SCons options added by the fork** (declared in `SConstruct`):

- `cafecito_relink_kit=yes|no` — emit a `bin/<binary>.relink.json` manifest
  beside each built binary. The manifest records the final link line with SDK
  and toolchain paths rewritten to placeholders (`__APPLE_SDK_ROOT__`,
  `__VULKAN_SDK__`, `__ANDROID_NDK_ROOT__`), so a Relink Kit can be re-linked on
  another machine without rebuilding the engine.
- `cafecito_ext_library=<path/to/lib.a>` — replace the default extension archive
  with a prebuilt private one.

**Extension and security seams** — `core/extension/cafecito_extensions.h` and
`core/crypto/cafecito_security.h` declare a small C++ namespace contract
(`CafecitoExtensions`, `CafecitoSecurity`) that the engine calls into for
extension init/teardown, build id, pack/encrypted header magics, AES key
derivation, pack-directory encryption policy, and the export key fingerprint.
The default implementation lives in `core/cafecito_ext_default/` and
**reproduces upstream Godot behavior exactly** — same `GDPC`/`GDEC` magics,
identity key derivation, no forced encryption, no-op extension hooks. A private
static archive linked via `cafecito_ext_library=` substitutes for it.

Treat those two headers as an **ABI boundary**: changing a signature breaks
every private archive built against them, and such archives are not in this
repository to be recompiled. Add new hooks rather than altering existing ones.

**Trimmed module profile** — `DEFAULT_SCONS_FLAGS` in
`cafecito_build/scons.py` disables Mono, OpenXR/WebXR/mobile VR, CSG, GridMap,
raycast, the RD lightmapper, xatlas, and meshoptimizer, and enables
`scu_build`. This is a 2D-oriented profile and it means **a `task build:*`
binary is not feature-equivalent to an upstream build**. Pass `--all-modules` to
opt out. If a bug reproduces only under one of these profiles, say which
profile was used.

**Build identity** — `identity.py` derives `BUILD_NAME=cafecito` (or
`cafecito_dirty` when the tree has uncommitted tracked changes) from a
9-character short SHA, and the engine reports its version as
`cafecito.<sha>[.<ext_id>]`, where `<ext_id>` comes from
`CafecitoExtensions::get_extension_build_id()`. Issue reports should quote that
string; it identifies the exact commit and extension archive.

### CI

`.github/workflows/runner.yml` fans out to the per-platform workflows (Android,
iOS, Linux, macOS, Windows, Web) plus static checks on every push and PR.
`release.yml` is the fork's own `workflow_dispatch` release pipeline: it builds
selected platforms, packages templates and Relink Kits, and publishes them.
iOS builds are fail-closed on the Xcode 27 / iOS 27 SDK floor via
`cafecito-build select-xcode`.

## How work lands, reporting, and upstreaming

**Pull requests are disabled on `cafecito-games/godot`** — the repository accepts
issues but not PRs, for contributors and maintainers alike. Land work by
committing to a branch and pushing, then fast-forwarding `main`; do not try to
open a PR here, and do not suggest one as the finishing step.

If a bug is not specific to this fork, it belongs
[upstream](https://github.com/godotengine/godot/issues) — fixing it there means
it eventually arrives via rebase instead of being carried forever. Carry a local
fix only when waiting is not an option, and keep it shaped so it can be dropped
once upstream lands its own.

Note that upstream `CONTRIBUTING.md` requires AI agents to self-disclose on pull
requests and issues opened against `godotengine/godot`.
