#!/usr/bin/env bash
# Container-only full compile + link of the app against the distro Qt6 (6.4), to catch every
# compile/link error before handing the code to the user's MSVC build. NOT the shipping build —
# that is build.bat on Windows. Requires: qt6-base-dev, libqt6opengl6-dev, g++.
#
# The source list is READ FROM CMakeLists.txt, and every listed header that declares Q_OBJECT is
# moc'd. It used to be a hand-maintained copy of the build graph and drifted (four sources and
# five Q_OBJECT headers missing by 2026-09 — the script could not link the tree it claimed to
# verify). If a file is in the build, it is in this check; nothing to keep in sync.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QT=/usr/include/x86_64-linux-gnu/qt6
MOC=/usr/lib/qt6/libexec/moc
OUT="${1:-/root/work/appbuild}"
JOBS="${JOBS:-$(nproc)}"
mkdir -p "$OUT"; cd "$OUT"
INC="-I$ROOT/src -I$ROOT/third_party/ooz -I$ROOT/third_party/bcdec -I$QT -I$QT/QtCore -I$QT/QtGui -I$QT/QtWidgets -I$QT/QtOpenGL -I$QT/QtOpenGLWidgets -fPIC"
CXX="g++ -O2 -std=c++17 -w $INC"

# Everything between "qt_add_executable(POE2AssetBrowser" and its closing ")" that looks like src/x.y
mapfile -t LISTED < <(awk '/qt_add_executable\(POE2AssetBrowser/{on=1;next} on&&/^\)/{on=0} on' "$ROOT/CMakeLists.txt" \
                       | grep -oE 'src/[A-Za-z0-9_/.-]+\.(cpp|h)')
SRCS=(); HDRS=()
for f in "${LISTED[@]}"; do
  [ -f "$ROOT/$f" ] || { echo "MISSING in tree but listed in CMake: $f" >&2; exit 1; }
  case "$f" in *.cpp) SRCS+=("$f");; *.h) HDRS+=("$f");; esac
done
echo "== ${#SRCS[@]} sources, ${#HDRS[@]} headers listed in CMakeLists.txt =="

echo "== ooz (vendored, main renamed) =="
for f in kraken bitknit lzna; do $CXX -Dmain=ooz_cli_main -c "$ROOT/third_party/ooz/$f.cpp" -o "$f.o" & done; wait

echo "== moc every listed header that declares Q_OBJECT =="
MOC_OBJS=""
for h in "${HDRS[@]}"; do
  grep -q 'Q_OBJECT' "$ROOT/$h" || continue
  b="moc_$(basename "${h%.h}")"
  $MOC "$ROOT/$h" -o "$b.cpp" && $CXX -c "$b.cpp" -o "$b.o" &
  MOC_OBJS="$MOC_OBJS $b.o"
done; wait

echo "== app sources =="
OBJS=""
i=0
for s in "${SRCS[@]}"; do
  b="$(echo "${s#src/}" | tr '/' '_')"; b="${b%.cpp}.o"
  $CXX -c "$ROOT/$s" -o "$b" &
  OBJS="$OBJS $b"
  i=$((i+1)); [ $((i % JOBS)) -eq 0 ] && wait
done; wait

echo "== link =="
g++ -O2 -o POE2AssetBrowser $OBJS $MOC_OBJS kraken.o bitknit.o lzna.o \
    -lQt6Widgets -lQt6OpenGLWidgets -lQt6OpenGL -lQt6Gui -lQt6Core -lGL
echo "OK: $OUT/POE2AssetBrowser"
