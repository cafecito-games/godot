<p align="center">
  <img src="misc/logo/logo_outlined.svg" width="320" alt="Godot Engine logo">
</p>

<h1 align="center">Godot Engine — Cafecito Games Fork</h1>

<p align="center">
  <strong>The build of <a href="https://godotengine.org">Godot</a> we ship our games with.</strong><br>
  Based on <code>4.7.2-stable</code> · tracks upstream <code>godotengine/godot</code>
</p>

---

## What this is

This is [Cafecito Games](https://github.com/cafecito-games)' fork of the Godot
Engine. It is the engine we build and ship our own games with, so everything in
it exists because we hit a real problem in a real project: a crash on a device,
a deadlock during loading, a platform integration that upstream does not cover
yet, or a build step we needed to automate.

The fork is a thin layer on top of a stable upstream release. It is not a
re-architecture, a rename, or a competing engine — it is Godot, plus a set of
bug fixes, small enhancements, and a few extension points. We rebase onto new
upstream releases and we upstream fixes where it makes sense.

**It is built primarily to serve our own use cases.** We make no promises of
general-purpose support, and we will prioritize what our games need. That said,
every change here is MIT-licensed just like upstream Godot, and if any of it is
useful to you, you are very welcome to it.

## Should you use it?

| You are… | Recommendation |
| --- | --- |
| Starting a new Godot project | Use [official Godot](https://godotengine.org/download). |
| Hitting one of the bugs fixed below | Grab this fork, or cherry-pick the commit — they are small and self-contained. |
| Embedding Godot in an Apple app (LibGodot) | This fork may save you a lot of time. |
| Shipping to iOS/Android and seeing platform crashes | Worth a look; most of our fixes are mobile-related. |

## Changes in this fork

Every fork commit is prefixed `cafecito(<topic>)`, so
`git log --oneline --grep "^cafecito"` always gives you the exact delta against
upstream.

### Apple platforms

- **LibGodot embedding ABI for Apple** — ports the SwiftGodotKit embedding
  surface: native render surfaces, iOS app lifecycle integration, embedded
  display servers, and headless display-server selection on macOS. This is what
  lets us host Godot inside a native Swift app.
- **iOS `UIScene` URL forwarding** — restores delivery of incoming URLs to
  plugin services under the `UIScene` lifecycle.
- **CoreHaptics lifecycle safety** — correct haptic player retention, transient
  pulse selection, and stop-on-focus-loss/teardown, instead of leaking or
  crashing players.
- **Bundle-relative resource packs** — Apple embedded platforms resolve packs
  against `mainBundle.resourcePath`, and relative `.pck`/`.zip` paths are
  canonicalized at registration time.

### Android

- **`JavaClassWrapper` field lookup** — uses `getDeclaredFields` instead of
  `getFields`, which crashed on API 26 devices.
- **Nullable Java arguments** — `null` can be passed to Java methods expecting
  strings, boxed primitives, or arrays.
- **Correct boolean returns** — Java `boolean`/`Boolean` returns arrive as
  `Variant::BOOL` instead of `Variant::INT`.

### Rendering

- **2D shadow atlas bounds** — atlas indices are clamped to the canvas
  renderer's real capacity; lights beyond capacity render unshadowed rather
  than corrupting the atlas.
- **GLES3 canvas shadow sampling** — point-samples the 2D shadow atlas and
  clamps its band axis instead of repeating it, removing shadow bleed on mobile
  GL devices.
- **Texture encoders everywhere** — Basis Universal, ASTC, ETC/S3TC, and BPTC
  encoders are built and registered in *every* build target, not just the
  editor, so runtime imports and tooling behave identically.

### Stability and threading

- **ResourceLoader ↔ GDScript deadlock** — releases `WorkerThreadPool`
  unlock-allowance-zone mutexes across `ResourceLoader` waits and guards
  in-flight `GDScriptCache` reloads. This was a hard hang during threaded
  loading.
- **`ClassDB` default-property cache** — initialization and access are
  serialized, fixing a data race.
- **Rotating file logger** — initialization and backup rotation no longer race
  or crash on startup.
- **Editor shutdown** — deferred GDExtension documentation generation no longer
  crashes the editor on exit.

### Core and editor

- **`DirAccessUnix::change_dir`** — uses `stat`/`access`/`realpath` probing
  instead of process-wide `chdir`, which was mutating global process state from
  worker threads.
- **Autoload UIDs** — fallback paths are stored alongside autoload UIDs and
  resolved after UID setup, so renamed/moved autoloads keep resolving.
- **Mobile text context menus** — touch long-press brings up copy/paste context
  menus in text controls.
- **Namespaced script classes** — namespaced global script classes are ranked
  and labeled by their simple name in the Create Node dialog.

### Build, packaging, and extension seams

- **`cafecito-build` + Taskfile** — a small Python package and `task` front end
  wrapping SCons, so editor, templates, and Apple LibGodot SDK slices build
  with one command and a reproducible build identity.
- **Relink Kits** — each build emits a portable `.relink.json` manifest (with
  SDK and toolchain paths normalized to placeholders) that can be packaged into
  a kit and re-linked elsewhere without rebuilding the whole engine.
- **Multi-platform CI on free runners** — Android, iOS, Linux, macOS, Windows,
  and Web builds plus a release workflow that publishes templates and Relink
  Kits.
- **Extension and security seams** — a small set of hook points
  (`CafecitoExtensions`, `CafecitoSecurity`) that let a private static library
  be linked into the engine to customize pack/encryption behavior. The default
  implementation shipped here is a no-op that behaves exactly like upstream
  Godot. Builds report their identity as `cafecito.<sha>[.<ext_id>]`.

## Building

Prerequisites are the same as upstream Godot — see the
[official compiling docs](https://docs.godotengine.org/en/latest/engine_details/development/compiling).
On top of those you need [`uv`](https://docs.astral.sh/uv/) and
[`task`](https://taskfile.dev).

```bash
task                        # list all available tasks
task info                   # host, Godot version, and build identity
task build:editor           # editor for the current host
task build:template-release # release export template
task build:all              # editor + release + debug templates
task clean                  # remove bin/ artifacts
```

Each of those emits a `bin/<binary>.relink.json` manifest alongside the binary.
To turn one into a portable kit:

```bash
task package:relink-kit -- --manifest bin/<name>.relink.json
```

Apple LibGodot SDK slices (macOS dylibs + iOS static libraries):

```bash
task build:libgodot:release
```

Plain SCons still works exactly as it does upstream if you'd rather skip the
wrapper.

## Staying current with upstream

```bash
git remote add upstream https://github.com/godotengine/godot.git
git fetch upstream
git log --oneline --grep "^cafecito"   # the fork's delta
```

We rebase the fork onto new upstream stable releases rather than carrying long
divergent branches, which keeps each change reviewable in isolation and easy to
drop once upstream fixes it.

## Bug reports

**If your bug is not specific to this fork, please report it
[upstream](https://github.com/godotengine/godot/issues)** — that helps everyone,
including us, and a fix landed upstream reaches this fork on the next rebase.

For problems with the changes listed above, the build tooling, or the Apple
embedding layer, open an issue here.

**We don't accept pull requests.** Pull requests are disabled on this
repository. This fork is driven by what our own games need, and keeping it a
small, rebaseable delta on top of upstream matters more to us than growing it.
If you have a fix, the best home for it is
[upstream Godot](https://github.com/godotengine/godot/pulls) — we'll pick it up
from there. If it's specific to something in this fork, open an issue with the
details and we'll take a look.

Since everything here is MIT-licensed, you are of course free to fork this fork
and take it wherever you need.

## About Godot

Godot is a feature-packed, cross-platform 2D and 3D game engine, completely free
and open source under the [MIT license](https://godotengine.org/license), with
development funded and governed independently through the
[Godot Foundation](https://godot.foundation/). Nothing in this fork changes that
— the users' games are theirs, down to the last line of engine code.

- [Official website](https://godotengine.org) and [downloads](https://godotengine.org/download)
- [Documentation](https://docs.godotengine.org) and [class reference](https://docs.godotengine.org/en/latest/classes/)
- [Community channels](https://godotengine.org/community)
- [Demo projects](https://github.com/godotengine/godot-demo-projects) and [awesome-godot](https://github.com/godotengine/awesome-godot)

Enormous thanks to [Juan Linietsky](https://github.com/reduz),
[Ariel Manzur](https://github.com/punto-), and the many hundreds of
[contributors](AUTHORS.md) whose work this fork is built on.

## License

MIT, same as upstream Godot. See [LICENSE.txt](LICENSE.txt) and
[COPYRIGHT.txt](COPYRIGHT.txt).
