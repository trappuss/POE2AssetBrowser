#include "gl/GLModelWidget.h"
#include "model/RigMath.h"
#include "model/AstSkeleton.h"

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QVector3D>
#include <QWheelEvent>
#include <cmath>

// Native (x,y,z) → view Y-up (x,-z,y): the same proper rotation the exporter uses, so the viewport
// and the .glb agree (docs/FORMATS.md §3.2).
static inline QVector3D toYUp(float x, float y, float z) { return QVector3D(x, -z, y); }

// Trim fully-transparent margins from an image (image export "crop to model", template §15). Scans for
// the bounding box of any pixel with alpha > 0, keeps a small even margin, and returns the crop; an
// entirely transparent image (nothing drawn) is returned unchanged so the caller never gets a 0×0.
static QImage cropTransparent(const QImage& src)
{
    if (src.isNull() || !src.hasAlphaChannel()) return src;
    int minX = src.width(), minY = src.height(), maxX = -1, maxY = -1;
    for (int y = 0; y < src.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        for (int x = 0; x < src.width(); ++x) {
            if (qAlpha(row[x]) != 0) {
                if (x < minX) minX = x; if (x > maxX) maxX = x;
                if (y < minY) minY = y; if (y > maxY) maxY = y;
            }
        }
    }
    if (maxX < minX || maxY < minY) return src;   // nothing opaque — leave as-is
    const int margin = 8;
    minX = qMax(0, minX - margin); minY = qMax(0, minY - margin);
    maxX = qMin(src.width()  - 1, maxX + margin);
    maxY = qMin(src.height() - 1, maxY + margin);
    return src.copy(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

namespace {
const char* kVert = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec2 aUv;
layout(location=3) in vec4 aTan;
uniform mat4 uMVP; uniform mat4 uModel;
out vec3 vN; out vec2 vUv; out vec3 vWorld; out vec4 vTan;
void main(){ vN = mat3(uModel)*aNrm; vTan = vec4(mat3(uModel)*aTan.xyz, aTan.w); vUv = aUv; vWorld=(uModel*vec4(aPos,1.0)).xyz; gl_Position = uMVP*vec4(aPos,1.0); }
)";
// Metallic-roughness PBR with normal mapping. A two-light rig plus a constant ambient — enough to
// read the surface honestly in the viewport (docs/FORMATS.md §6.1 packing). The channel viewer
// (uChannel) shows the raw material inputs instead of the lit result.
const char* kFrag = R"(#version 330 core
in vec3 vN; in vec2 vUv; in vec3 vWorld; in vec4 vTan; out vec4 FragColor;
uniform sampler2D uBase, uNormal, uMR, uEmis, uSpecColor;
uniform int uHasBase, uHasNormal, uHasMR, uHasEmis, uHasSpecColor;
uniform int uShading, uChannel;
uniform vec3 uSel; uniform int uSelected; uniform vec3 uCam;
uniform vec3 uSSS; uniform float uTransl;   // subsurface mid-tint (0=off); translucency 0..1
uniform int uAlphaMode;   // 0 Opaque, 1 Mask (cutout), 2 Blend (alpha-over), 3 Additive
uniform int uIsFur, uHasFurNoise, uHasFurMask;   // FurV2 shell-fur preview
uniform sampler2D uFurNoise, uFurMask;

vec3 F_Schlick(vec3 f0, float vh){ return f0 + (1.0-f0)*pow(1.0-vh,5.0); }
float D_GGX(float nh, float a){ float a2=a*a; float d=nh*nh*(a2-1.0)+1.0; return a2/(3.14159*d*d+1e-6); }
float V_Smith(float nl,float nv,float a){ float k=a*0.5; float gl=nl/(nl*(1.0-k)+k); float gv=nv/(nv*(1.0-k)+k); return gl*gv; }

void main(){
    vec2 uv = fract(vUv);
    vec4 baseT = uHasBase==1 ? texture(uBase, uv) : vec4(0.72,0.72,0.74,1.0);
    // Hard cutout ONLY for Mask materials (ForceAlphaTest). Never discard on the albedo alpha of an
    // Opaque material — for the AlbedoSpecMask family that alpha is a spec mask, and discarding on it
    // was what punched holes across solid surfaces. Blend/Additive keep every fragment and composite.
    if (uAlphaMode==1 && uHasBase==1 && baseT.a < 0.35) discard;
    vec3 albedo = baseT.rgb;

    // Geometric + mapped normal.
    vec3 Ng = normalize(vN);
    vec3 N = Ng;
    if (uHasNormal==1){
        vec3 t = normalize(vTan.xyz - Ng*dot(Ng,vTan.xyz));
        vec3 b = cross(Ng, t) * vTan.w;
        vec3 nT = texture(uNormal, uv).xyz*2.0 - 1.0;   // RGB tangent normal, z in B (§6.1)
        N = normalize(mat3(t,b,Ng) * nT);
    }
    float rough = 0.8, metal = 0.0, ao = 1.0;
    if (uHasMR==1){ vec4 mr = texture(uMR, uv); ao = mr.r; rough = clamp(mr.g,0.04,1.0); metal = mr.b; }  // ORM: R=AO G=rough B=metal

    // FurV2 shell-fur PREVIEW (glTF has no fur). The material carries NO albedo — only a strand noise
    // (Noise_TEX) and a fur-length mask (DepthMap_TEX) — so the plain PBR path draws it flat grey. Here
    // we fake a fur look from those real inputs: high-frequency strand detail from the noise, self-shadow
    // AO from the density mask, high roughness, and a soft backscatter fuzz rim added at the end.
    float furRim = 0.0;
    if (uIsFur==1){
        float dens   = uHasFurMask==1  ? texture(uFurMask,  uv).r        : 0.5;   // fur length / density
        // Two octaves of the strand noise (coarse clumps + fine hairs) give a clearer fur read than one.
        float strand = uHasFurNoise==1 ? (0.65*texture(uFurNoise, uv*14.0).r + 0.35*texture(uFurNoise, uv*40.0).r) : 0.5;
        albedo = vec3(0.72) * mix(0.26, 1.18, strand);       // neutral coat (Color mult ~white), strong strand light/dark
        ao    *= mix(0.5, 1.0, dens);                         // denser/longer fur self-shadows at the base
        rough  = 0.93;                                        // fur reads matte
        vec3 Vf = normalize(uCam - vWorld);
        furRim = pow(1.0 - max(dot(normalize(vN), Vf), 0.0), 2.5) * (0.4 + 0.5*dens);   // fuzzy backscatter edge
    }

    // Channel viewer: show raw inputs, not the lit surface.
    if (uChannel>0){
        vec3 c = vec3(0.0);
        if (uChannel==1) c = uHasNormal==1 ? texture(uNormal,uv).rgb : (Ng*0.5+0.5);  // Normal
        else if (uChannel==2) c = vec3(rough);                                         // Roughness
        else if (uChannel==3) c = vec3(metal);                                         // Metallic
        else if (uChannel==4) c = vec3(ao);                                            // AO (ORM.R)
        else if (uChannel==5) c = uHasEmis==1 ? pow(texture(uEmis,uv).rgb,vec3(1.0/2.2)) : vec3(0.0); // Emissive (→display)
        else if (uChannel==6) c = pow(albedo, vec3(1.0/2.2));                           // Flat/base (linear→display)
        FragColor = vec4(c,1.0); return;
    }

    if (uShading==2){ FragColor = vec4(pow(albedo,vec3(1.0/2.2)),1.0); return; }  // wireframe pass (linear→display)

    vec3 V = normalize(uCam - vWorld);
    // Spec-gloss: an explicit RGB specular-colour map drives F0 directly; else the metal-rough F0.
    vec3 f0 = uHasSpecColor==1 ? texture(uSpecColor, uv).rgb : mix(vec3(0.04), albedo, metal);
    vec3 diffuse = albedo*(1.0-metal);
    float a = rough*rough;
    vec3 lit = vec3(0.0);
    // Two directional lights.
    vec3 Ls[2]; Ls[0]=normalize(vec3(0.4,0.8,0.6)); Ls[1]=normalize(vec3(-0.5,0.3,-0.7));
    float Li[2]; Li[0]=1.0; Li[1]=0.35;
    bool sssOn = (uSSS.r+uSSS.g+uSSS.b) > 0.0;
    for (int i=0;i<2;i++){
        vec3 L=Ls[i]; vec3 H=normalize(L+V);
        float nl=max(dot(N,L),0.0), nv=max(dot(N,V),1e-3), nh=max(dot(N,H),0.0), vh=max(dot(V,H),0.0);
        vec3 F=F_Schlick(f0,vh); float D=D_GGX(nh,a); float Vs=V_Smith(nl,nv,a);
        vec3 spec = F*D*Vs;
        lit += (diffuse/3.14159 + spec) * nl * Li[i];
        // Subsurface (SSS): tint the terminator/shadowed side with the authored mid-depth colour.
        if (sssOn) lit += diffuse * uSSS * smoothstep(0.0,0.7,1.0-nl) * Li[i];
        // Translucency: light transmitted through thin surfaces (back-lit glow).
        if (uTransl>0.0) lit += diffuse * max(dot(-N,L),0.0) * uTransl * Li[i];
    }
    if (uShading==0) lit = diffuse * (0.4+0.6*max(dot(N,V),0.0));   // Flat: unlit-ish base
    vec3 ambient = diffuse * 0.18 * ao;                            // ambient occlusion
    vec3 col = lit + ambient;
    if (uIsFur==1) col += furRim * albedo * 0.6;                    // fur backscatter fuzz
    if (uHasEmis==1) col += texture(uEmis,uv).rgb;
    col = col/(col+vec3(1.0));                 // Reinhard tonemap
    col = pow(col, vec3(1.0/2.2));             // gamma
    if (uSelected==1) col = mix(col, uSel, 0.35);
    // Blend (2) and Additive (3) carry the albedo alpha as coverage; the draw loop sets the matching
    // blend equation (alpha-over vs add). Opaque/Mask write alpha 1. Additive with no albedo defaults
    // to a luminance coverage so an untextured glow still fades by its own brightness.
    float outA = 1.0;
    if (uAlphaMode==2 || uAlphaMode==3) outA = (uHasBase==1) ? baseT.a : clamp(max(col.r,max(col.g,col.b)),0.0,1.0);
    FragColor = vec4(col, outA);
}
)";
const char* kPickVert = R"(#version 330 core
layout(location=0) in vec3 aPos; uniform mat4 uMVP; void main(){ gl_Position = uMVP*vec4(aPos,1.0); }
)";
const char* kPickFrag = R"(#version 330 core
out vec4 FragColor; uniform vec3 uId; void main(){ FragColor = vec4(uId,1.0); }
)";
const char* kLineVert = R"(#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aCol; uniform mat4 uMVP; out vec3 vc;
void main(){ vc=aCol; gl_Position=uMVP*vec4(aPos,1.0); }
)";
const char* kLineFrag = R"(#version 330 core
in vec3 vc; out vec4 FragColor; void main(){ FragColor=vec4(vc,1.0); }
)";

