#!/usr/bin/env bash
# Container-only build of tools/export_model.cpp (headless production-pipeline .glb export) against
# distro Qt6 + the app sources. Requires: qt6-base-dev, g++.  Output: $OUT/export_model
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QT=/usr/include/x86_64-linux-gnu/qt6
OUT="${1:-/root/work/exportbuild}"
mkdir -p "$OUT"; cd "$OUT"
INC="-I$ROOT/src -I$ROOT/third_party/ooz -I$ROOT/third_party/bcdec -I$QT -I$QT/QtCore -I$QT/QtGui -fPIC"
CXX="g++ -O2 -std=c++17 -w $INC"

MOC=/usr/lib/qt6/libexec/moc
echo "== ooz =="
for f in kraken bitknit lzna; do $CXX -Dmain=ooz_cli_main -c "$ROOT/third_party/ooz/$f.cpp" -o "$f.o"; done

echo "== moc =="
MOC_OBJS=""
for h in store/AssetStore store/IndexLoader; do b=$(basename "$h"); $MOC "$ROOT/src/$h.h" -o "moc_$b.cpp"; $CXX -c "moc_$b.cpp" -o "moc_$b.o"; MOC_OBJS="$MOC_OBJS moc_$b.o"; done

echo "== sources =="
SRCS="bundle/Bundle bundle/BundleIndex bundle/Oodle store/AssetStore store/IndexLoader \
model/MeshParser model/AssetText model/AstSkeleton model/GlbExporter tex/DdsImage tex/BcdecImpl"
OBJS=""
for s in $SRCS; do b=$(basename "$s"); $CXX -c "$ROOT/src/$s.cpp" -o "$b.o"; OBJS="$OBJS $b.o"; done
$CXX -c "$ROOT/tools/export_model.cpp" -o export_model.o

echo "== link =="
g++ -O2 -o export_model export_model.o $OBJS $MOC_OBJS kraken.o bitknit.o lzna.o -lQt6Gui -lQt6Core
echo "OK: $OUT/export_model"
