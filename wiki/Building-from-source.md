# Building from source

## Prerequisites

- **Visual Studio 2022** with the "Desktop development with C++" workload (gives you MSVC, CMake and
  Ninja).
- **vcpkg** — clone it, bootstrap it, and set `VCPKG_ROOT`:
  ```
  git clone https://github.com/microsoft/vcpkg
  .\vcpkg\bootstrap-vcpkg.bat
  setx VCPKG_ROOT C:\path\to\vcpkg
  ```
  (then open a new terminal so the variable is picked up).
- **Python 3** (optional) — used by `verify-src.py`, the pre-build source check. The build runs
  without it, just without that check.

## Build

Double-click **`build.bat`**. The first build compiles Qt6 through vcpkg and is slow (30–90 minutes,
several GB) — that is a one-time cost. After that, use **`rebuild.bat`** for the fast edit → build →
run loop (seconds), and **`clean-rebuild.bat`** if you ever suspect a stale object.

`run.bat` deploys the Qt runtime DLLs and plugins next to the exe and launches it.

## The checks

`rebuild.bat` runs `verify-src.py` before building — it catches, cheaply, the mistakes that otherwise
cost a full MSVC cycle: zero-byte files, unbalanced `{}()[]`, a header-only helper used without a
real `#include`, a QSettings key written and never read, and classification decided by a name
substring (the one convention this family exists to enforce).

## The vendored decoders

`third_party/ooz` (the Oodle Kraken/Mermaid/Leviathan decompressor) and `third_party/bcdec` (the
block-compression texture decoder) are built into the app by CMake. The three `ooz` `.cpp` files are
the unmodified upstream release; only `third_party/ooz/stdafx.h` is ours, a small cross-platform
shim, and CMake renames `ooz`'s command-line `main` so it does not clash with the app's.

## Verifying without MSVC (Linux/CI)

`tools/verify_container.sh` compiles and links the whole app against a distro Qt6 (6.4) to catch every
compile/link error before an MSVC build. It is not the shipping build — that is `build.bat` — but it
is a fast full-tree check. `tools/formats/` holds the Python reference parsers the C++ was validated
against.

## Publishing to GitHub and cutting a release

Three double-clickable steps, in order:

1. **`build.bat`** — produces `build\release\POE2AssetBrowser.exe`.
2. **`package-release.bat`** — assembles a standalone Windows package (the exe plus the Qt runtime DLLs
   and plugins, exactly what `run.bat` deploys) and zips it into `dist\POE2AssetBrowser-v<version>-win64.zip`.
3. **`publish.bat`** — creates or updates the GitHub repository, pushes the code to `main`, publishes a
   release tagged from the `VERSION` file with the `dist\` zip attached, and syncs the `wiki\` folder to
   the GitHub Wiki. It installs Git and the GitHub CLI if they are missing and signs you in; it is safe
   to run repeatedly, so the same file handles both the first publish and later versions.

Settings live in `.publish.cfg` (repository name, public/private, release-notes source, and so on) —
plain `KEY=value`, edit and re-run. The version is read from the `VERSION` file; bump it there (and in
`CMakeLists.txt` to keep them in step) for the next release. Release notes come from `CHANGELOG.md`. For
a code-only push with no release, set `SKIP_RELEASE=1`.

`dist\` and `build\` are git-ignored, so build output never lands in the repository — the release zip is
attached to the GitHub release instead.