GLuint link(QOpenGLFunctions_3_3_Core* f, const char* vs, const char* fs)
{
    auto c = [&](GLenum t, const char* s){ GLuint sh = f->glCreateShader(t); f->glShaderSource(sh,1,&s,nullptr); f->glCompileShader(sh); return sh; };
    GLuint v = c(GL_VERTEX_SHADER, vs), fr = c(GL_FRAGMENT_SHADER, fs);
    GLuint p = f->glCreateProgram(); f->glAttachShader(p,v); f->glAttachShader(p,fr); f->glLinkProgram(p);
    f->glDeleteShader(v); f->glDeleteShader(fr); return p;
}
}

GLModelWidget::GLModelWidget(QWidget* parent) : QOpenGLWidget(parent) { setFocusPolicy(Qt::StrongFocus); }
GLModelWidget::~GLModelWidget()
{
    if (context()) { makeCurrent();
        for (const GpuMat& g : m_gpuMat) { for (GLuint t : {g.base, g.normal, g.mr, g.emissive, g.specColor, g.furNoise, g.furMask}) if (t) glDeleteTextures(1, &t); }
        releaseAttachmentGpu();
        if (m_prog) glDeleteProgram(m_prog); if (m_pickProg) glDeleteProgram(m_pickProg); if (m_lineProg) glDeleteProgram(m_lineProg);
        if (m_gridVbo) glDeleteBuffers(1, &m_gridVbo); if (m_gridVao) glDeleteVertexArrays(1, &m_gridVao);
        if (m_boneVbo) glDeleteBuffers(1, &m_boneVbo); if (m_boneVao) glDeleteVertexArrays(1, &m_boneVao);
        doneCurrent(); }
}

void GLModelWidget::setModel(const ModelGeometry& geo, const AstSkeleton::Skeleton& skel)
{
    m_geo = geo; m_skel = skel; m_selected.clear(); m_hidden.clear();
    m_pendingUpload = true;
    // Reset animation to the bind pose for the new model.
    m_clip = -1; m_time = 0.0f; m_clipDur = 0.0f; m_playing = false; m_frameDirty = false;
    // Does the skeleton cover the mesh's joint palette? A body-armour piece is skinned to a
    // character rig it does not carry (docs/FORMATS.md §7), so its joint indices run past the bones
    // in any .ast found beside it — detect that and refuse to animate against the wrong rig.
    m_skelMatchesMesh = false;
    if (m_geo.skinned && !m_skel.bones.isEmpty()) {
        int maxJoint = -1;
        for (const MeshVertex& v : m_geo.vertices)
            for (int j = 0; j < 4; ++j) if (v.weights[j] > 0 && v.joints[j] > maxJoint) maxJoint = v.joints[j];
        m_skelMatchesMesh = (maxJoint >= 0 && maxJoint < m_skel.bones.size());
    }
    emit animTimeChanged(0.0f, 0.0f);
    // Fit camera.
    QVector3D lo(geo.bboxMin[0], geo.bboxMin[1], geo.bboxMin[2]);
    QVector3D hi(geo.bboxMax[0], geo.bboxMax[1], geo.bboxMax[2]);
    QVector3D c = toYUp((lo.x()+hi.x())/2, (lo.y()+hi.y())/2, (lo.z()+hi.z())/2);
    m_target = c;
    m_radius = qMax(0.001f, (toYUp(hi.x(),hi.y(),hi.z()) - toYUp(lo.x(),lo.y(),lo.z())).length() * 0.5f);
    m_dist = m_radius * 2.6f;
    update();
    emitStats();
}

