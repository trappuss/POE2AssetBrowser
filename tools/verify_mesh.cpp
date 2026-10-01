// Container-only: parse a decompressed .smd/.fmt with the real C++ MeshParser and print numbers to
// diff against the Python oracle (tools/formats/smd_parse.py, fmt_parse.py).
#include "model/MeshParser.h"
#include <QFile>
#include <cstdio>

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: verify_mesh file.smd|file.fmt\n"); return 2; }
    QFile f(QString::fromLocal8Bit(argv[1]));
    if (!f.open(QIODevice::ReadOnly)) { fprintf(stderr, "open failed\n"); return 1; }
    const QByteArray data = f.readAll();
    const QString path = QString::fromLocal8Bit(argv[1]);
    ModelGeometry g; QString err;
    const bool ok = path.endsWith(".fmt") ? MeshParser::parseFmt(data, path, g, &err)
                                          : MeshParser::parseSmd(data, path, g, &err);
    if (!ok) { printf("PARSE FAIL: %s\n", qPrintable(err)); return 1; }
    double sx = 0, sy = 0, sz = 0; double su = 0, sv = 0;
    for (const MeshVertex& v : g.vertices) { sx += v.px; sy += v.py; sz += v.pz; su += v.u; sv += v.v; }
    printf("ver=%d vf=0x%x verts=%d indices=%d tris=%d parts=%d skinned=%d\n",
           g.formatVersion, g.vertexFormat, g.vertices.size(), g.indices.size(), g.triangleCount(), g.parts.size(), g.skinned ? 1 : 0);
    printf("possum= %.3f %.3f %.3f uvsum= %.4f %.4f\n", sx, sy, sz, su, sv);
    printf("bbox min %.3f %.3f %.3f max %.3f %.3f %.3f\n", g.bboxMin[0], g.bboxMin[1], g.bboxMin[2], g.bboxMax[0], g.bboxMax[1], g.bboxMax[2]);
    for (int i = 0; i < g.parts.size() && i < 12; ++i)
        printf("  part %-28s idx=%u count=%u mat=%s\n", qPrintable(g.parts[i].name), g.parts[i].indexStart, g.parts[i].indexCount, qPrintable(g.parts[i].material));
    // index bounds
    uint32_t mx = 0; for (uint32_t k : g.indices) if (k > mx) mx = k;
    printf("max index %u of %d verts (%s)\n", mx, g.vertices.size(), mx < uint32_t(g.vertices.size()) ? "in range" : "OUT OF RANGE");
    return 0;
}
