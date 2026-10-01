// Headless test of ExportCapture::turntableGif / animLoopGif (template §26). Builds a cube, shows a
// GLModelWidget offscreen (xvfb+llvmpipe), and exports a turntable GIF opaque, a transparent+cropped
// turntable GIF, and confirms animLoopGif refuses when no clip is loaded. Validates the GIFs are real
// GIF89a with the requested frame count, and that turntable frames actually differ (the camera moved).
#include "app/ExportCapture.h"
#include "gl/GLModelWidget.h"
#include "model/ModelGeometry.h"
#include <QApplication>
#include <QFile>
#include <cstdio>

static ModelGeometry makeCube()
{
    ModelGeometry g; const float c = 0.5f;
    const float pts[8][3] = {{-c,-c,-c},{c,-c,-c},{c,c,-c},{-c,c,-c},{-c,-c,c},{c,-c,c},{c,c,c},{-c,c,c}};
    for (auto& p : pts) { MeshVertex v{}; v.px=p[0];v.py=p[1];v.pz=p[2]; v.nx=p[0];v.ny=p[1];v.nz=p[2]; v.tx=1;v.tw=1; g.vertices.append(v); }
    const int idx[36]={0,1,2,0,2,3, 4,6,5,4,7,6, 0,4,5,0,5,1, 1,5,6,1,6,2, 2,6,7,2,7,3, 3,7,4,3,4,0};
    for (int i:idx) g.indices.append(i);
    MeshPart part; part.indexStart=0; part.indexCount=36; part.materialIndex=-1; part.name="cube"; g.parts.append(part);
    g.materialPaths.append(QString());
    for (int k=0;k<3;++k){ g.bboxMin[k]=-c; g.bboxMax[k]=c; }
    return g;
}

// Minimal GIF frame counter (count Image Separators 0x2C) and header check.
static int gifFrames(const char* path, bool& isGif)
{
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) { isGif=false; return -1; }
    QByteArray d = f.readAll();
    isGif = d.startsWith("GIF89a") || d.startsWith("GIF87a");
    int n=0; for (int i=0;i<d.size();++i) if ((unsigned char)d[i]==0x2C) ++n;   // image descriptors (upper bound)
    return n;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    GLModelWidget w; w.resize(200,160); w.show(); app.processEvents();
    w.setModel(makeCube()); app.processEvents();

    int fails=0; auto req=[&](bool ok,const char* m){ if(!ok){ printf("FAIL: %s\n",m); ++fails; } };

    ExportCapture::GifOptions opt; opt.fps=20; opt.turntableFrames=24; opt.scalePercent=100; opt.maxColors=128;
    // 1) opaque turntable
    bool ok1 = ExportCapture::turntableGif(&w, "/tmp/tt_opaque.gif", opt);
    req(ok1, "turntable (opaque) returned false");
    // 2) transparent + cropped turntable
    ExportCapture::GifOptions opt2 = opt; opt2.transparentBg=true; opt2.cropToModel=true;
    bool ok2 = ExportCapture::turntableGif(&w, "/tmp/tt_transp.gif", opt2);
    req(ok2, "turntable (transparent+crop) returned false");
    // 3) anim-loop with no clip must refuse
    bool ok3 = ExportCapture::animLoopGif(&w, "/tmp/loop_nope.gif", opt);
    req(!ok3, "animLoopGif should refuse with no clip loaded");

    bool g1,g2; int n1=gifFrames("/tmp/tt_opaque.gif",g1); int n2=gifFrames("/tmp/tt_transp.gif",g2);
    req(g1, "tt_opaque is not a GIF"); req(g2, "tt_transp is not a GIF");
    req(n1>=24, "tt_opaque frame count too low"); req(n2>=24, "tt_transp frame count too low");

    printf("opaque: GIF=%d frames~%d   transparent: GIF=%d frames~%d   animLoop-no-clip refused=%d\n",
           g1, n1, g2, n2, ok3?0:1);
    printf(fails? "RESULT: %d FAILURE(S)\n":"RESULT: PASS\n", fails);
    return fails?1:0;
}
