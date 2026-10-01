// Verifies the §11 viewport per-part hide/isolate actually removes a part from the render. Builds a
// two-part model — cube A on the left, cube B on the right, each its own MeshPart — shows a
// GLModelWidget offscreen, and checks that isolating part 0 makes the right-hand cube vanish (the
// opaque silhouette's right edge moves left), then Show-all brings it back.
#include "gl/GLModelWidget.h"
#include "model/ModelGeometry.h"
#include <QApplication>
#include <cstdio>

// Append a unit cube centered at cx as its own part.
static void addCube(ModelGeometry& g, float cx, const char* name)
{
    const int base = g.vertices.size();
    const uint32_t iStart = g.indices.size();
    const float c = 0.4f;
    const float pts[8][3] = {{-c,-c,-c},{c,-c,-c},{c,c,-c},{-c,c,-c},{-c,-c,c},{c,-c,c},{c,c,c},{-c,c,c}};
    for (auto& p : pts) { MeshVertex v{}; v.px=p[0]+cx; v.py=p[1]; v.pz=p[2]; v.nx=p[0]; v.ny=p[1]; v.nz=p[2]; v.tx=1; v.tw=1; g.vertices.append(v); }
    const int idx[36]={0,1,2,0,2,3, 4,6,5,4,7,6, 0,4,5,0,5,1, 1,5,6,1,6,2, 2,6,7,2,7,3, 3,7,4,3,4,0};
    for (int i : idx) g.indices.append(base + i);
    MeshPart part; part.indexStart=iStart; part.indexCount=36; part.materialIndex=-1; part.name=name; g.parts.append(part);
}

// Rightmost column that has any opaque pixel (−1 if none).
static int maxOpaqueX(const QImage& im)
{
    int mx=-1;
    for (int y=0;y<im.height();++y){ const QRgb* r=reinterpret_cast<const QRgb*>(im.constScanLine(y));
        for (int x=0;x<im.width();++x) if (qAlpha(r[x])>8 && x>mx) mx=x; }
    return mx;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    GLModelWidget w; w.resize(240, 160); w.show(); app.processEvents();
    ModelGeometry g;
    addCube(g, -1.0f, "left");
    addCube(g, +1.0f, "right");
    for (int k=0;k<3;++k){ g.bboxMin[k]=-1.4f; g.bboxMax[k]=1.4f; }
    w.setModel(g); app.processEvents();

    int fails=0; auto req=[&](bool ok,const char* m){ if(!ok){ printf("FAIL: %s\n",m); ++fails; } };
    req(w.partCount()==2, "expected 2 parts");

    QImage full = w.renderToImage(100, /*transparent*/true, /*crop*/false);
    const int mxFull = maxOpaqueX(full);

    w.isolateParts({0});                       // keep left, hide right
    req(w.hiddenParts().size()==1, "isolate should hide exactly one part");
    QImage iso = w.renderToImage(100, true, false);
    const int mxIso = maxOpaqueX(iso);

    w.clearHiddenParts();                       // show all
    req(w.hiddenParts().isEmpty(), "clearHiddenParts should leave nothing hidden");
    QImage back = w.renderToImage(100, true, false);
    const int mxBack = maxOpaqueX(back);

    // The right cube occupies the right portion of the frame. Isolating the left part must pull the
    // rightmost opaque column well to the left; showing all must restore it.
    req(mxFull>0 && mxIso>0 && mxBack>0, "nothing rendered");
    req(mxIso < mxFull - 20, "isolate did not remove the right-hand part");
    req(mxBack > mxIso + 20, "show-all did not restore the right-hand part");

    printf("rightmost opaque X: full=%d  isolate-left=%d  show-all=%d\n", mxFull, mxIso, mxBack);
    printf(fails? "RESULT: %d FAILURE(S)\n":"RESULT: PASS\n", fails);
    return fails?1:0;
}
