// Container-only: render a model headless with the REAL viewport shader + two-pass alpha, exactly as
// GLModelWidget does, and save a PNG. Lets a headless session SEE the result (geometry/materials/
// transparency) without the GUI — the visual counterpart to skin_diag/rig_cover. Bind pose only
// (static framing); that is what the viewport shows until a clip plays, and is enough to judge the
// mesh topology (vertex-explosion) and alpha compositing.
//
// usage: render_offscreen <bundlesDir> <model.smd> <out.png> [size=900] [yawDeg=35]

#include "store/AssetStore.h"
#include "model/GlbExporter.h"
#include "model/ModelGeometry.h"
#include "model/AstSkeleton.h"
#include "model/RigMath.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLVersionFunctionsFactory>
#include <QImage>
#include <QMatrix4x4>
#include <QVector3D>
#include <cmath>
#include <cstdio>
#include "/root/work/scratch/shaders.inc"

static GLuint mkTex(QOpenGLFunctions_3_3_Core* gl, const QImage& img, bool srgb) {
    if (img.isNull()) return 0;
    QImage im = img.convertToFormat(QImage::Format_RGBA8888);
    GLuint t=0; gl->glGenTextures(1,&t); gl->glBindTexture(GL_TEXTURE_2D,t);
    gl->glTexImage2D(GL_TEXTURE_2D,0, srgb?GL_SRGB8_ALPHA8:GL_RGBA8, im.width(),im.height(),0,GL_RGBA,GL_UNSIGNED_BYTE, im.constBits());
    gl->glGenerateMipmap(GL_TEXTURE_2D);
    gl->glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    gl->glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    return t;
}

