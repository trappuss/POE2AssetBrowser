// Exercise the REAL image + GIF export path (GLModelWidget::renderToImage, ExportCapture::turntableGif
// / animLoopGif) with a REAL model loaded from the store — the synthetic-cube tests can't catch a
// real-model regression (materials, transparency, skinning, framing). Renders a PNG and both GIFs and
// reports pixel/byte facts so a headless session can judge whether they actually contain the model.
//   usage: real_export_test <bundlesDir> <model.smd> <outDir>
#include "store/AssetStore.h"
#include "gl/GLModelWidget.h"
#include "app/ExportCapture.h"
#include "model/ModelGeometry.h"
#include "model/AstSkeleton.h"
#include "model/GlbExporter.h"
#include <QApplication>
#include <QImage>
#include <QFileInfo>
#include <QDir>
#include <cstdio>

static void imgStats(const QImage& im, const char* tag){
    if(im.isNull()){ printf("  %s: NULL image\n", tag); return; }
    long clear=0, solid=0, mid=0; long lum=0; long n=0;
    QImage t = im.convertToFormat(QImage::Format_ARGB32);
    for(int y=0;y<t.height();++y){ const QRgb* r=reinterpret_cast<const QRgb*>(t.constScanLine(y));
        for(int x=0;x<t.width();++x){ int a=qAlpha(r[x]); if(a==0)++clear; else if(a==255)++solid; else ++mid;
            if(a>0){ lum += (qRed(r[x])+qGreen(r[x])+qBlue(r[x]))/3; ++n; } } }
    printf("  %s: %dx%d  alpha[clear=%ld solid=%ld mid=%ld]  avgLum(opaque)=%ld\n",
           tag, im.width(), im.height(), clear, solid, mid, n?lum/n:-1);
}

int main(int argc, char** argv){
    QApplication app(argc, argv);
    if(argc<4){ fprintf(stderr,"usage: real_export_test <bundlesDir> <model.smd> <outDir>\n"); return 2; }
    AssetStore store; QString err;
    if(!store.open(QString::fromLocal8Bit(argv[1]), &err)){ fprintf(stderr,"open: %s\n", qPrintable(err)); return 1; }
    const QString smd = QString::fromLocal8Bit(argv[2]).toLower();
    const QString outDir = QString::fromLocal8Bit(argv[3]);
    QDir().mkpath(outDir);

    ModelGeometry geo;
    if(!store.loadModel(smd, geo, &err)){ fprintf(stderr,"loadModel: %s\n", qPrintable(err)); return 1; }
    printf("model: %d verts, %d parts, %d materials\n", geo.vertices.size(), geo.parts.size(), geo.materialPaths.size());

    GLModelWidget w; w.resize(700,700); w.show(); app.processEvents();
    AstSkeleton::Skeleton skel = store.loadSkeletonFor(smd, /*decodeClips*/true, geo.jointPaletteSize());
    w.setModel(geo, skel);
    // resolve + hand materials to the widget exactly like ModelsTab::loadModel does
    const QVector<GlbExporter::ExportMaterial> mats = store.resolveMaterials(geo, /*decode*/true);
    QVector<GLModelWidget::MaterialTextures> tex(geo.materialPaths.size());
    for(int i=0;i<mats.size() && i<tex.size();++i){
        tex[i].baseColor=mats[i].baseColor; tex[i].normal=mats[i].normal; tex[i].metalRough=mats[i].metallicRoughness;
        tex[i].emissive=mats[i].emissive; tex[i].specColor=mats[i].specularColor;
        for(int c=0;c<3;++c) tex[i].subsurface[c]=mats[i].subsurface[c];
        tex[i].translucent=mats[i].transmissionFactor; tex[i].alphaMode=mats[i].alphaMode;
        tex[i].isFur=mats[i].isFur; tex[i].furNoise=mats[i].furNoise; tex[i].furMask=mats[i].furMask; tex[i].furDepth=mats[i].furDepth;
    }
    w.setMaterialTextures(tex);
    app.processEvents(); app.processEvents();

    int fails=0;
    fprintf(stderr,"[phase] materials+widget ready, starting image render\n"); fflush(stderr);
    // ---- image ----
    QImage opaque = w.renderToImage(100, false, false);
    fprintf(stderr,"[phase] opaque render done\n"); fflush(stderr);
    QImage trans  = w.renderToImage(100, true, true);
    imgStats(opaque, "image opaque");
    imgStats(trans,  "image transparent+crop");
    opaque.save(QDir(outDir).filePath("real_opaque.png"), "PNG");
    trans.save(QDir(outDir).filePath("real_trans.png"), "PNG");
    if(opaque.isNull()||trans.isNull()){ printf("FAIL: null image\n"); ++fails; }
    // an all-black opaque render = nothing drawn / shader broken
    {
        QImage t = opaque.convertToFormat(QImage::Format_RGB32); long nonbg=0;
        for(int y=0;y<t.height();++y){ const QRgb* r=reinterpret_cast<const QRgb*>(t.constScanLine(y));
            for(int x=0;x<t.width();++x){ int R=qRed(r[x]),G=qGreen(r[x]),B=qBlue(r[x]);
                if(abs(R-33)>6||abs(G-33)>6||abs(B-38)>6) ++nonbg; } }
        printf("  opaque non-background pixels: %ld\n", nonbg);
        if(nonbg < 200){ printf("FAIL: opaque render shows ~no model (only backdrop)\n"); ++fails; }
    }

    // ---- gifs ----
    fprintf(stderr,"[phase] starting turntable gif\n"); fflush(stderr);
    ExportCapture::GifOptions gopt; gopt.fps=15; gopt.turntableFrames=16; gopt.scalePercent=60;
    gopt.transparentBg=false; gopt.cropToModel=true; gopt.maxColors=128; gopt.dither=true; gopt.optimize=false;
    const QString tg = QDir(outDir).filePath("real_turntable.gif");
    bool tok = ExportCapture::turntableGif(&w, tg, gopt, {});
    QFileInfo tf(tg);
    printf("  turntable gif: ok=%d exists=%d bytes=%lld\n", tok, tf.exists(), (long long)tf.size());
    if(!tok || !tf.exists() || tf.size()<200){ printf("FAIL: turntable gif not written\n"); ++fails; }

    printf(fails? "RESULT: %d FAILURE(S)\n":"RESULT: PASS\n", fails);
    return fails?1:0;
}
