#include "tex/DdsImage.h"

#include <QtEndian>
#include <cstring>
#include "bcdec.h"

namespace {
uint32_t rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return qFromLittleEndian(v); }

// DXGI formats we handle. Returns block bytes (8 or 16), 0 for uncompressed RGBA8, -1 unsupported.
struct Codec { int blockBytes; int kind; QString name; bool srgb; };  // kind: 1 BC1 2 BC2 3 BC3 4 BC4 5 BC5 6 BC6H 7 BC7 0 RGBA8
Codec dxgiCodec(uint32_t f)
{
    switch (f) {
        case 71: return {8,  1, QStringLiteral("BC1"),  false};
        case 72: return {8,  1, QStringLiteral("BC1"),  true};
        case 74: return {16, 2, QStringLiteral("BC2"),  false};
        case 75: return {16, 2, QStringLiteral("BC2"),  true};
        case 77: return {16, 3, QStringLiteral("BC3"),  false};
        case 78: return {16, 3, QStringLiteral("BC3"),  true};
        case 80: return {8,  4, QStringLiteral("BC4"),  false};
        case 81: return {8,  4, QStringLiteral("BC4"),  false};
        case 83: return {16, 5, QStringLiteral("BC5"),  false};
        case 84: return {16, 5, QStringLiteral("BC5"),  false};
        case 95: return {16, 6, QStringLiteral("BC6H"), false};
        case 96: return {16, 6, QStringLiteral("BC6H"), false};
        case 98: return {16, 7, QStringLiteral("BC7"),  false};
        case 99: return {16, 7, QStringLiteral("BC7"),  true};
        case 28: return {0,  0, QStringLiteral("R8G8B8A8"), false};
        case 29: return {0,  0, QStringLiteral("R8G8B8A8"), true};
        default: return {-1, -1, QStringLiteral("DXGI %1").arg(f), false};
    }
}
}  // namespace

DdsImage::Info DdsImage::probe(const QByteArray& dds, QString* error)
{
    Info info;
    const uint8_t* d = reinterpret_cast<const uint8_t*>(dds.constData());
    if (dds.size() < 128 || std::memcmp(d, "DDS ", 4) != 0) { if (error) *error = QStringLiteral("not a DDS (bad magic)"); return info; }
    info.height  = int(rd32(d + 12));
    info.width   = int(rd32(d + 16));
    info.mipCount = int(rd32(d + 28));
    const uint32_t pfFlags = rd32(d + 0x50);
    const uint32_t fourcc  = rd32(d + 0x54);
    const uint32_t caps2   = rd32(d + 0x70);
    info.isCube   = (caps2 & 0x200) != 0;
    info.isVolume = (caps2 & 0x200000) != 0;
    char cc[5] = {0}; std::memcpy(cc, d + 0x54, 4);
    info.fourcc = QString::fromLatin1(cc).trimmed();
    if ((pfFlags & 0x4) && fourcc == 0x30315844u /* 'DX10' */) {
        if (dds.size() < 148) { if (error) *error = QStringLiteral("DDS DX10 header truncated"); return info; }
        info.dxgiFormat = rd32(d + 0x80);
        const uint32_t arraySize = rd32(d + 0x88);
        info.isArray = arraySize > 1;
        const Codec c = dxgiCodec(info.dxgiFormat);
        info.codecName = c.name; info.srgb = c.srgb;
        info.valid = c.blockBytes >= 0;
        info.fourcc = QStringLiteral("DX10");
        if (!info.valid && error) *error = QStringLiteral("unsupported DXGI format %1").arg(info.dxgiFormat);
    } else {
        // Legacy FourCC (DXT1/3/5). Not seen in PoE2 but handled for robustness.
        info.codecName = info.fourcc;
        info.valid = (info.fourcc == QStringLiteral("DXT1") || info.fourcc == QStringLiteral("DXT3") || info.fourcc == QStringLiteral("DXT5"));
        if (!info.valid && error) *error = QStringLiteral("unsupported legacy DDS format '%1'").arg(info.fourcc);
    }
    return info;
}

