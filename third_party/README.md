# Third-party code

Two open-source decoders are vendored here and built into the app. Nothing else third-party ships,
and no game assets or decryption keys are in this repository.

## ooz/  — Oodle Kraken / Mermaid / Leviathan decompressor
- Upstream: https://github.com/powzix/ooz (public domain / unlicense)
- `kraken.cpp`, `bitknit.cpp`, `lzna.cpp`, `targetver.h` are the unmodified upstream release.
- `stdafx.h` is **ours**: a small cross-platform shim so the same sources compile with MSVC (the
  shipping build) and GCC/Clang (the CI/verification build). CMake compiles these with
  `-Dmain=ooz_cli_main` so upstream's command-line `main` doesn't clash with the app's.
- The tool uses only `Kraken_Decompress`, wrapped by `src/bundle/Oodle.cpp`.

## bcdec/ — block-compression texture decoder (BC1–BC7)
- Upstream: https://github.com/iOrange/bcdec (MIT / public domain dual license — see bcdec/LICENSE)
- `bcdec.h` is the unmodified single-header release; `src/tex/BcdecImpl.cpp` is the one translation
  unit that instantiates it.
