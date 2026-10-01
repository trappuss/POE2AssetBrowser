#pragma once
#include <QByteArray>
#include <QImage>
#include <QString>
#include <cstdint>

// PoE2 texture decoding. A `.dds` is a standard DDS with a DX10 extension header on every file
// measured (docs/FORMATS.md §5); the sibling `.dds.header` carries the full dimensions and format
// so the Textures list never has to open a Streaming bundle. Decoding is CPU-side via bcdec
// (third_party/bcdec, MIT), which handles BC1/2/3/4/5/6H/7 and plain RGBA8.
namespace DdsImage {

struct Info {
    int      width = 0, height = 0, mipCount = 0;
    uint32_t dxgiFormat = 0;     // the DX10 header's DXGI_FORMAT, 0 for a legacy FourCC dds
    QString  fourcc;             // "DX10", "DXT1", … (the raw pixel-format tag)
    QString  codecName;          // "BC7", "BC1", "R8G8B8A8", …
    bool     isCube = false, isArray = false, isVolume = false;
    bool     srgb = false;
    bool     valid = false;
};

// Read the DDS header (no pixel decode). `error` set on failure.
Info probe(const QByteArray& dds, QString* error = nullptr);

// Decode the top mip of a full `.dds` to an RGBA8 QImage. Null on an unsupported format (with
// `error` set), so callers can fall back to reporting "format not decoded" rather than crashing.
QImage decode(const QByteArray& dds, QString* error = nullptr);

// The `.dds.header` split file: version 3, full width/height/mips + a compact format code, then a
// tiny embedded placeholder DDS. Returns the real texture's dimensions and format for the list.
struct HeaderInfo {
    int      width = 0, height = 0, mipCount = 0;
    uint32_t gggFormat = 0;      // GGG's own format enum (205 BC1, 213 BC7, 207 BC2, 21 RGBA8 — measured)
    QString  codecName;
    uint32_t fullDdsSize = 0;    // the sibling .dds size, for a sanity check
    bool     valid = false;
    bool     redirect = false;   // a '*'-prefixed path redirect (PoE1 shape; kept for safety)
    QString  redirectPath;
};
HeaderInfo probeHeader(const QByteArray& header, QString* error = nullptr);

// Map a GGG .dds.header format code to a human codec name (best-effort; unknown → "fmt <n>").
QString gggFormatName(uint32_t code);

// Self-test: decode a 4x4 synthetic BC1 block and check the corner pixels. Empty on success.
QString selfTest();

}  // namespace DdsImage