void GLModelWidget::setMaterialTextures(const QVector<MaterialTextures>& m) { m_mat = m; m_pendingTex = true; update(); }
void GLModelWidget::setPartTextures(const QVector<QImage>& tex)
{
    m_mat.clear(); m_mat.reserve(tex.size());
    for (const QImage& img : tex) { MaterialTextures mt; mt.baseColor = img; m_mat.append(mt); }
    m_pendingTex = true; update();
}
void GLModelWidget::clearModel() { m_geo = ModelGeometry(); m_skel = AstSkeleton::Skeleton(); m_selected.clear(); m_pendingUpload = true; update(); }

void GLModelWidget::setShading(Shading s) { m_shading = s; update(); }
void GLModelWidget::setChannel(Channel c) { m_channel = c; update(); }

void GLModelWidget::setOverlaysOn(bool on) { m_overlaysOn = on; update(); }
void GLModelWidget::setShowGrid(bool on) { m_showGrid = on; update(); }
void GLModelWidget::setShowSkeleton(bool on) { m_showSkeleton = on; update(); }
void GLModelWidget::setShowStats(bool on) { m_showStats = on; update(); emitStats(); }

void GLModelWidget::setSelectedParts(const QSet<int>& parts) { m_selected = parts; update(); emit selectionChanged(m_selected); }

QStringList GLModelWidget::clipNames() const
{
    if (!m_skelMatchesMesh) return {};   // rig mismatch → no playback offered
    QStringList out;
    for (const AstSkeleton::Clip& c : m_skel.clips)
        out << (c.name.isEmpty() ? QStringLiteral("(clip %1)").arg(out.size()) : c.name);
    return out;
}

void GLModelWidget::setClip(int index)
{
    if (!m_skelMatchesMesh) index = -1;
    if (index < -1 || index >= m_skel.clips.size()) index = -1;
    // A clip can only play if its keys were decoded; otherwise fall back to the bind pose.
    if (index >= 0 && !m_skel.clipsDecoded) index = -1;
    m_clip = index;
    m_time = 0.0f;
    m_clipDur = (index >= 0) ? AstSkeleton::clipDuration(m_skel, index) : 0.0f;
    m_playing = false;
    m_frameDirty = true;
    update();
    emit animTimeChanged(m_time, m_clipDur);
}

void GLModelWidget::setClips(const QVector<AstSkeleton::Clip>& clips)
{
    // Swap the clip list only; the bones (and thus m_skelMatchesMesh, the skinning bind) are unchanged,
    // so a retargeted library clip whose nodeIds index these bones skins correctly. Back to bind pose.
    m_skel.clips = clips;
    m_skel.clipsDecoded = !clips.isEmpty();
    m_clip = -1; m_time = 0.0f; m_clipDur = 0.0f; m_playing = false; m_frameDirty = true;
    update();
    emit animTimeChanged(0.0f, 0.0f);
}

void GLModelWidget::setPlaying(bool on)
{
    if (m_clip < 0 || m_clipDur <= 0.0f) on = false;   // nothing to play
    m_playing = on;
    if (on) { m_lastTickNs = m_clock.isValid() ? m_clock.nsecsElapsed() : 0; if (!m_clock.isValid()) m_clock.start(); update(); }
}

void GLModelWidget::setAnimTime(float seconds)
{
    if (m_clipDur <= 0.0f) { m_time = 0; }
    else { m_time = std::fmod(std::fmax(0.0f, seconds), m_clipDur); }
    m_frameDirty = true;
    update();
    emit animTimeChanged(m_time, m_clipDur);
}

void GLModelWidget::advancePlayback()
{
    if (!m_playing || m_clipDur <= 0.0f) return;
    if (!m_clock.isValid()) { m_clock.start(); m_lastTickNs = 0; }
    const qint64 now = m_clock.nsecsElapsed();
    const float dt = float(double(now - m_lastTickNs) * 1e-9);
    m_lastTickNs = now;
    m_time = std::fmod(m_time + dt, m_clipDur);
    m_frameDirty = true;
    emit animTimeChanged(m_time, m_clipDur);
}

void GLModelWidget::uploadSkinnedFrame()
{
    m_frameDirty = false;
    if (m_geo.isEmpty()) return;
    QVector<float> verts;
    if (m_clip >= 0 && m_skel.clipsDecoded && !m_skel.bones.isEmpty()) {
        const QVector<RigMath::Mat4> skin = AstSkeleton::skinMatrices(m_skel, m_clip, m_time);
        fillVertexBuffer(verts, &skin);
        reskinAttachments(&skin);           // attachments rigidly follow their bone through the clip
    } else {
        fillVertexBuffer(verts, nullptr);   // bind pose
        reskinAttachments(nullptr);         // …and snap back to rest when playback stops
    }
    m_vao.bind();
    m_vbo.bind(); m_vbo.allocate(verts.constData(), verts.size()*4);
    m_vao.release();
}

void GLModelWidget::initializeGL()
{
    if (!initializeOpenGLFunctions()) { m_glBroken = true; return; }   // no usable context → draw nothing rather than crash
    m_clock.start();
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.13f, 0.13f, 0.15f, 1.0f);
    m_prog = link(this, kVert, kFrag);
    m_pickProg = link(this, kPickVert, kPickFrag);
    m_lineProg = link(this, kLineVert, kLineFrag);
    m_vao.create(); m_vbo.create(); m_ibo.create();

    glGenVertexArrays(1, &m_gridVao); glGenBuffers(1, &m_gridVbo);
    glGenVertexArrays(1, &m_boneVao); glGenBuffers(1, &m_boneVbo);
    buildGrid();   // default size until a model sets m_radius
}

void GLModelWidget::buildGrid()
{
    if (m_gridVao == 0) return;
    // Size the grid to the loaded model: a round cell size giving ~20 cells across the model, so a
    // large character isn't standing on a postage stamp and a small prop isn't lost on a huge plane.
    const float span = qMax(0.001f, m_radius) * 2.0f;          // ~model bounding diameter
    const float raw  = span / 20.0f;
    const float mag  = std::pow(10.0f, std::floor(std::log10(qMax(1e-4f, raw))));
    const float norm = raw / mag;
    const float step = (norm < 1.5f ? 1.0f : norm < 3.5f ? 2.0f : norm < 7.5f ? 5.0f : 10.0f) * mag;
    const int   N    = 24;                                     // cells each side of centre
    const float ext  = N * step;

    // Ground height: the model's lowest point in the Y-up frame, so the grid sits under its feet
    // rather than at an arbitrary y=0 that may be far above or below it.
    float groundY = 0.0f;
    if (!m_geo.isEmpty()) {
        groundY = 1e30f;
        for (int c = 0; c < 8; ++c) {
            const QVector3D p = toYUp(c & 1 ? m_geo.bboxMax[0] : m_geo.bboxMin[0],
                                     c & 2 ? m_geo.bboxMax[1] : m_geo.bboxMin[1],
                                     c & 4 ? m_geo.bboxMax[2] : m_geo.bboxMin[2]);
            groundY = qMin(groundY, p.y());
        }
    }

    QVector<float> grid;
    grid.reserve((2*N + 1) * 4 * 6);
    for (int i = -N; i <= N; ++i) {
        const bool axis = (i == 0);
        const float r = axis ? 0.5f : 0.32f, g = axis ? 0.5f : 0.32f, b = axis ? 0.55f : 0.36f;
        grid << i*step << groundY << -ext << r << g << b;   grid << i*step << groundY <<  ext << r << g << b;
        grid << -ext   << groundY <<  i*step << r << g << b; grid <<  ext   << groundY <<  i*step << r << g << b;
    }
    m_gridVerts = grid.size() / 6;
    glBindVertexArray(m_gridVao); glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
    glBufferData(GL_ARRAY_BUFFER, grid.size()*4, grid.constData(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*4, nullptr);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*4, (void*)(3*4));
    glBindVertexArray(0);
}

