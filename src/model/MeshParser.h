#pragma once
#include "model/ModelGeometry.h"
#include <QByteArray>
#include <QString>

// Parsers for PoE2 mesh payloads. Both fill a ModelGeometry directly from decoded bundle bytes.
// Layout and every constant here is measured — see docs/FORMATS.md §3 (.smd) and §4 (.fmt); the
// Python reference parsers in tools/formats/ are the oracle these mirror.
//
// Neither classifies anything by file name. The vertex layout is decided by the authored
// vertexFormat flag word, the material by the authored name in the .sm/.fmt string table.
namespace MeshParser {

// Bytes each vertexFormat flag contributes, in the measured field order. Shared by both parsers
// and by the self-test. (0x40's 4 bytes are a per-vertex u32 whose meaning is unverified; it is
// read and discarded so the stride is right.)
int vertexStride(uint32_t vertexFormat);

// A .smd payload → ModelGeometry (versions 1, 2, 3). `error` set and false returned on a bad file.
// Names come from the mesh name table; materials are left empty (the .sm supplies them — see
// SkinnedMeshDesc). Skeleton is NOT here (an .ast carries it); `skinned` is set from the format.
bool parseSmd(const QByteArray& data, const QString& sourcePath, ModelGeometry& out, QString* error);

// A .fmt payload → ModelGeometry (version 9 only; 4–8 return false with a clear reason). Names and
// per-part material paths come from the file's own UTF-16 string table.
bool parseFmt(const QByteArray& data, const QString& sourcePath, ModelGeometry& out, QString* error);

// Startup self-test: builds a tiny synthetic v3 .smd in memory and round-trips it, checks the
// stride table against the documented flag set. Empty string on success.
QString selfTest();

}  // namespace MeshParser
