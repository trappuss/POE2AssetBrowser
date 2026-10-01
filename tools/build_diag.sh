#!/usr/bin/env bash
# Container-only build of tools/poe_diag.cpp against distro Qt6 + the app's parsing/export sources.
# NOT a shipping build — it exists so a headless session can validate rig/export correctness.
# Requires: qt6-base-dev, g++.  Output: $OUT/poe_diag
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QT=/usr/include/x86_64-linux-gnu/qt6
OUT="${1:-/root/work/diagbuild}"
mkdir -p "$OUT"; cd "$OUT"
INC="-I$ROOT/src -I$ROOT/third_party/ooz -I$ROOT/third_party/bcdec -I$QT -I$QT/QtCore -I$QT/QtGui -fPIC"
CXX="g++ -O2 -std=c++17 -w $INC"

echo "== ooz =="
for f in kraken bitknit lzna; do $CXX -Dmain=ooz_cli_main -c "$ROOT/third_party/ooz/$f.cpp" -o "$f.o"; done

echo "== sources =="
SRCS="model/MeshParser model/AssetText model/AstSkeleton model/GlbExporter bundle/Bundle bundle/Oodle"
OBJS=""
for s in $SRCS; do b=$(basename "$s"); $CXX -c "$ROOT/src/$s.cpp" -o "$b.o"; OBJS="$OBJS $b.o"; done
$CXX -c "$ROOT/tools/poe_diag.cpp" -o poe_diag.o

echo "== link =="
g++ -O2 -o poe_diag poe_diag.o $OBJS kraken.o bitknit.o lzna.o -lQt6Gui -lQt6Core
echo "OK: $OUT/poe_diag"