void GLModelWidget::fillVertexBuffer(QVector<float>& verts, const QVector<RigMath::Mat4>* skin) const
{
    // Interleaved: pos(3) nrm(3) uv(2) tan(4) in the Y-up frame. Stride 12 floats. When `skin` is
    // supplied (CPU skinning), each vertex is transformed in the NATIVE frame by its ≤4 weighted
    // bone matrices, then rotated to Y-up — so the skinning stays in the same convention as the bind
    // matrices (docs/FORMATS.md §8) and only the final toYUp is shared with the static path.
    verts.clear(); verts.reserve(m_geo.vertices.size()*12);
    const int nb = skin ? skin->size() : 0;
    for (const MeshVertex& v : m_geo.vertices) {
        float px = v.px, py = v.py, pz = v.pz;
        float nx = v.nx, ny = v.ny, nz = v.nz, tx = v.tx, ty = v.ty, tz = v.tz;
        if (skin && nb > 0) {
            // Blend the ≤4 bone influences, normalising by the sum of the weights we actually
            // applied — a joint index outside the skeleton is skipped without collapsing the vertex
            // toward the origin. (setModel already gates clip playback on the skeleton covering the
            // mesh's palette, so in practice all four are in range; this stays fail-closed anyway.)
            float ap[3]={0,0,0}, an[3]={0,0,0}, at[3]={0,0,0}, used=0;
            for (int j = 0; j < 4; ++j) {
                float w = v.weights[j]; if (w <= 0) continue;
                int b = v.joints[j]; if (b < 0 || b >= nb) continue;
                const RigMath::Mat4& m = (*skin)[b];
                float o[3];
                RigMath::transformPoint(m, v.px, v.py, v.pz, o); ap[0]+=w*o[0]; ap[1]+=w*o[1]; ap[2]+=w*o[2];
                RigMath::transformDir(m, v.nx, v.ny, v.nz, o);   an[0]+=w*o[0]; an[1]+=w*o[1]; an[2]+=w*o[2];
                RigMath::transformDir(m, v.tx, v.ty, v.tz, o);   at[0]+=w*o[0]; at[1]+=w*o[1]; at[2]+=w*o[2];
                used += w;
            }
            if (used > 1e-6f) {
                const float ws = 1.0f/used;
                px=ap[0]*ws; py=ap[1]*ws; pz=ap[2]*ws;
                nx=an[0]; ny=an[1]; nz=an[2]; tx=at[0]; ty=at[1]; tz=at[2];
            }   // else: fully-unriggable vertex → leave at bind
        }
        QVector3D p = toYUp(px, py, pz), n = toYUp(nx, ny, nz), t = toYUp(tx, ty, tz);
        n.normalize(); if (t.lengthSquared() > 1e-12f) t.normalize();
        verts << p.x() << p.y() << p.z() << n.x() << n.y() << n.z() << v.u << v.v
              << t.x() << t.y() << t.z() << v.tw;
    }
}

void GLModelWidget::rebuildBuffers()
{
    m_pendingUpload = false;
    m_gpuParts.clear();
    buildGrid();   // resize the ground grid to this model (m_radius set in setModel)
    if (m_geo.isEmpty()) return;
    // Bind-pose fill (skinning happens per-frame via uploadSkinnedFrame()).
    QVector<float> verts; fillVertexBuffer(verts, nullptr);
    m_vao.bind();
    m_vbo.bind(); m_vbo.allocate(verts.constData(), verts.size()*4);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12*4, nullptr);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 12*4, (void*)(3*4));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 12*4, (void*)(6*4));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 12*4, (void*)(8*4));
    m_ibo.bind(); m_ibo.allocate(m_geo.indices.constData(), m_geo.indices.size()*4);
    m_vao.release();
    for (int i = 0; i < m_geo.parts.size(); ++i) {
        const MeshPart& part = m_geo.parts[i];
        m_gpuParts.append({int(part.indexStart), int(part.indexCount), part.materialIndex, i});
    }
    // Bone lines.
    if (!m_skel.bones.isEmpty()) {
        QVector<float> lines;
        for (int i = 0; i < m_skel.bones.size(); ++i) {
            const auto& b = m_skel.bones[i]; if (b.parent < 0) continue;
            float tc[3], tp[3]; RigMath::translationOf(b.bind, tc); RigMath::translationOf(m_skel.bones[b.parent].bind, tp);
            QVector3D c = toYUp(tc[0], tc[1], tc[2]), p = toYUp(tp[0], tp[1], tp[2]);
            lines << p.x() << p.y() << p.z() << 1.0f << 0.8f << 0.2f << c.x() << c.y() << c.z() << 1.0f << 0.8f << 0.2f;
        }
        m_boneVerts = lines.size() / 6;
        glBindVertexArray(m_boneVao); glBindBuffer(GL_ARRAY_BUFFER, m_boneVbo);
        glBufferData(GL_ARRAY_BUFFER, lines.size()*4, lines.constData(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*4, nullptr);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*4, (void*)(3*4));
        glBindVertexArray(0);
    } else m_boneVerts = 0;
}

void GLModelWidget::uploadTextures()
{
    m_pendingTex = false;
    // Free any previous GPU textures.
    for (const GpuMat& g : m_gpuMat) { for (GLuint t : {g.base, g.normal, g.mr, g.emissive, g.specColor, g.furNoise, g.furMask}) if (t) glDeleteTextures(1, &t); }
    m_gpuMat.clear();

    // Upload one QImage as an RGBA8 2D texture with mipmaps. `srgb` selects the sRGB internal format
    // (base colour is authored in sRGB; normal/MR/emissive-as-data stay linear). Returns 0 for null.
    auto upload = [&](const QImage& img, bool srgb, bool mip = true) -> GLuint {
        if (img.isNull()) return 0;
        const QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
        GLuint id = 0;
        glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
        // The fur strand noise is deliberately NOT mipmapped: mip-averaging a high-frequency grain to
        // grey is exactly what erased the fur detail, so it samples sharp (GL_LINEAR).
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mip ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8,
                     rgba.width(), rgba.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.constBits());
        if (mip) glGenerateMipmap(GL_TEXTURE_2D);
        return id;
    };

    m_gpuMat.reserve(m_mat.size());
    for (const MaterialTextures& mt : m_mat) {
        GpuMat g;
        g.base     = upload(mt.baseColor,  /*srgb*/true);
        g.normal   = upload(mt.normal,     /*srgb*/false);
        g.mr       = upload(mt.metalRough, /*srgb*/false);
        g.emissive = upload(mt.emissive,   /*srgb*/true);
        g.specColor = upload(mt.specColor, /*srgb*/true);   // RGB specular colour (spec-gloss)
        for (int c = 0; c < 3; ++c) g.subsurface[c] = mt.subsurface[c];
        g.translucent = mt.translucent;
        g.alphaMode = mt.alphaMode;
        g.isFur = mt.isFur; g.furDepth = mt.furDepth;
        if (mt.isFur) { g.furNoise = upload(mt.furNoise, /*srgb*/false, /*mip*/false); g.furMask = upload(mt.furMask, /*srgb*/false, /*mip*/false); }
        m_gpuMat.append(g);
    }
}

