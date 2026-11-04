# vice-x64-ide

The modified VICE emulator source used by **x64 IDE**, published for review
by the VICE developers. This repository contains the emulator and its native
integration changes; the x64 IDE application is a separate project.

## Reviewing the changes

- [`tree/src`](tree/src) contains the current modified emulator source.
- The `web64-baseline` tag identifies the frozen **Web64-modified** VICE source
  before the x64 native adaptations. It is not a pristine upstream VICE release.
- The subsequent commit contains the native changes, so they can be reviewed
  with `git diff web64-baseline..main -- tree/src`.
- [`patches`](patches) contains the four exact native patches, already applied
  to the current tree. Patch paths are relative to `tree/`.
- [`review/publication.json`](review/publication.json) lists each changed file
  with its baseline and published SHA-256.

The inherited Web64 modifications include the profiler, debugger hooks and
memory/media injection mechanisms. The x64 work retains that modified C/C++
emulator rather than replacing it. Native adaptations include:

1. **Native headless host** — enables the retained hooks with
   `WEB64_HOST_NATIVE`, using a bounded binary process protocol rather than
   browser transport. Control, debugger and injection operations run at the
   emulator's safe points. Replies carry copied data rather than native pointers.
2. **Windows pacing** — a high-resolution waitable timer and more frequent
   host synchronization for the resident emulator.
3. **Profiler byte generations** — tracks individual written bytes so unrelated
   data writes on a code page do not invalidate that code's identity.
4. **Debugger bank/actions** — bank-aware inspection and breakpoint ownership,
   hit/action controls and the corresponding native protocol handling.

The source also contains the native frame/audio delivery and optional audio
host loading interfaces. The IDE's UI, Rust compiler and host-side clients
are not part of this repository.

## Source identity

The frozen Web64 VICE capture is
`sha256:97f804e16e8f6b2ab8961a8327267621b848cbf7fcffab4bd422d81432f883ab`,
original Git revision `d7543be1cd5452d38eeccf020345490a2b34a2e7`.
It contains 3,021 baseline `src/` files; the native tree contains 3,034.

[`manifest.json`](manifest.json) is the original, unmodified full-capture
inventory. Its local capture path and entries outside `src/` are provenance,
not files supplied by this repository. [`prepared.json`](prepared.json)
records the current source hashes and four native patches. Its historical
`emulator/patches/` paths correspond to the copies in this repository's
`patches/` directory. The source was verified against the locked capture and
the exact patch series before publication on 4 November 2026.

This repository has its own Git history. It does not contain the x64 IDE
repository's history.

## Build scope

This is a **source-review snapshot**, not a complete standalone VICE build
distribution. Only the modified `src/` tree was imported into x64 IDE.
Top-level Autotools support, generated configure inputs, the dependency make
fragment and ROM/data files are staged separately by the owning project.

[`review/build-native.sh`](review/build-native.sh) preserves the exact native
build recipe for inspection. It expects that complete verified staging tree;
it cannot build directly from this source-only checkout. The current native
build uses MSYS2 UCRT64 GCC/C++, the headless UI, `VICE_WEB=1` and
`WEB64_HOST_NATIVE=1`, with normal and profiler-enabled variants. GTK, SDL and
Emscripten are not used by that build. x64 IDE's current supported platform
is Windows 11 x64.

## Licence and notices

VICE's existing **GPL-2.0-or-later** licence and individual source notices are
retained. See [`COPYING`](COPYING) and the captured upstream
[`README`](README), including its copyright and third-party notices.
Bundled components retain their respective notices in the source tree.

This publication supports review of the modifications and a separate request
for special distribution permission. It does not claim that any licence
exception has been granted or change the existing licence terms.
