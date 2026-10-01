// Container-only: read one .mat by path and print its parsed structure (version, graphs, textures,
// derived family/workflow/effects) exactly as AssetText::parseMat sees it. For diagnosing family
// classification against real data.
//
// usage: mat_dump <bundlesDir> <path.mat>

#include "store/AssetStore.h"
#include "model/AssetText.h"
#include <QCoreApplication>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) { fprintf(stderr, "usage: mat_dump <bundlesDir> <path.mat>\n"); return 2; }
    AssetStore store; QString err;
    if (!store.open(QString::fromLocal8Bit(argv[1]), &err)) { fprintf(stderr, "open failed: %s\n", qPrintable(err)); return 1; }
    const QString path = QString::fromLocal8Bit(argv[2]).toLower();
    const QByteArray data = store.readFile(path, &err);
    if (data.isEmpty()) { fprintf(stderr, "read failed: %s\n", qPrintable(err)); return 1; }

    const AssetText::Material m = AssetText::parseMat(data);
    printf("path    %s\n", qPrintable(path));
    printf("version %d  valid=%d\n", m.version, m.valid);
    static const char* kAlpha[] = {"Opaque","Mask","Blend","Additive"};
    printf("family  '%s'  workflow=%d  alphaMode=%s alphaTest=%d usesRough=%d SSS=%d transl=%d fur=%d\n",
           qPrintable(m.family), int(m.workflow), kAlpha[int(m.alphaMode)], m.alphaTest, m.usesRoughness, m.hasSSS, m.hasTranslucency, m.hasFur);
    printf("roles: albedo=%s\n       normalGlossAO=%s\n       metalMask=%s\n       specColorTex=%s\n       emissive=%s\n",
           qPrintable(m.albedo), qPrintable(m.normalGlossAO), qPrintable(m.metalMask), qPrintable(m.specColorTex), qPrintable(m.emissiveTex));
    printf("graphs (%d):\n", int(m.graphs.size()));
    for (const QString& g : m.graphs) printf("    %s\n", qPrintable(g));
    printf("textures (%d):\n", int(m.textures.size()));
    for (const auto& t : m.textures) printf("    [%s] %s%s\n", qPrintable(t.role), qPrintable(t.path), t.srgb ? "  (srgb)" : "");
    return 0;
}