// Upload one QImage as an RGBA8 mipmapped texture (shared shape with uploadTextures' local lambda).
static GLuint uploadTex2D(QOpenGLFunctions_3_3_Core* gl, const QImage& img, bool srgb)
{
    if (img.isNull()) return 0;
    const QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
    GLuint id = 0; gl->glGenTextures(1, &id); gl->glBindTexture(GL_TEXTURE_2D, id);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, rgba.width(), rgba.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.constBits());
    gl->glGenerateMipmap(GL_TEXTURE_2D);
    return id;
}

void GLModelWidget::setAttachments(const QVector<Attachment>& atts)
{
    m_attach = atts;
    m_attachDirty = true;
    if (isValid()) { makeCurrent(); rebuildAttachments(); doneCurrent(); }
    update();
}

void GLModelWidget::setAttachmentVisible(int index, bool on)
{
    if (index >= 0 && index < m_attach.size())    m_attach[index].visible = on;
    if (index >= 0 && index < m_attachGpu.size())  m_attachGpu[index].visible = on;
    update();
}

void GLModelWidget::clearAttachments()
{
    m_attach.clear();
    if (isValid()) { makeCurrent(); releaseAttachmentGpu(); doneCurrent(); } else m_attachGpu.clear();
    update();
}

void GLModelWidget::releaseAttachmentGpu()
{
    for (AttachGpu& a : m_attachGpu) {
        for (const GpuMat& g : a.mats) for (GLuint t : {g.base, g.normal, g.mr, g.emissive, g.specColor}) if (t) glDeleteTextures(1, &t);
        if (a.ibo) glDeleteBuffers(1, &a.ibo);
        if (a.vbo) glDeleteBuffers(1, &a.vbo);
        if (a.vao) glDeleteVertexArrays(1, &a.vao);
    }
    m_attachGpu.clear();
}

// Bake each attachment into the body's Y-up frame at its parent bone's bind transform and upload it.
// Bone match is by name against the loaded body skeleton; an unmatched bone (or no skeleton) places
// the piece at the origin so it is still visible (and the caller can see the bone name was unresolved).
void GLModelWidget::rebuildAttachments()
{
    m_attachDirty = false;
    releaseAttachmentGpu();
    if (m_glBroken || m_attach.isEmpty()) return;

    for (const Attachment& at : m_attach) {
        // Resolve the parent bone once (by name) and bake at its bind transform. The index is kept so
        // a playing body animation can re-place the piece per frame without re-searching (reskin…).
        int boneIdx = -1;
        for (int bi = 0; bi < m_skel.bones.size(); ++bi)
            if (m_skel.bones[bi].name.compare(at.bone, Qt::CaseInsensitive) == 0) { boneIdx = bi; break; }
        RigMath::Mat4 T; for (int i = 0; i < 16; ++i) T[i] = (i % 5 == 0) ? 1.0f : 0.0f;   // identity if unmatched
        if (boneIdx >= 0) T = m_skel.bones[boneIdx].bind;

        QVector<float> vb; bakeAttachmentVerts(at.geo, T, vb);

        AttachGpu g; g.visible = at.visible; g.boneIndex = boneIdx; g.vertCount = at.geo.vertices.size();
        glGenVertexArrays(1, &g.vao); glBindVertexArray(g.vao);
        glGenBuffers(1, &g.vbo); glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
        glBufferData(GL_ARRAY_BUFFER, vb.size() * 4, vb.constData(), GL_STATIC_DRAW);
        glGenBuffers(1, &g.ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, at.geo.indices.size() * 4, at.geo.indices.constData(), GL_STATIC_DRAW);
        for (int i = 0; i < 4; ++i) glEnableVertexAttribArray(i);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 48, (void*)0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 48, (void*)12);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 48, (void*)24);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 48, (void*)32);
        glBindVertexArray(0);

        for (const MaterialTextures& mt : at.mats) {
            GpuMat m;
            m.base = uploadTex2D(this, mt.baseColor, true); m.normal = uploadTex2D(this, mt.normal, false);
            m.mr = uploadTex2D(this, mt.metalRough, false); m.emissive = uploadTex2D(this, mt.emissive, true);
            m.specColor = uploadTex2D(this, mt.specColor, true);
            for (int c = 0; c < 3; ++c) m.subsurface[c] = mt.subsurface[c];
            m.translucent = mt.translucent; m.alphaMode = mt.alphaMode;
            g.mats.append(m);
        }
        for (const MeshPart& p : at.geo.parts)
            g.parts.append(GpuPart{ int(p.indexStart), int(p.indexCount), p.materialIndex, -1 });
        m_attachGpu.append(g);
    }
}

// Bake one attachment's vertices by `xform` (its parent bone's world matrix in the native frame),
// then rotate to Y-up — the interleaved pos3/nrm3/uv2/tan4 layout the shared vertex shader expects.
void GLModelWidget::bakeAttachmentVerts(const ModelGeometry& geo, const RigMath::Mat4& xform, QVector<float>& out) const
{
    out.clear(); out.reserve(geo.vertices.size() * 12);
    for (const MeshVertex& v : geo.vertices) {
        float p[3], n[3], t[3];
        RigMath::transformPoint(xform, v.px, v.py, v.pz, p);
        RigMath::transformDir(xform, v.nx, v.ny, v.nz, n);
        RigMath::transformDir(xform, v.tx, v.ty, v.tz, t);
        const QVector3D P = toYUp(p[0], p[1], p[2]), N = toYUp(n[0], n[1], n[2]), Tn = toYUp(t[0], t[1], t[2]);
        out << P.x() << P.y() << P.z() << N.x() << N.y() << N.z() << v.u << v.v << Tn.x() << Tn.y() << Tn.z() << v.tw;
    }
}

