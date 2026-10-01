// Timing harness: open the store, load a model, time findSmForSmd/skeleton. No GL.
#include "store/AssetStore.h"
#include "model/ModelGeometry.h"
#include "model/AstSkeleton.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <cstdio>
int main(int argc, char** argv){
    QCoreApplication app(argc, argv);
    if(argc<3){ fprintf(stderr,"usage: store_time <bundles> <smd>\n"); return 2; }
    QElapsedTimer t; t.start();
    AssetStore store; QString err;
    if(!store.open(QString::fromLocal8Bit(argv[1]), &err)){ fprintf(stderr,"open: %s\n", qPrintable(err)); return 1; }
    fprintf(stderr,"open: %lld ms  (%d files)\n", t.elapsed(), (int)store.index().files().size());
    const QString smd = QString::fromLocal8Bit(argv[2]).toLower();
    t.restart();
    ModelGeometry geo;
    bool ok = store.loadModel(smd, geo, &err);
    fprintf(stderr,"loadModel: %lld ms  ok=%d verts=%d parts=%d mats=%d\n", t.elapsed(), ok, geo.vertices.size(), geo.parts.size(), geo.materialPaths.size());
    t.restart();
    AstSkeleton::Skeleton sk = store.loadSkeletonFor(smd, true, geo.jointPaletteSize());
    fprintf(stderr,"loadSkeletonFor: %lld ms  bones=%d\n", t.elapsed(), sk.bones.size());
    t.restart();
    QStringList deps = store.collectAssetFiles(smd, true, true);
    fprintf(stderr,"collectAssetFiles: %lld ms  files=%d\n", t.elapsed(), deps.size());
    return 0;
}