int main(int argc, char** argv){
    QGuiApplication app(argc, argv);
    if (argc<4){ fprintf(stderr,"usage: render_offscreen <bundlesDir> <model.smd> <out.png> [size] [yaw]\n"); return 2; }
    const int SZ = argc>4 ? atoi(argv[4]) : 900;
    const float yaw = (argc>5 ? atof(argv[5]) : 35.f) * float(M_PI)/180.f;

    AssetStore store; QString err;
    if (!store.open(QString::fromLocal8Bit(argv[1]), &err)){ fprintf(stderr,"open: %s\n",qPrintable(err)); return 1; }
    ModelGeometry geo;
    if (!store.loadModel(QString::fromLocal8Bit(argv[2]).toLower(), geo, &err)){ fprintf(stderr,"load: %s\n",qPrintable(err)); return 1; }
    QVector<GlbExporter::ExportMaterial> mats = store.resolveMaterials(geo, /*decodeTextures*/true);
    geo.computeBounds();

    QSurfaceFormat f; f.setVersion(3,3); f.setProfile(QSurfaceFormat::CoreProfile); QSurfaceFormat::setDefaultFormat(f);
    QOffscreenSurface surf; surf.setFormat(f); surf.create();
    QOpenGLContext ctx; ctx.setFormat(f); if(!ctx.create()||!ctx.makeCurrent(&surf)){ fprintf(stderr,"ctx fail\n"); return 1; }
    auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(&ctx); gl->initializeOpenGLFunctions();

    // FBO
    GLuint fbo,col,dep; gl->glGenFramebuffers(1,&fbo); gl->glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    gl->glGenTextures(1,&col); gl->glBindTexture(GL_TEXTURE_2D,col);
    gl->glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,SZ,SZ,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,col,0);
    gl->glGenRenderbuffers(1,&dep); gl->glBindRenderbuffer(GL_RENDERBUFFER,dep);
    gl->glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,SZ,SZ);
    gl->glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,dep);

    // shaders
    auto sh=[&](GLenum t,const char* s){ GLuint o=gl->glCreateShader(t); gl->glShaderSource(o,1,&s,nullptr); gl->glCompileShader(o);
        GLint ok=0; gl->glGetShaderiv(o,GL_COMPILE_STATUS,&ok); if(!ok){char l[4096];gl->glGetShaderInfoLog(o,4096,nullptr,l);fprintf(stderr,"shader:%s\n",l);} return o; };
    GLuint prog=gl->glCreateProgram(); gl->glAttachShader(prog,sh(GL_VERTEX_SHADER,kVertS)); gl->glAttachShader(prog,sh(GL_FRAGMENT_SHADER,kFragS)); gl->glLinkProgram(prog);

    // Optional animation: clipIndex + time (seconds) → skin the body and drive attachment follow.
    const int clipIndex = argc>6 ? atoi(argv[6]) : -1;
    const float clipT   = argc>7 ? atof(argv[7]) : 0.f;
    AstSkeleton::Skeleton bodySkel = store.loadSkeletonFor(QString::fromLocal8Bit(argv[2]).toLower(), true, geo.jointPaletteSize());
    QVector<RigMath::Mat4> bodySkin;
    const bool anim = clipIndex>=0 && !bodySkel.bones.isEmpty();
    if (anim) bodySkin = AstSkeleton::skinMatrices(bodySkel, clipIndex, clipT);

    // interleaved verts pos3 nrm3 uv2 tan4 (native→Y-up), CPU-skinned when animating = 12 floats
    auto toYUp=[&](float x,float y,float z,float& ox,float& oy,float& oz){ ox=x; oy=z; oz=-y; }; // PoE2 Z-up→Y-up
    QVector<float> vb; vb.reserve(geo.vertices.size()*12);
    for (const MeshVertex& v: geo.vertices){ float px=v.px,py=v.py,pz=v.pz,nx=v.nx,ny=v.ny,nz=v.nz,tx=v.tx,ty=v.ty,tz=v.tz;
        if (anim){ float ap[3]={0,0,0},an[3]={0,0,0},at[3]={0,0,0},w=0;
            for(int k=0;k<4;k++){ float wk=v.weights[k]; if(wk<=0)continue; int b=v.joints[k]; if(b<0||b>=bodySkin.size())continue;
                float o[3]; RigMath::transformPoint(bodySkin[b],v.px,v.py,v.pz,o); ap[0]+=wk*o[0];ap[1]+=wk*o[1];ap[2]+=wk*o[2];
                RigMath::transformDir(bodySkin[b],v.nx,v.ny,v.nz,o); an[0]+=wk*o[0];an[1]+=wk*o[1];an[2]+=wk*o[2];
                RigMath::transformDir(bodySkin[b],v.tx,v.ty,v.tz,o); at[0]+=wk*o[0];at[1]+=wk*o[1];at[2]+=wk*o[2]; w+=wk; }
            if(w>1e-6f){ px=ap[0]/w;py=ap[1]/w;pz=ap[2]/w; nx=an[0];ny=an[1];nz=an[2]; tx=at[0];ty=at[1];tz=at[2]; } }
        float ox,oy,oz,onx,ony,onz,otx,oty,otz;
        toYUp(px,py,pz,ox,oy,oz); toYUp(nx,ny,nz,onx,ony,onz); toYUp(tx,ty,tz,otx,oty,otz);
        vb<<ox<<oy<<oz<<onx<<ony<<onz<<v.u<<v.v<<otx<<oty<<otz<<v.tw; }
    GLuint vao,vbo,ibo; gl->glGenVertexArrays(1,&vao); gl->glBindVertexArray(vao);
    gl->glGenBuffers(1,&vbo); gl->glBindBuffer(GL_ARRAY_BUFFER,vbo); gl->glBufferData(GL_ARRAY_BUFFER,vb.size()*4,vb.constData(),GL_STATIC_DRAW);
    gl->glGenBuffers(1,&ibo); gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo); gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER,geo.indices.size()*4,geo.indices.constData(),GL_STATIC_DRAW);
    for (int i=0;i<4;i++) gl->glEnableVertexAttribArray(i);
    gl->glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,48,(void*)0);
    gl->glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,48,(void*)12);
    gl->glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,48,(void*)24);
    gl->glVertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,48,(void*)32);

    // textures per material
    struct GM{GLuint b=0,n=0,mr=0,em=0,sc=0; int am=0;};
    QVector<GM> gm(mats.size());
    for (int i=0;i<mats.size();++i){ gm[i].b=mkTex(gl,mats[i].baseColor,true); gm[i].n=mkTex(gl,mats[i].normal,false);
        gm[i].mr=mkTex(gl,mats[i].metallicRoughness,false); gm[i].em=mkTex(gl,mats[i].emissive,false); gm[i].sc=mkTex(gl,mats[i].specularColor,true); gm[i].am=mats[i].alphaMode; }

    // camera framing the bbox
    QVector3D lo(geo.bboxMin[0],geo.bboxMin[2],-geo.bboxMax[1]), hi(geo.bboxMax[0],geo.bboxMax[2],-geo.bboxMin[1]);
    QVector3D c=(lo+hi)*0.5f; float rad=(hi-lo).length()*0.5f; if(rad<1e-3f)rad=1;
    QVector3D eye = c + QVector3D(std::sin(yaw),0.35f,std::cos(yaw))*rad*2.6f;
    QMatrix4x4 P; P.perspective(35.f,1.f,rad*0.05f,rad*20.f);
    QMatrix4x4 Vm; Vm.lookAt(eye,c,QVector3D(0,1,0));
    QMatrix4x4 M; QMatrix4x4 mvp=P*Vm*M;

    gl->glViewport(0,0,SZ,SZ); gl->glEnable(GL_DEPTH_TEST);
    gl->glClearColor(0.12f,0.12f,0.13f,1.f); gl->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    gl->glUseProgram(prog);
    gl->glUniformMatrix4fv(gl->glGetUniformLocation(prog,"uMVP"),1,GL_FALSE,mvp.constData());
    gl->glUniformMatrix4fv(gl->glGetUniformLocation(prog,"uModel"),1,GL_FALSE,M.constData());
    gl->glUniform3f(gl->glGetUniformLocation(prog,"uCam"),eye.x(),eye.y(),eye.z());
    gl->glUniform1i(gl->glGetUniformLocation(prog,"uShading"),1);
    gl->glUniform1i(gl->glGetUniformLocation(prog,"uChannel"),0);
    gl->glUniform1i(gl->glGetUniformLocation(prog,"uSelected"),0);
    gl->glUniform3f(gl->glGetUniformLocation(prog,"uSel"),1,1,1);
    for (int u=0;u<5;u++) gl->glUniform1i(gl->glGetUniformLocation(prog,(QString("u")+QStringList{"Base","Normal","MR","Emis","SpecColor"}[u]).toUtf8()),u);
    const int lHB=gl->glGetUniformLocation(prog,"uHasBase"),lHN=gl->glGetUniformLocation(prog,"uHasNormal"),lHM=gl->glGetUniformLocation(prog,"uHasMR"),
        lHE=gl->glGetUniformLocation(prog,"uHasEmis"),lHS=gl->glGetUniformLocation(prog,"uHasSpecColor"),lAM=gl->glGetUniformLocation(prog,"uAlphaMode"),
        lSSS=gl->glGetUniformLocation(prog,"uSSS"),lTR=gl->glGetUniformLocation(prog,"uTransl");
    auto draw=[&](const MeshPart& p){ const GM* m=(p.materialIndex>=0&&p.materialIndex<gm.size())?&gm[p.materialIndex]:nullptr;
        gl->glUniform1i(lHB,m&&m->b?1:0); gl->glUniform1i(lHN,m&&m->n?1:0); gl->glUniform1i(lHM,m&&m->mr?1:0);
        gl->glUniform1i(lHE,m&&m->em?1:0); gl->glUniform1i(lHS,m&&m->sc?1:0); gl->glUniform1i(lAM,m?m->am:0);
        gl->glUniform3f(lSSS,0,0,0); gl->glUniform1f(lTR,0);
        if(m&&m->b){gl->glActiveTexture(GL_TEXTURE0);gl->glBindTexture(GL_TEXTURE_2D,m->b);} if(m&&m->n){gl->glActiveTexture(GL_TEXTURE1);gl->glBindTexture(GL_TEXTURE_2D,m->n);}
        if(m&&m->mr){gl->glActiveTexture(GL_TEXTURE2);gl->glBindTexture(GL_TEXTURE_2D,m->mr);} if(m&&m->em){gl->glActiveTexture(GL_TEXTURE3);gl->glBindTexture(GL_TEXTURE_2D,m->em);}
        if(m&&m->sc){gl->glActiveTexture(GL_TEXTURE4);gl->glBindTexture(GL_TEXTURE_2D,m->sc);}
        gl->glDrawElements(GL_TRIANGLES,p.indexCount,GL_UNSIGNED_INT,(void*)(size_t(p.indexStart)*4)); };
    auto isT=[&](const MeshPart& p){ const GM* m=(p.materialIndex>=0&&p.materialIndex<gm.size())?&gm[p.materialIndex]:nullptr; return m&&(m->am==2||m->am==3); };
    for (const MeshPart& p: geo.parts) if(!isT(p)) draw(p);
    gl->glEnable(GL_BLEND); gl->glDepthMask(GL_FALSE);
    for (const MeshPart& p: geo.parts) if(isT(p)){ const GM* m=&gm[p.materialIndex];
        if(m->am==3) gl->glBlendFunc(GL_SRC_ALPHA,GL_ONE); else gl->glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); draw(p); }
    gl->glDepthMask(GL_TRUE); gl->glDisable(GL_BLEND);

    // ── Attachment assembly: place each declared attachment on its parent bone and draw it too ──
    int attachPieces = 0;
    {
        AstSkeleton::Skeleton& sk = bodySkel;
        AssetStore::Assembly asmbl = store.attachmentsForModel(QString::fromLocal8Bit(argv[2]).toLower());
        for (const auto& piece : asmbl.pieces) {
            if (piece.smdPath.isEmpty()) continue;
            ModelGeometry ag; QString ae;
            if (!store.loadModel(piece.smdPath, ag, &ae)) continue;
            QVector<GlbExporter::ExportMaterial> am = store.resolveMaterials(ag, true);
            RigMath::Mat4 T; for (int i=0;i<16;++i) T[i]=(i%5==0)?1.f:0.f;
            for (int bi=0; bi<sk.bones.size(); ++bi) if (sk.bones[bi].name.compare(piece.bone, Qt::CaseInsensitive)==0){
                T = (anim && bi<bodySkin.size()) ? RigMath::mul(sk.bones[bi].bind, bodySkin[bi]) : sk.bones[bi].bind; break; }
            QVector<float> avb; avb.reserve(ag.vertices.size()*12);
            for (const MeshVertex& v: ag.vertices){ float p[3],n[3],t[3]; RigMath::transformPoint(T,v.px,v.py,v.pz,p); RigMath::transformDir(T,v.nx,v.ny,v.nz,n); RigMath::transformDir(T,v.tx,v.ty,v.tz,t);
                float px,py,pz,nx,ny,nz,tx,ty,tz; toYUp(p[0],p[1],p[2],px,py,pz); toYUp(n[0],n[1],n[2],nx,ny,nz); toYUp(t[0],t[1],t[2],tx,ty,tz);
                avb<<px<<py<<pz<<nx<<ny<<nz<<v.u<<v.v<<tx<<ty<<tz<<v.tw; }
            GLuint av,ab,ai; gl->glGenVertexArrays(1,&av); gl->glBindVertexArray(av);
            gl->glGenBuffers(1,&ab); gl->glBindBuffer(GL_ARRAY_BUFFER,ab); gl->glBufferData(GL_ARRAY_BUFFER,avb.size()*4,avb.constData(),GL_STATIC_DRAW);
            gl->glGenBuffers(1,&ai); gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ai); gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER,ag.indices.size()*4,ag.indices.constData(),GL_STATIC_DRAW);
            for(int i=0;i<4;i++) gl->glEnableVertexAttribArray(i);
            gl->glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,48,(void*)0); gl->glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,48,(void*)12);
            gl->glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,48,(void*)24); gl->glVertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,48,(void*)32);
            QVector<GM> agm(am.size());
            for(int i=0;i<am.size();++i){ agm[i].b=mkTex(gl,am[i].baseColor,true); agm[i].n=mkTex(gl,am[i].normal,false); agm[i].mr=mkTex(gl,am[i].metallicRoughness,false); agm[i].em=mkTex(gl,am[i].emissive,false); agm[i].sc=mkTex(gl,am[i].specularColor,true); agm[i].am=am[i].alphaMode; }
            auto adraw=[&](const MeshPart& p){ const GM* m=(p.materialIndex>=0&&p.materialIndex<agm.size())?&agm[p.materialIndex]:nullptr;
                gl->glUniform1i(lHB,m&&m->b?1:0); gl->glUniform1i(lHN,m&&m->n?1:0); gl->glUniform1i(lHM,m&&m->mr?1:0); gl->glUniform1i(lHE,m&&m->em?1:0); gl->glUniform1i(lHS,m&&m->sc?1:0); gl->glUniform1i(lAM,m?m->am:0);
                gl->glUniform3f(lSSS,0,0,0); gl->glUniform1f(lTR,0);
                if(m&&m->b){gl->glActiveTexture(GL_TEXTURE0);gl->glBindTexture(GL_TEXTURE_2D,m->b);} if(m&&m->n){gl->glActiveTexture(GL_TEXTURE1);gl->glBindTexture(GL_TEXTURE_2D,m->n);}
                if(m&&m->mr){gl->glActiveTexture(GL_TEXTURE2);gl->glBindTexture(GL_TEXTURE_2D,m->mr);} if(m&&m->em){gl->glActiveTexture(GL_TEXTURE3);gl->glBindTexture(GL_TEXTURE_2D,m->em);}
                if(m&&m->sc){gl->glActiveTexture(GL_TEXTURE4);gl->glBindTexture(GL_TEXTURE_2D,m->sc);}
                gl->glDrawElements(GL_TRIANGLES,p.indexCount,GL_UNSIGNED_INT,(void*)(size_t(p.indexStart)*4)); };
            auto aisT=[&](const MeshPart& p){ const GM* m=(p.materialIndex>=0&&p.materialIndex<agm.size())?&agm[p.materialIndex]:nullptr; return m&&(m->am==2||m->am==3); };
            for(const MeshPart& p: ag.parts) if(!aisT(p)) adraw(p);
            gl->glEnable(GL_BLEND); gl->glDepthMask(GL_FALSE);
            for(const MeshPart& p: ag.parts) if(aisT(p)){ const GM* m=&agm[p.materialIndex]; if(m->am==3) gl->glBlendFunc(GL_SRC_ALPHA,GL_ONE); else gl->glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); adraw(p); }
            gl->glDepthMask(GL_TRUE); gl->glDisable(GL_BLEND);
            ++attachPieces;
            printf("  attached '%s' on bone '%s' (%s)\n", qPrintable(piece.label), qPrintable(piece.bone), qPrintable(piece.smdPath.section('/', -1)));
        }
    }

    QImage out(SZ,SZ,QImage::Format_RGBA8888);
    gl->glReadPixels(0,0,SZ,SZ,GL_RGBA,GL_UNSIGNED_BYTE,out.bits());
    out.mirror(false,true);
    out.save(QString::fromLocal8Bit(argv[3]));
    printf("rendered %s  (%d parts, %d mats)\n", argv[3], geo.parts.size(), mats.size());
    return 0;
}