// Re-place every attachment for the current animation frame: rigidly follow the parent bone's ANIMATED
// world transform = mul(bind[b], skin[b]) (row-vector convention; = bind[b] at bind pose, so this also
// restores the rest placement when playback stops). Rigid follow only — the attachment's own cloth
// sim/flutter is not simulated. Small meshes, so a per-frame re-bake + glBufferSubData is cheap.
void GLModelWidget::reskinAttachments(const QVector<RigMath::Mat4>* skin)
{
    if (m_attachGpu.isEmpty()) return;
    QVector<float> vb;
    for (int i = 0; i < m_attachGpu.size() && i < m_attach.size(); ++i) {
        AttachGpu& g = m_attachGpu[i];
        const int b = g.boneIndex;
        RigMath::Mat4 T; for (int k = 0; k < 16; ++k) T[k] = (k % 5 == 0) ? 1.0f : 0.0f;
        if (b >= 0 && b < m_skel.bones.size()) {
            T = m_skel.bones[b].bind;
            if (skin && b < skin->size()) T = RigMath::mul(m_skel.bones[b].bind, (*skin)[b]);
        }
        bakeAttachmentVerts(m_attach[i].geo, T, vb);
        glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, vb.size() * 4, vb.constData());
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

QMatrix4x4 GLModelWidget::viewMatrix() const
{
    QVector3D eye = m_target + QVector3D(std::cos(m_pitch)*std::sin(m_yaw), std::sin(m_pitch), std::cos(m_pitch)*std::cos(m_yaw)) * m_dist;
    QMatrix4x4 v; v.lookAt(eye, m_target, QVector3D(0, 1, 0)); return v;
}
QMatrix4x4 GLModelWidget::projMatrix() const
{
    QMatrix4x4 p; const float a = height() > 0 ? float(width())/height() : 1.0f;
    p.perspective(45.0f, a, qMax(0.001f, m_radius*0.02f), m_radius*40.0f + 10.0f); return p;
}

void GLModelWidget::resizeGL(int w, int h) { glViewport(0, 0, w, h); }

void GLModelWidget::paintGL()
{
    if (m_glBroken) return;
    if (m_pendingUpload) { rebuildBuffers(); m_frameDirty = (m_clip >= 0); }
    if (m_pendingTex) uploadTextures();
    if (m_playing) advancePlayback();
    if (m_frameDirty) uploadSkinnedFrame();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const QMatrix4x4 mvp = projMatrix() * viewMatrix();

    // Overlays behind the master gate (§3.3).
    if (m_overlaysOn && m_showGrid && m_gridVerts) {
        glUseProgram(m_lineProg);
        glUniformMatrix4fv(glGetUniformLocation(m_lineProg, "uMVP"), 1, GL_FALSE, mvp.constData());
        glBindVertexArray(m_gridVao); glDrawArrays(GL_LINES, 0, m_gridVerts); glBindVertexArray(0);
    }

    drawModel(mvp);

    if (m_overlaysOn && m_showSkeleton && m_boneVerts) {
        glDisable(GL_DEPTH_TEST);
        glUseProgram(m_lineProg);
        glUniformMatrix4fv(glGetUniformLocation(m_lineProg, "uMVP"), 1, GL_FALSE, mvp.constData());
        glBindVertexArray(m_boneVao); glDrawArrays(GL_LINES, 0, m_boneVerts); glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);
    }

    // Keep the frame loop alive while playing (QOpenGLWidget only redraws on demand).
    if (m_playing) update();
}

// Draw the model itself: pass 1 opaque+mask, pass 2 transparent (blend/additive), with the current
// shading / channel / selection state, given the composed mvp. Assumes the target framebuffer is
// bound and cleared, the viewport is set, and depth-test is enabled. Shared by paintGL (on screen)
// and renderToImage (offscreen export) so the two render paths never drift (template §5 — one
// viewport, one draw path). Deliberately draws ONLY the asset — no grid or skeleton overlay — so an
// exported image is the model alone, not the workspace chrome (template §15).
void GLModelWidget::drawModel(const QMatrix4x4& mvp)
{
    if (m_glBroken || m_geo.isEmpty()) return;
    QMatrix4x4 model;   // identity (already Y-up)
    {
        glUseProgram(m_prog);
        glUniformMatrix4fv(glGetUniformLocation(m_prog, "uMVP"), 1, GL_FALSE, mvp.constData());
        glUniformMatrix4fv(glGetUniformLocation(m_prog, "uModel"), 1, GL_FALSE, model.constData());
        glUniform3f(glGetUniformLocation(m_prog, "uSel"), 0.20f, 0.55f, 1.0f);
        glUniform1i(glGetUniformLocation(m_prog, "uShading"), int(m_shading));
        glUniform1i(glGetUniformLocation(m_prog, "uChannel"), int(m_channel));
        // Camera eye (matches viewMatrix()): the specular/Fresnel view vector.
        const QVector3D eye = m_target + QVector3D(std::cos(m_pitch)*std::sin(m_yaw), std::sin(m_pitch), std::cos(m_pitch)*std::cos(m_yaw)) * m_dist;
        glUniform3f(glGetUniformLocation(m_prog, "uCam"), eye.x(), eye.y(), eye.z());
        // Fixed sampler units.
        glUniform1i(glGetUniformLocation(m_prog, "uBase"), 0);
        glUniform1i(glGetUniformLocation(m_prog, "uNormal"), 1);
        glUniform1i(glGetUniformLocation(m_prog, "uMR"), 2);
        glUniform1i(glGetUniformLocation(m_prog, "uEmis"), 3);
        glUniform1i(glGetUniformLocation(m_prog, "uSpecColor"), 4);
        glUniform1i(glGetUniformLocation(m_prog, "uFurNoise"), 5);
        glUniform1i(glGetUniformLocation(m_prog, "uFurMask"), 6);
        const int locIsFur = glGetUniformLocation(m_prog, "uIsFur");
        const int locHasFurNoise = glGetUniformLocation(m_prog, "uHasFurNoise");
        const int locHasFurMask = glGetUniformLocation(m_prog, "uHasFurMask");
        const int locHasBase = glGetUniformLocation(m_prog, "uHasBase");
        const int locHasNormal = glGetUniformLocation(m_prog, "uHasNormal");
        const int locHasMR = glGetUniformLocation(m_prog, "uHasMR");
        const int locHasEmis = glGetUniformLocation(m_prog, "uHasEmis");
        const int locHasSpecColor = glGetUniformLocation(m_prog, "uHasSpecColor");
        const int locSelected = glGetUniformLocation(m_prog, "uSelected");
        const int locSSS = glGetUniformLocation(m_prog, "uSSS");
        const int locTransl = glGetUniformLocation(m_prog, "uTransl");
        const int locAlphaMode = glGetUniformLocation(m_prog, "uAlphaMode");
        const bool wire = m_shading == Wireframe;
        if (wire) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        // Draw groups: group −1 is the body (its VAO + materials + parts); groups ≥0 are visible
        // attachments, each with its own VAO/materials/parts already baked into the body's frame. The
        // shading/alpha passes below iterate every group so attachments composite with the body.
        auto matsOf  = [&](int gi) -> const QVector<GpuMat>&  { return gi < 0 ? m_gpuMat   : m_attachGpu[gi].mats;  };
        auto partsOf = [&](int gi) -> const QVector<GpuPart>& { return gi < 0 ? m_gpuParts : m_attachGpu[gi].parts; };
        auto bindGroup = [&](int gi) { if (gi < 0) m_vao.bind(); else glBindVertexArray(m_attachGpu[gi].vao); };
        auto matPtr = [&](const QVector<GpuMat>& mats, const GpuPart& gp) -> const GpuMat* {
            return (gp.materialIndex >= 0 && gp.materialIndex < mats.size()) ? &mats[gp.materialIndex] : nullptr; };
        // Wireframe forces every part opaque; otherwise Blend(2)/Additive(3) are the transparent pass.
        auto passOf = [&](const QVector<GpuMat>& mats, const GpuPart& gp) -> int {
            if (wire) return 0; const GpuMat* gm = matPtr(mats, gp);
            return (gm && (gm->alphaMode == 2 || gm->alphaMode == 3)) ? 1 : 0; };
        auto drawPart = [&](const QVector<GpuMat>& mats, const GpuPart& gp) {
            const GpuMat* gm = matPtr(mats, gp);
            const bool useTex = !wire;
            const bool hasBase   = useTex && gm && gm->base;
            const bool hasNormal = useTex && gm && gm->normal;
            const bool hasMR     = useTex && gm && gm->mr;
            const bool hasEmis   = useTex && gm && gm->emissive;
            const bool hasSpecColor = useTex && gm && gm->specColor;
            glUniform1i(locHasBase,   hasBase   ? 1 : 0);
            glUniform1i(locHasNormal, hasNormal ? 1 : 0);
            glUniform1i(locHasMR,     hasMR     ? 1 : 0);
            glUniform1i(locHasEmis,   hasEmis   ? 1 : 0);
            glUniform1i(locHasSpecColor, hasSpecColor ? 1 : 0);
            glUniform1i(locAlphaMode, (useTex && gm) ? gm->alphaMode : 0);
            if (hasBase)   { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, gm->base); }
            if (hasNormal) { glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, gm->normal); }
            if (hasMR)     { glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, gm->mr); }
            if (hasEmis)   { glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, gm->emissive); }
            if (hasSpecColor) { glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, gm->specColor); }
            // FurV2 shell-fur preview: bind the strand noise + fur-length mask and flag the fur path.
            const bool isFur = useTex && gm && gm->isFur;
            glUniform1i(locIsFur, isFur ? 1 : 0);
            glUniform1i(locHasFurNoise, (isFur && gm->furNoise) ? 1 : 0);
            glUniform1i(locHasFurMask,  (isFur && gm->furMask)  ? 1 : 0);
            if (isFur && gm->furNoise) { glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, gm->furNoise); }
            if (isFur && gm->furMask)  { glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D, gm->furMask); }
            if (useTex && gm) { glUniform3f(locSSS, gm->subsurface[0], gm->subsurface[1], gm->subsurface[2]); glUniform1f(locTransl, gm->translucent); }
            else              { glUniform3f(locSSS, 0.f, 0.f, 0.f); glUniform1f(locTransl, 0.f); }
            glUniform1i(locSelected, gp.part >= 0 && m_selected.contains(gp.part) ? 1 : 0);
            glDrawElements(GL_TRIANGLES, gp.count, GL_UNSIGNED_INT, (void*)(size_t(gp.first) * 4));
        };

        QVector<int> groups; groups << -1;
        for (int i = 0; i < m_attachGpu.size(); ++i) if (m_attachGpu[i].visible) groups << i;

        // A part hidden in the viewport (Isolate/Hide) is skipped in both passes. Only the body group
        // (gi < 0) has real part indices; attachment parts carry part = -1 and are never hidden here.
        auto hidden = [&](int gi, const GpuPart& gp) { return gi < 0 && m_hidden.contains(gp.part); };

        // Assert a known render state before the opaque pass so nothing leaked from a prior draw — an
        // overlay, the previous frame's transparent pass, or an equipped additive/effect piece — can
        // bleed the whole model into transparency (blend left on) or make it vanish (depth-writes off).
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND);

        // Pass 1 — opaque + mask (all groups).
        for (int gi : groups) { bindGroup(gi); const auto& mats = matsOf(gi);
            for (const GpuPart& gp : partsOf(gi)) if (gp.count && !hidden(gi, gp) && passOf(mats, gp) == 0) drawPart(mats, gp); }
        // Pass 2 — transparent (blend/additive): keep depth, stop writing it, composite. Additive uses
        // SRC_ALPHA,ONE; blend uses alpha-over. Not depth-sorted among themselves (a solid first cut).
        if (!wire) {
            glEnable(GL_BLEND); glDepthMask(GL_FALSE);
            for (int gi : groups) { bindGroup(gi); const auto& mats = matsOf(gi);
                for (const GpuPart& gp : partsOf(gi)) {
                    if (!gp.count || hidden(gi, gp) || passOf(mats, gp) != 1) continue;
                    const GpuMat* gm = matPtr(mats, gp);
                    if (gm && gm->alphaMode == 3) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                    else                          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    drawPart(mats, gp);
                } }
            glDepthMask(GL_TRUE); glDisable(GL_BLEND);
        }
        glBindVertexArray(0);
        if (wire) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
}