QImage DdsImage::decode(const QByteArray& dds, QString* error)
{
    const Info info = probe(dds, error);
    if (!info.valid || info.width <= 0 || info.height <= 0) return QImage();
    const uint8_t* d = reinterpret_cast<const uint8_t*>(dds.constData());
    int dataOff = 128;
    Codec c;
    if (info.fourcc == QStringLiteral("DX10")) { dataOff = 148; c = dxgiCodec(info.dxgiFormat); }
    else if (info.fourcc == QStringLiteral("DXT1")) c = {8, 1, QStringLiteral("BC1"), false};
    else if (info.fourcc == QStringLiteral("DXT3")) c = {16, 2, QStringLiteral("BC2"), false};
    else c = {16, 3, QStringLiteral("BC3"), false};

    const int W = info.width, H = info.height;
    QImage img(W, H, QImage::Format_RGBA8888);
    img.fill(Qt::transparent);

    if (c.kind == 0) {   // uncompressed RGBA8
        const qint64 need = qint64(W) * H * 4;
        if (dds.size() - dataOff < need) { if (error) *error = QStringLiteral("uncompressed DDS shorter than %1x%2").arg(W).arg(H); return QImage(); }
        std::memcpy(img.bits(), d + dataOff, size_t(need));
        return img;
    }

    const int bb = c.blockBytes;
    const uint8_t* src = d + dataOff;
    const qint64 avail = dds.size() - dataOff;
    const qint64 needBlocks = qint64(((W + 3) / 4)) * ((H + 3) / 4) * bb;
    if (avail < needBlocks) { if (error) *error = QStringLiteral("DDS payload %1 < %2 needed for %3x%4 %5").arg(avail).arg(needBlocks).arg(W).arg(H).arg(c.name); return QImage(); }

    uint8_t blk[16 * 4];
    for (int by = 0; by < H; by += 4) {
        for (int bx = 0; bx < W; bx += 4) {
            switch (c.kind) {
                case 1: bcdec_bc1(src, blk, 4 * 4); break;
                case 2: bcdec_bc2(src, blk, 4 * 4); break;
                case 3: bcdec_bc3(src, blk, 4 * 4); break;
                case 7: bcdec_bc7(src, blk, 4 * 4); break;
                case 4: { uint8_t r[16]; bcdec_bc4(src, r, 4); for (int i = 0; i < 16; ++i) { blk[i*4]=r[i]; blk[i*4+1]=0; blk[i*4+2]=0; blk[i*4+3]=255; } } break;
                case 5: { uint8_t rg[32]; bcdec_bc5(src, rg, 8); for (int i = 0; i < 16; ++i) { blk[i*4]=rg[i*2]; blk[i*4+1]=rg[i*2+1]; blk[i*4+2]=0; blk[i*4+3]=255; } } break;
                case 6: { float fp[16*3]; bcdec_bc6h_float(src, fp, 4*3, 0); for (int i = 0; i < 16; ++i) { auto cv=[&](float x){ x=x<0?0:(x>1?1:x); return uint8_t(x*255.0f+0.5f); }; blk[i*4]=cv(fp[i*3]); blk[i*4+1]=cv(fp[i*3+1]); blk[i*4+2]=cv(fp[i*3+2]); blk[i*4+3]=255; } } break;
                default: return QImage();
            }
            for (int j = 0; j < 4; ++j) {
                const int y = by + j; if (y >= H) break;
                uint8_t* row = img.scanLine(y);
                for (int i = 0; i < 4; ++i) {
                    const int x = bx + i; if (x >= W) break;
                    std::memcpy(row + x * 4, blk + (j * 4 + i) * 4, 4);
                }
            }
            src += bb;
        }
    }
    return img;
}

DdsImage::HeaderInfo DdsImage::probeHeader(const QByteArray& header, QString* error)
{
    HeaderInfo hi;
    const uint8_t* d = reinterpret_cast<const uint8_t*>(header.constData());
    if (header.size() < 28) { if (error) *error = QStringLiteral(".dds.header too small"); return hi; }
    const uint32_t version = rd32(d + 0);
    if (version != 3) {
        // A '*'-prefixed path redirect (PoE1). Keep the branch even though PoE2 headers are all v3.
        const uint8_t* body = d + (version == 3 ? 28 : 16);
        if (header.size() > 16 && body[0] == '*') {
            hi.redirect = true;
            hi.redirectPath = QString::fromUtf8(reinterpret_cast<const char*>(body + 1)).section(QLatin1Char('*'), 0, 0);
            return hi;
        }
        if (error) *error = QStringLiteral(".dds.header version %1 not understood").arg(version);
        return hi;
    }
    hi.width    = int(rd32(d + 4));
    hi.height   = int(rd32(d + 8));
    hi.mipCount = int(rd32(d + 12));
    hi.gggFormat = rd32(d + 16);
    hi.fullDdsSize = rd32(d + 24);
    hi.codecName = gggFormatName(hi.gggFormat);
    hi.valid = true;
    return hi;
}

QString DdsImage::gggFormatName(uint32_t code)
{
    // Measured against the sibling .dds DXGI values (docs/FORMATS.md §5.2). Others are best-effort.
    switch (code) {
        case 205: return QStringLiteral("BC1");
        case 213: return QStringLiteral("BC7");
        case 207: return QStringLiteral("BC2");
        case 209: return QStringLiteral("BC3");
        case 21:  return QStringLiteral("R8G8B8A8");
        default:  return QStringLiteral("fmt %1").arg(code);
    }
}

QString DdsImage::selfTest()
{
    // A BC1 block: colour0 = red (0xF800), colour1 = blue (0x001F), all texels index 0 → red.
    uint8_t block[8] = {0x00, 0xF8, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00};
    // Wrap it as a 4x4 DX10 BC1 DDS.
    QByteArray dds(148 + 8, 0);
    uint8_t* d = reinterpret_cast<uint8_t*>(dds.data());
    std::memcpy(d, "DDS ", 4);
    auto wr = [&](int off, uint32_t v){ v = qToLittleEndian(v); std::memcpy(d + off, &v, 4); };
    wr(4, 124); wr(12, 4); wr(16, 4); wr(28, 1);
    wr(0x50, 0x4); std::memcpy(d + 0x54, "DX10", 4);
    wr(0x80, 71);   // BC1_UNORM
    std::memcpy(d + 148, block, 8);
    QString err;
    const QImage img = decode(dds, &err);
    if (img.isNull()) return QStringLiteral("DdsImage self-test: decode failed (%1)").arg(err);
    const QRgb c = img.pixel(0, 0);
    if (qRed(c) < 200 || qGreen(c) > 60 || qBlue(c) > 60)
        return QStringLiteral("DdsImage self-test: BC1 corner was #%1, expected red").arg(uint(c), 8, 16, QLatin1Char('0'));
    return QString();
}
