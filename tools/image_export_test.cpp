// Headless verification of GLModelWidget::renderToImage (template §15 image export). Builds a tiny
// cube geometry, shows a GLModelWidget offscreen (xvfb + llvmpipe), and renders at 100%, 200% and with
// a transparent background — asserting the returned images are non-empty, that 200% is genuinely larger
// (a re-render, not an upscale), and that the transparent render carries real alpha (some fully
// transparent pixels AND some opaque ones). Saves the PNGs so they can be eyeballed too.
#include "gl/GLModelWidget.h"
#include "model/ModelGeometry.h"
#include <QApplication>
#include <QImage>
#include <cstdio>

static ModelGeometry makeCube()
{
    ModelGeometry g;
    // 8 corners of a unit cube centred at origin.
    const float c = 0.5f;
    const float pts[8][3] = {
        {-c,-c,-c},{c,-c,-c},{c,c,-c},{-c,c,-c},{-c,-c,c},{c,-c,c},{c,c,c},{-c,c,c} };
    for (auto& p : pts) {
        MeshVertex v{}; v.px=p[0]; v.py=p[1]; v.pz=p[2];
        v.nx=p[0]; v.ny=p[1]; v.nz=p[2];   // crude outward normals — fine for a render smoke test
        v.u=0; v.v=0; v.tx=1; v.ty=0; v.tz=0; v.tw=1;
        g.vertices.append(v);
    }
    const int idx[36] = {
        0,1,2, 0,2,3,  4,6,5, 4,7,6,  0,4,5, 0,5,1,
        1,5,6, 1,6,2,  2,6,7, 2,7,3,  3,7,4, 3,4,0 };
    for (int i : idx) g.indices.append(i);
    MeshPart part; part.indexStart=0; part.indexCount=36; part.materialIndex=-1; part.name="cube";
    g.parts.append(part);
    g.materialPaths.append(QString());
    for (int k=0;k<3;++k){ g.bboxMin[k]=-c; g.bboxMax[k]=c; }
    return g;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    GLModelWidget w;
    w.resize(320, 240);
    w.show();
    app.processEvents();
    w.setModel(makeCube());
    app.processEvents();

    QImage a = w.renderToImage(100, /*transparent*/false, /*crop*/false);
    QImage b = w.renderToImage(200, /*transparent*/false, /*crop*/false);
    QImage t = w.renderToImage(100, /*transparent*/true,  /*crop*/true);

    int fails = 0;
    auto req = [&](bool ok, const char* msg){ if(!ok){ printf("FAIL: %s\n", msg); ++fails; } };

    req(!a.isNull() && a.width() > 0 && a.height() > 0, "100% image is empty");
    req(!b.isNull(), "200% image is empty");
    req(b.width() >= a.width()*2 - 2 && b.height() >= a.height()*2 - 2, "200% not a larger re-render");
    req(!t.isNull(), "transparent image is empty");
    req(t.hasAlphaChannel(), "transparent image has no alpha channel");

    // Alpha content check on the transparent render: some pixels fully transparent (background), some
    // fully opaque (the cube). If everything is opaque the transparent path did nothing.
    if (!t.isNull() && t.hasAlphaChannel()) {
        long clear=0, solid=0;
        for (int y=0;y<t.height();++y){ const QRgb* r=reinterpret_cast<const QRgb*>(t.constScanLine(y));
            for (int x=0;x<t.width();++x){ int al=qAlpha(r[x]); if(al==0)++clear; else if(al==255)++solid; } }
        req(clear > 0, "transparent render has no transparent pixels");
        req(solid > 0, "transparent render has no opaque pixels (nothing drawn)");
        printf("transparent render: %ld clear, %ld solid pixels (%dx%d)\n", clear, solid, t.width(), t.height());
    }

    a.save("/tmp/img_100.png","PNG");
    b.save("/tmp/img_200.png","PNG");
    t.save("/tmp/img_transparent.png","PNG");
    printf("100%%: %dx%d   200%%: %dx%d   transparent(cropped): %dx%d\n",
           a.width(),a.height(), b.width(),b.height(), t.width(),t.height());
    printf(fails ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", fails);
    return fails ? 1 : 0;
}