// Render the model offscreen to an image (template §15 image/icon export). scalePercent RE-RENDERS at
// a larger framebuffer (true supersampling — never an upscale of the on-screen pixels), so 200% is a
// genuinely sharper image. transparentBg clears the colour buffer to alpha 0 so the asset drops onto a
// transparent PNG; otherwise the viewport backdrop is kept. cropToModel trims fully-transparent margins
// (only meaningful with transparentBg). Draws the asset alone — no grid or skeleton overlay. Returns a
// null QImage when there is nothing to render or GL is unavailable (headless with no context).
QImage GLModelWidget::renderToImage(int scalePercent, bool transparentBg, bool cropToModel)
{
    if (m_glBroken || m_geo.isEmpty()) return QImage();
    makeCurrent();
    // Make sure geometry / textures / the current animation frame are up to date before we read back
    // (renderToImage can be invoked before the widget has painted, e.g. straight after loading).
    if (m_pendingUpload) { rebuildBuffers(); m_frameDirty = (m_clip >= 0); }
    if (m_pendingTex)   uploadTextures();
    if (m_frameDirty)   uploadSkinnedFrame();

    const qreal dpr = devicePixelRatioF();
    const double s = qBound(25, scalePercent, 400) / 100.0;
    const int w = qMax(1, int(width()  * dpr * s));
    const int h = qMax(1, int(height() * dpr * s));

    QOpenGLFramebufferObjectFormat fmt;
    fmt.setAttachment(QOpenGLFramebufferObject::Depth);
    fmt.setSamples(4);   // MSAA → clean silhouette edge; resolved by toImage()
    QOpenGLFramebufferObject fbo(w, h, fmt);
    // A very large scale on a hi-dpi widget can ask for an FBO the driver can't allocate; drawing into
    // an incomplete FBO wastes the whole render and yields garbage. Bail cleanly instead.
    if (!fbo.isValid()) return QImage();
    fbo.bind();
    glViewport(0, 0, w, h);
    if (transparentBg) glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    else               glClearColor(0.13f, 0.13f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    // Aspect is preserved (proj uses width()/height(), the frame just has more pixels), so the framing
    // matches what is on screen.
    drawModel(projMatrix() * viewMatrix());
    fbo.release();

    QImage img = fbo.toImage();   // resolves MSAA; ARGB32_Premultiplied, already y-flipped

    // Restore the on-screen viewport/clear state (mirrors pickPart; the next paintGL rebinds the
    // widget's own framebuffer through QOpenGLWidget).
    glClearColor(0.13f, 0.13f, 0.15f, 1.0f);
    glViewport(0, 0, int(width() * dpr), int(height() * dpr));

    if (img.isNull()) return img;
    if (transparentBg) {
        img = img.convertToFormat(QImage::Format_ARGB32);   // un-premultiply for a clean PNG alpha
        if (cropToModel) img = cropTransparent(img);
    } else {
        img = img.convertToFormat(QImage::Format_RGB32);    // opaque; drop the alpha channel
    }
    return img;
}

int GLModelWidget::pickPart(const QPoint& pos)
{
    if (m_glBroken || m_geo.isEmpty()) return -1;
    makeCurrent();
    const qreal dpr = devicePixelRatioF();
    QOpenGLFramebufferObject fbo(int(width()*dpr), int(height()*dpr), QOpenGLFramebufferObject::Depth);
    if (!fbo.isValid()) return -1;
    fbo.bind();
    glViewport(0, 0, fbo.width(), fbo.height());
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    const QMatrix4x4 mvp = projMatrix() * viewMatrix();
    glUseProgram(m_pickProg);
    glUniformMatrix4fv(glGetUniformLocation(m_pickProg, "uMVP"), 1, GL_FALSE, mvp.constData());
    m_vao.bind();
    for (const GpuPart& gp : m_gpuParts) {
        if (gp.count == 0 || m_hidden.contains(gp.part)) continue;   // a hidden part is unpickable
        const int id = gp.part + 1;   // 0 = background
        glUniform3f(glGetUniformLocation(m_pickProg, "uId"), ((id) & 0xFF)/255.f, ((id>>8) & 0xFF)/255.f, ((id>>16) & 0xFF)/255.f);
        glDrawElements(GL_TRIANGLES, gp.count, GL_UNSIGNED_INT, (void*)(size_t(gp.first)*4));
    }
    m_vao.release();
    unsigned char px[4] = {0,0,0,0};
    glReadPixels(int(pos.x()*dpr), int((height()-pos.y())*dpr), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    fbo.release();
    glClearColor(0.13f, 0.13f, 0.15f, 1.0f);
    glViewport(0, 0, int(width()*dpr), int(height()*dpr));
    const int id = px[0] | (px[1] << 8) | (px[2] << 16);
    return id == 0 ? -1 : id - 1;
}

void GLModelWidget::mousePressEvent(QMouseEvent* e)
{
    m_lastPos = m_pressPos = e->pos(); m_dragButton = e->button();
    m_swallowNextRelease = false; m_dragging = false;
}

void GLModelWidget::mouseMoveEvent(QMouseEvent* e)
{
    const QPoint d = e->pos() - m_lastPos; m_lastPos = e->pos();
    // Latch "dragging" once the press has moved past a small threshold, then keep orbiting for the
    // rest of the gesture regardless of per-event delta. (The old per-event manhattanLength()>2 gate
    // dropped out on slow drags — the "loses traction" bug.)
    if (m_dragButton == Qt::LeftButton && !(e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
        if (!m_dragging && (e->pos() - m_pressPos).manhattanLength() > 3) m_dragging = true;
        if (m_dragging || (e->modifiers() & Qt::AltModifier)) {
            m_yaw -= d.x() * 0.01f; m_pitch = qBound(-1.55f, m_pitch + d.y() * 0.01f, 1.55f); update();
        }
    } else if (m_dragButton == Qt::MiddleButton || (m_dragButton == Qt::RightButton && (e->modifiers() & Qt::AltModifier))) {
        // pan
        QVector3D right(std::cos(m_yaw), 0, -std::sin(m_yaw));
        QVector3D up(0, 1, 0);
        m_target -= right * (d.x() * m_dist * 0.0015f);
        m_target += up * (d.y() * m_dist * 0.0015f);
        update();
    }
}

void GLModelWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_swallowNextRelease) { m_swallowNextRelease = false; m_dragButton = Qt::NoButton; m_dragging = false; return; }
    // A click (left button, no drag) selects; a drag was an orbit and must NOT select.
    if (e->button() == Qt::LeftButton && m_dragButton == Qt::LeftButton && !m_dragging) {
        const int part = pickPart(e->pos());
        if (e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) {
            if (part >= 0) { if (m_selected.contains(part)) m_selected.remove(part); else m_selected.insert(part); }
        } else {
            m_selected.clear();
            if (part >= 0) m_selected.insert(part);
        }
        update(); emit selectionChanged(m_selected);
    }
    // Right-click (no drag, no Alt-pan): §11 scoping — clicking a part OUTSIDE the current selection
    // replaces the selection with it; clicking INSIDE keeps the whole selection, so the menu can act on
    // all of it. Empty space leaves the selection alone. Then raise the viewport part menu.
    else if (e->button() == Qt::RightButton && m_dragButton == Qt::RightButton && !m_dragging
             && !(e->modifiers() & Qt::AltModifier)) {
        const int part = pickPart(e->pos());
        if (part >= 0 && !m_selected.contains(part)) {
            m_selected.clear();
            m_selected.insert(part);
            update(); emit selectionChanged(m_selected);
        }
        emit viewportPartMenuRequested(e->globalPosition().toPoint());
    }
    m_dragButton = Qt::NoButton; m_dragging = false;
}

void GLModelWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    m_swallowNextRelease = true;   // Qt delivers press→release→dblclick→release (§11 scar 1)
    const int part = pickPart(e->pos());
    if (part >= 0) { emit partDoubleClicked(part); }
    else frameAll();
}

