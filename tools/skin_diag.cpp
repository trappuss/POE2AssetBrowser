// Container-only: reproduce the viewport's skinning EXACTLY to diagnose "vertex explosion".
// Loads the model + skeleton the same way the app does (AssetStore::loadModel + loadSkeletonFor with
// the mesh's own joint-palette size as minBones), then reports:
//   - rest bbox (raw POSITION, what the viewport draws with skin=nullptr) — a huge/NaN bbox = a
//     PARSE explosion (independent of any skeleton),
//   - which skeleton was chosen (bone count + note),
//   - for each clip, the max |skinned coord| over a few sampled times — a blow-up here = an ANIMATION
//     explosion (skinMatrices/clip data), not a rest/parse one.
//
// usage: skin_diag <bundlesDir> <model.smd>

#include "store/AssetStore.h"
#include "model/AstSkeleton.h"
#include "model/ModelGeometry.h"
#include "model/RigMath.h"
#include <QCoreApplication>
#include <cmath>
#include <cstdio>

static void bbox(const ModelGeometry& g, double lo[3], double hi[3]) {
    lo[0]=lo[1]=lo[2]=1e30; hi[0]=hi[1]=hi[2]=-1e30;
    for (const MeshVertex& v : g.vertices) {
        const float p[3]={v.px,v.py,v.pz};
        for (int k=0;k<3;++k){ if(p[k]<lo[k])lo[k]=p[k]; if(p[k]>hi[k])hi[k]=p[k]; }
    }
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) { fprintf(stderr, "usage: skin_diag <bundlesDir> <model.smd>\n"); return 2; }
    AssetStore store; QString err;
    if (!store.open(QString::fromLocal8Bit(argv[1]), &err)) { fprintf(stderr, "open: %s\n", qPrintable(err)); return 1; }
    const QString modelPath = QString::fromLocal8Bit(argv[2]).toLower();

    ModelGeometry geo;
    if (!store.loadModel(modelPath, geo, &err)) { fprintf(stderr, "loadModel: %s\n", qPrintable(err)); return 1; }
    geo.computeBounds();

    double lo[3], hi[3]; bbox(geo, lo, hi);
    bool nan=false; for (const MeshVertex& v : geo.vertices) if (!std::isfinite(v.px)||!std::isfinite(v.py)||!std::isfinite(v.pz)){nan=true;break;}
    const double diag = std::sqrt(std::pow(hi[0]-lo[0],2)+std::pow(hi[1]-lo[1],2)+std::pow(hi[2]-lo[2],2));
    printf("%s\n", qPrintable(modelPath));
    printf("  verts=%d parts=%d skinned=%d palette=%d\n", geo.vertices.size(), geo.parts.size(), geo.skinned, geo.jointPaletteSize());
    printf("  REST bbox = [%.1f %.1f %.1f] .. [%.1f %.1f %.1f]  diag=%.1f  nan=%d\n",
           lo[0],lo[1],lo[2],hi[0],hi[1],hi[2],diag,nan);

    if (!geo.skinned) { printf("  (static mesh — no skinning)\n"); return 0; }

    const int minB = geo.jointPaletteSize();
    AstSkeleton::Skeleton sk = store.loadSkeletonFor(modelPath, /*decodeClips*/true, minB);
    printf("  skeleton: bones=%d  covers=%d  clips=%d  note='%s'\n",
           sk.bones.size(), (minB<=sk.bones.size()), sk.clips.size(), qPrintable(sk.note));
    if (sk.bones.isEmpty()) { printf("  (no skeleton loaded)\n"); return 0; }

    // Skin the mesh at (clip,t) and return BOTH the model extent (bbox size) and the centre offset.
    // extent ballooning >> rest diag = chaotic per-vertex SCATTER (a real explosion); extent staying
    // ~diag while the centre moves far = coherent ROOT MOTION (an entrance/leap — not a bug).
    auto sample = [&](int clip, float t, double& extent, double& centreOff){
        const QVector<RigMath::Mat4> skin = AstSkeleton::skinMatrices(sk, clip, t);
        const int nb = skin.size();
        double slo[3]={1e30,1e30,1e30}, shi[3]={-1e30,-1e30,-1e30};
        for (const MeshVertex& v : geo.vertices) {
            float ap[3]={0,0,0}, used=0;
            for (int j=0;j<4;++j){ float w=v.weights[j]; if(w<=0)continue; int b=v.joints[j]; if(b<0||b>=nb)continue;
                float o[3]; RigMath::transformPoint(skin[b], v.px,v.py,v.pz,o); ap[0]+=w*o[0];ap[1]+=w*o[1];ap[2]+=w*o[2]; used+=w; }
            if(used>1e-6f){ for(int k=0;k<3;++k){ double c=ap[k]/used; if(c<slo[k])slo[k]=c; if(c>shi[k])shi[k]=c; } }
        }
        extent = std::sqrt(std::pow(shi[0]-slo[0],2)+std::pow(shi[1]-slo[1],2)+std::pow(shi[2]-slo[2],2));
        centreOff = std::sqrt(std::pow((shi[0]+slo[0])/2,2)+std::pow((shi[1]+slo[1])/2,2)+std::pow((shi[2]+slo[2])/2,2));
    };
    double e0,c0; sample(-1,0.f,e0,c0);
    printf("  REST skin extent = %.1f  (matches raw diag %.1f)\n", e0, diag);
    for (int c=0;c<sk.clips.size() && c<30;++c){
        const float dur = AstSkeleton::clipDuration(sk, c);
        double maxExt=0, maxCtr=0;
        for(int s=0;s<=6;++s){ double e,ctr; sample(c, dur*s/6.f, e, ctr); maxExt=std::max(maxExt,e); maxCtr=std::max(maxCtr,ctr); }
        const bool scatter = maxExt > diag*3.0;         // extent blew up = real explosion
        const bool motion  = !scatter && maxCtr > diag*1.5; // centre far but shape intact = root motion
        printf("    clip[%2d] '%-22s' dur=%5.2fs  extent=%7.1f centre=%7.1f  %s\n",
               c, qPrintable(sk.clips[c].name), dur, maxExt, maxCtr,
               scatter ? "<-- SCATTER (explosion)" : motion ? "(root motion, shape intact)" : "ok");
    }
    return 0;
}
