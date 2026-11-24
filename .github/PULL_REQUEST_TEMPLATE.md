<!--
This repository does not accept outside pull requests. External PRs are closed
unreviewed. General Godot fixes should be contributed to
https://github.com/godotengine/godot so they can reach this fork through an
upstream rebase. For a problem specific to this fork, open an issue instead:
https://github.com/cafecito-games/godot/issues/new/choose

The checklist below is for Cafecito Games team members.
-->

> [!IMPORTANT]
> This repository does not accept outside pull requests. Please send general
> engine fixes to [upstream Godot](https://github.com/godotengine/godot/pulls),
> or [open an issue here](https://github.com/cafecito-games/godot/issues/new/choose)
> for a problem specific to this fork.

## Problem

<!-- What real project or release problem requires this fork change? -->

- Closes #

## Solution

<!-- Explain the smallest change that solves the problem and why it belongs in this fork. -->

## Verification

<!-- List the exact builds, tests, platforms, and module profile used. -->

## Fork maintenance

- [ ] The PR targets `main` and contains one self-contained, rebaseable change.
- [ ] Every fork-authored commit starts with `cafecito(<topic>):`.
- [ ] Relevant automated tests or a clear manual verification procedure are included.
- [ ] The change preserves the `CafecitoExtensions` and `CafecitoSecurity` ABI, or adds a new hook instead of changing an existing signature.
- [ ] The PR explains whether this should be upstreamed and how to recognize when the local change can be dropped.