void GLModelWidget::wheelEvent(QWheelEvent* e)
{
    m_dist = qBound(m_radius*0.05f, m_dist * (e->angleDelta().y() > 0 ? 1/1.15f : 1.15f), m_radius*40.0f);
    update(); e->accept();
}

void GLModelWidget::frameAll()
{
    m_target = modelCenter();
    m_dist = m_radius * 2.6f; update();
}

// The model's geometric centre (bbox centre, Y-up) — the pivot a turntable should orbit so the object
// spins in place rather than swinging, regardless of any pan the user applied.
QVector3D GLModelWidget::modelCenter() const
{
    QVector3D lo(m_geo.bboxMin[0], m_geo.bboxMin[1], m_geo.bboxMin[2]);
    QVector3D hi(m_geo.bboxMax[0], m_geo.bboxMax[1], m_geo.bboxMax[2]);
    return (toYUp(lo.x(),lo.y(),lo.z()) + toYUp(hi.x(),hi.y(),hi.z())) * 0.5f;
}
QVector3D GLModelWidget::orbitTarget() const { return m_target; }
void GLModelWidget::setOrbitTarget(const QVector3D& t) { m_target = t; update(); }

void GLModelWidget::frameSelected()
{
    if (m_selected.isEmpty()) { frameAll(); return; }
    QVector3D lo(1e30f,1e30f,1e30f), hi(-1e30f,-1e30f,-1e30f);
    for (int part : m_selected) {
        if (part < 0 || part >= m_geo.parts.size()) continue;
        const MeshPart& mp = m_geo.parts[part];
        for (uint32_t k = mp.indexStart; k < mp.indexStart + mp.indexCount; ++k) {
            const MeshVertex& v = m_geo.vertices[m_geo.indices[k]];
            QVector3D p = toYUp(v.px, v.py, v.pz);
            lo.setX(qMin(lo.x(),p.x())); lo.setY(qMin(lo.y(),p.y())); lo.setZ(qMin(lo.z(),p.z()));
            hi.setX(qMax(hi.x(),p.x())); hi.setY(qMax(hi.y(),p.y())); hi.setZ(qMax(hi.z(),p.z()));
        }
    }
    m_target = (lo + hi) * 0.5f;
    m_dist = qMax(0.01f, (hi - lo).length()) * 1.4f; update();
}

QString GLModelWidget::partName(int index) const
{
    return (index >= 0 && index < m_geo.parts.size()) ? m_geo.parts[index].name : QString();
}

void GLModelWidget::setHiddenParts(const QSet<int>& hidden)
{
    if (m_hidden == hidden) return;
    m_hidden = hidden;
    // A hidden part can't stay selected-and-picked meaningfully, but keep the selection as-is: the
    // caller (Isolate) usually wants the isolated parts to remain selected. Just repaint.
    update();
}

void GLModelWidget::isolateParts(const QSet<int>& keepVisible)
{
    QSet<int> hide;
    for (int i = 0; i < m_geo.parts.size(); ++i) if (!keepVisible.contains(i)) hide.insert(i);
    setHiddenParts(hide);
}

void GLModelWidget::clearHiddenParts() { setHiddenParts({}); }

void GLModelWidget::emitStats()
{
    if (m_geo.isEmpty()) { emit statsText(QString()); return; }
    emit statsText(QStringLiteral("%1 parts · %2 verts · %3 tris")
        .arg(m_geo.parts.size()).arg(m_geo.vertices.size()).arg(m_geo.triangleCount()));
}
