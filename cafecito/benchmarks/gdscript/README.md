# Cafecito GDScript Benchmark Suite

Reproducible micro- and macro-benchmarks for tracking GDScript VM and compiler performance across commits.

## Quick start

Always measure optimization changes against **`template_release`** (built with `disable_path_overrides=no`, which is the default in `task build:template-release`), since `editor` / `template_debug` builds include `DEBUG_ENABLED` checks, `OPCODE_LINE`, and profiler hooks.

```bash
# Build a release export template at HEAD
task build:template-release

# Run the full suite (2 separate processes x 7 timed samples after 2 warmups)
task bench:gdscript -- --target release --runs 3 --label baseline

# Run a focused subset while iterating
task bench:gdscript -- --target release --filter calls --runs 2

# Compare two recorded snapshots
task bench:gdscript:compare -- \
  cafecito/benchmarks/results/<baseline>.json \
  cafecito/benchmarks/results/<candidate>.json
```

## Suites

- `arithmetic`: typed vs. untyped integer, float, `Vector2`, `Vector3`, `for-in-range`, and `while` loops.
- `calls`: GDScript `self` calls, cross-instance typed/untyped calls, polymorphic dispatch, recursive Fibonacci, native `Node2D` `MethodBind` calls, builtin/utility functions, and lambdas.
- `access`: direct `self` members, inherited native properties (`OPCODE_GET_MEMBER`/`SET_MEMBER`), cross-object fields, getters/setters, typed/untyped `Array`, `Packed*Array`, and `Dictionary`.
- `objects`: `RefCounted` instantiation, `is`/`as` type checks and casts, string operations, and container literal construction.
- `macro`: 2D spatial-hash entity simulation, 32x32 A* grid pathfinding with binary heap, 256-node `Node2D` transform tree update, binary varint packet codec, 64x64 `Vector2i` tile grid visibility scan, and polymorphic `Control` UI state dispatch.

## Methodology notes

- Every benchmark returns a deterministic integer checksum verified across all warmups, samples, and process runs; `compare` fails if a candidate changes any checksum.
- `--runs N` spawns separate Godot processes to smooth out ASLR / instruction-alignment variance.
- `compare` flags a change as `FASTER` / `SLOWER` only when the median delta exceeds `--threshold` (default `2%`) **and** the interquartile ranges `[Q1, Q3]` do not overlap.
- Results are written to `cafecito/benchmarks/results/` (gitignored).
