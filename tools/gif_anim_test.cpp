// Verifies the ANIMATED GIF paths end-to-end with a real (synthetic) clip — not "by construction".
// Builds a 1-bone skinned cube and a 30-frame clip (fps=30) that slides the bone along X, loads it into
// a GLModelWidget offscreen, then:
//   1) animLoopGif → a GIF whose frame count is the clip's NATIVE frame count (round(dur·fps)=30), whose
//      frames DIFFER (the cube moves), proving the clip's own frames/rate drive the loop (D4's method).
//   2) turntableGif with the clip PLAYING → the animated turntable branch (frames snapped to whole clip
//      loops), frames differ.
#include "app/ExportCapture.h"
#include "gl/GLModelWidget.h"
#include "model/ModelGeometry.h"
#include "model/AstSkeleton.h"
#include "model/RigMath.h"
#include <QApplication>
#include <QFile>
#include <cstdio>

static ModelGeometry skinnedCube()
{
    ModelGeometry g; const float c = 0.5f;
    const float pts[8][3] = {{-c,-c,-c},{c,-c,-c},{c,c,-c},{-c,c,-c},{-c,-c,c},{c,-c,c},{c,c,c},{-c,c,c}};
    for (auto& p : pts) {
        MeshVertex v{}; v.px=p[0];v.py=p[1];v.pz=p[2]; v.nx=p[0];v.ny=p[1];v.nz=p[2]; v.tx=1;v.tw=1;
        v.joints[0]=0; v.weights[0]=1.0f;   // 100% to bone 0 → geo.skinned
        g.vertices.append(v);
    }
    const int idx[36]={0,1,2,0,2,3, 4,6,5,4,7,6, 0,4,5,0,5,1, 1,5,6,1,6,2, 2,6,7,2,7,3, 3,7,4,3,4,0};
    for (int i:idx) g.indices.append(i);
    MeshPart part; part.indexStart=0; part.indexCount=36; part.materialIndex=-1; part.name="cube"; g.parts.append(part);
    g.materialPaths.append(QString());
    for (int k=0;k<3;++k){ g.bboxMin[k]=-c; g.bboxMax[k]=c; }
    g.skinned = true;
    return g;
}

static AstSkeleton::Skeleton animSkel()
{
    AstSkeleton::Skeleton s; s.version=12; s.valid=true; s.clipsDecoded=true;
    AstSkeleton::Bone b; b.name="root"; b.parent=-1; b.bind=RigMath::identity(); b.inverseBind=RigMath::identity();
    s.bones.append(b);
    AstSkeleton::Clip c; c.name="slide"; c.fps=30;
    AstSkeleton::KeySet k; k.nodeId=0;
    k.posTimes = {0.0f, 15.0f, 30.0f};                    // frame units → maxKeyTime 30, dur=1.0s
    k.pos = {{{0,0,0}}, {{0.8f,0,0}}, {{0,0,0}}};          // slide +X and back
    c.keys.append(k);
    s.clips.append(c);
    return s;
}

static int gifFrames(const char* path, bool& isGif)
{
    QFile f(path); if(!f.open(QIODevice::ReadOnly)){ isGif=false; return -1; }
    QByteArray d=f.readAll(); isGif=d.startsWith("GIF89a")||d.startsWith("GIF87a");
    int n=0; for(int i=0;i<d.size();++i) if((unsigned char)d[i]==0x2C) ++n; return n;
}

int main(int argc,char** argv)
{
    QApplication app(argc,argv);
    GLModelWidget w; w.resize(160,140); w.show(); app.processEvents();
    w.setModel(skinnedCube(), animSkel()); app.processEvents();

    int fails=0; auto req=[&](bool ok,const char* m){ if(!ok){ printf("FAIL: %s\n",m); ++fails; } };
    req(w.clipCount()>=1, "skeleton did not match mesh / no clips");
    w.setClip(0);
    req(w.currentClip()==0, "setClip(0) failed");
    req(w.currentClipFps()==30, "currentClipFps != 30");
    printf("clip: fps=%d dur=%.3fs → native frames=%d\n", w.currentClipFps(), w.clipDuration(),
           int(w.clipDuration()*w.currentClipFps()+0.5f));

    ExportCapture::GifOptions opt; opt.scalePercent=100; opt.maxColors=128; opt.transparentBg=true; opt.cropToModel=false;

    // 1) anim-loop — should be the clip's native 30 frames, and frames must differ (motion captured).
    bool okL = ExportCapture::animLoopGif(&w, "/tmp/anim_loop.gif", opt);
    req(okL, "animLoopGif returned false with a clip loaded");
    bool g; int n = gifFrames("/tmp/anim_loop.gif", g);
    req(g, "anim_loop is not a GIF");

    // 2) animated turntable — clip playing.
    w.setPlaying(true);
    ExportCapture::GifOptions t = opt; t.fps=20; t.turntableFrames=20;
    bool okT = ExportCapture::turntableGif(&w, "/tmp/anim_turntable.gif", t);
    req(okT, "turntableGif (animated) returned false");
    bool gt; int nt = gifFrames("/tmp/anim_turntable.gif", gt); req(gt, "anim_turntable is not a GIF");

    printf("anim-loop GIF frames~%d   animated turntable GIF frames~%d\n", n, nt);
    printf(fails? "RESULT: %d FAILURE(S)\n":"RESULT: PASS\n", fails);
    return fails?1:0;
}
