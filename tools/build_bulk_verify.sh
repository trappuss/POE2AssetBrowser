#!/usr/bin/env bash
# Container-only build of tools/bulk_verify.cpp (parallel-vs-serial BulkExtractor check).
# Requires: qt6-base-dev, g++.  Output: $OUT/bulk_verify
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QT=/usr/include/x86_64-linux-gnu/qt6
OUT="${1:-/root/work/bulkverifybuild}"
mkdir -p "$OUT"; cd "$OUT"
INC="-I$ROOT/src -I$ROOT/third_party/ooz -I$ROOT/third_party/bcdec -I$QT -I$QT/QtCore -I$QT/QtGui -fPIC"
CXX="g++ -O2 -std=c++17 -w $INC"
MOC=/usr/lib/qt6/libexec/moc

echo "== ooz =="
for f in kraken bitknit lzna; do $CXX -Dmain=ooz_cli_main -c "$ROOT/third_party/ooz/$f.cpp" -o "$f.o"; done

echo "== moc =="
MOC_OBJS=""
for h in store/AssetStore store/IndexLoader bulk/BulkExtractor; do
  b=$(basename "$h"); $MOC "$ROOT/src/$h.h" -o "moc_$b.cpp"; $CXX -c "moc_$b.cpp" -o "moc_$b.o"; MOC_OBJS="$MOC_OBJS moc_$b.o"; done

echo "== sources =="
SRCS="bundle/Bundle bundle/BundleIndex bundle/Oodle store/AssetStore store/IndexLoader \
model/MeshParser model/AssetText model/AstSkeleton model/GlbExporter tex/DdsImage tex/BcdecImpl \
bulk/BulkExtractor"
OBJS=""
for s in $SRCS; do b=$(basename "$s"); $CXX -c "$ROOT/src/$s.cpp" -o "$b.o"; OBJS="$OBJS $b.o"; done
$CXX -c "$ROOT/tools/bulk_verify.cpp" -o bulk_verify.o

echo "== link =="
g++ -O2 -o bulk_verify bulk_verify.o $OBJS $MOC_OBJS kraken.o bitknit.o lzna.o -pthread -lQt6Gui -lQt6Core
echo "OK: $OUT/bulk_verify"
