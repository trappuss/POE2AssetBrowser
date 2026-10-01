// Container-only research: does a given .ast rig skin a given .smd mesh at OWN-RIG quality?
// Loads the mesh (AssetStore::loadModel) and the rig (.ast) directly — independent of the app's
// skeleton-selection fallback — and reports, for every weighted vertex, the distance from the
// vertex to its weight-blended bone position, normalised by the model's bbox diagonal.
// Calibration (measured this project): a model's OWN correct rig ≈ 4–5%, a foreign/wrong rig ≈ 62%,
// a loose-drape robe on its correct rig ≈ 14%. Also reports palette size and out-of-range joints.
//
// usage: rig_cover <bundlesDir> <model.smd> <rig.ast>

#include "store/AssetStore.h"
#include "model/AstSkeleton.h"
#include "model/ModelGeometry.h"
#include <QCoreApplication>
#include <cmath>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 4) { fprintf(stderr, "usage: rig_cover <bundlesDir> <model.smd> <rig.ast>\n"); return 2; }
    AssetStore store; QString err;
    if (!store.open(QString::fromLocal8Bit(argv[1]), &err)) { fprintf(stderr, "open: %s\n", qPrintable(err)); return 1; }
    const QString modelPath = QString::fromLocal8Bit(argv[2]).toLower();
    const QString rigPath   = QString::fromLocal8Bit(argv[3]).toLower();

    ModelGeometry geo;
    if (!store.loadModel(modelPath, geo, &err)) { fprintf(stderr, "loadModel: %s\n", qPrintable(err)); return 1; }
    if (!geo.skinned) { printf("%s : NOT skinned (static mesh)\n", qPrintable(modelPath)); return 0; }

    QString e2; const QByteArray astData = store.readFile(rigPath, &e2);
    if (astData.isEmpty()) { fprintf(stderr, "read rig: %s\n", qPrintable(e2)); return 1; }
    const AstSkeleton::Skeleton sk = AstSkeleton::parse(astData, /*decodeClips*/false, &err);
    if (!sk.valid || sk.bones.isEmpty()) { fprintf(stderr, "parse rig failed / empty (%s)\n", qPrintable(err)); return 1; }

    geo.computeBounds();
    const double diag = std::sqrt(
        std::pow(double(geo.bboxMax[0]-geo.bboxMin[0]),2) +
        std::pow(double(geo.bboxMax[1]-geo.bboxMin[1]),2) +
        std::pow(double(geo.bboxMax[2]-geo.bboxMin[2]),2));
    const int palette = geo.jointPaletteSize();
    const int nb = sk.bones.size();

    long oob = 0, nv = 0;
    double sum = 0.0, worst = 0.0;
    for (const MeshVertex& v : geo.vertices) {
        double bx=0, by=0, bz=0, wtot=0; bool bad=false;
        for (int k=0;k<4;++k) {
            if (v.weights[k] <= 0.0f) continue;
            const int j = int(v.joints[k]);
            if (j >= nb) { bad=true; break; }
            const RigMath::Mat4& m = sk.bones[j].bind;   // model-space, translation row 3
            bx += double(v.weights[k]) * m[12];
            by += double(v.weights[k]) * m[13];
            bz += double(v.weights[k]) * m[14];
            wtot += v.weights[k];
        }
        if (bad) { ++oob; continue; }
        if (wtot <= 0.0) continue;
        bx/=wtot; by/=wtot; bz/=wtot;
        const double d = std::sqrt(std::pow(v.px-bx,2)+std::pow(v.py-by,2)+std::pow(v.pz-bz,2));
        sum += d; if (d>worst) worst=d; ++nv;
    }
    const double meanPct = (nv && diag>0) ? 100.0*(sum/nv)/diag : 0.0;
    const double worstPct = (diag>0) ? 100.0*worst/diag : 0.0;
    printf("%-52s palette=%-4d rig=%-4d(%s) oob=%-6ld mean=%.1f%%  worst=%.0f%%\n",
           qPrintable(modelPath.section('/', -1)), palette, nb,
           palette<=nb ? "covers" : "SHORT", oob, meanPct, worstPct);
    return 0;
}
