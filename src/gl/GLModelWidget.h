#pragma once
#include "model/ModelGeometry.h"
#include "model/AstSkeleton.h"
#include "model/RigMath.h"
#include <QElapsedTimer>
#include <QImage>
#include <QStringList>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QMatrix4x4>
#include <QPoint>
#include <QSet>
#include <QVector>
#include <QVector3D>
#include <memory>

// The ONE shared 3D viewport (template §5): every tab that draws a model uses this. Features land
// once. Covers tier 1–2: flat / shaded / wireframe shading, orbit-pan-zoom, part selection as a
// set (§11), a base-colour material preview, a channel viewer (§10), and overlays (grid, skeleton,
// stats) behind a single per-widget master gate (§3.3).
class GLModelWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit GLModelWidget(QWidget* parent = nullptr);
    ~GLModelWidget() override;

    enum Shading { Flat = 0, Shaded, Wireframe };
    enum Channel { BaseColor = 0, Normal, Roughness, Metallic, AO, Emissive, None };

    // Load geometry (and optional skeleton for the bone overlay). Per-part base-colour images may
    // be supplied later via setMaterialTextures(); pass empty for flat colour.
    void setModel(const ModelGeometry& geo, const AstSkeleton::Skeleton& skeleton = {});

    // Per-material decoded textures for PBR shading (docs/FORMATS.md §6.1). Any image may be null.
    // normal holds the tangent-space normal in RGB; metalRough is glTF-packed (G roughness, B metal).
    struct MaterialTextures { QImage baseColor, normal, metalRough, emissive, specColor; float subsurface[3] = {0,0,0}; float translucent = 0.0f; int alphaMode = 0;
        bool isFur = false; QImage furNoise, furMask; float furDepth = 0.0f; };   // FurV2 shell-fur preview
    void setMaterialTextures(const QVector<MaterialTextures>& byMaterial);

    // ── Attachment assembly ─────────────────────────────────────────────────────────────────────
    // Extra meshes placed on named bones of THIS model's skeleton (coat→hip, feathers→head), drawn in
    // the same shaded/alpha passes as the body. Each carries its own geometry + materials + the parent
    // bone name; the widget bakes it to that bone's bind-pose transform in the body's frame. Set after
    // the body (its skeleton must be loaded). Placement is bind-pose (attachments don't follow body
    // animation yet — they'd need per-frame re-baking + their own cloth sim).
    struct Attachment { ModelGeometry geo; QVector<MaterialTextures> mats; QString bone; QString label; bool visible = true; };
    void setAttachments(const QVector<Attachment>& atts);
    void setAttachmentVisible(int index, bool on);
    void clearAttachments();
    int  attachmentCount() const { return m_attach.size(); }
    // Back-compat: base colour only.
    void setPartTextures(const QVector<QImage>& baseColorByMaterial);
    void clearModel();

    void setShading(Shading s);
    void setChannel(Channel c);

    // Overlay master gate (§3.3): all overlay state flows through reapplyOverlays().
    void setOverlaysOn(bool on);
    void setShowGrid(bool on);
    void setShowSkeleton(bool on);
    void setShowStats(bool on);
    bool overlaysOn() const { return m_overlaysOn; }

    // Part selection is a set (§11).
    const QSet<int>& selectedParts() const { return m_selected; }
    void setSelectedParts(const QSet<int>& parts);
    void frameAll();
    void frameSelected();

    // Part identity + per-part visibility (§11 viewport context menu). Hiding is a viewport-only
    // aid (it never affects export, which reads the selection): "Isolate" hides everything except a
    // set, "Show all parts" clears it. Hidden parts also can't be picked.
    int     partCount() const { return m_geo.parts.size(); }
    QString partName(int index) const;
    const QSet<int>& hiddenParts() const { return m_hidden; }
    void setHiddenParts(const QSet<int>& hidden);
    void isolateParts(const QSet<int>& keepVisible);   // hide every part not in keepVisible
    void clearHiddenParts();

    // Render the current view offscreen to an image (template §15 image/icon export). scalePercent
    // (25–400) RE-RENDERS at a larger framebuffer — true supersampling, not an upscale. transparentBg
    // clears to alpha 0 for a transparent PNG; cropToModel trims transparent margins (needs
    // transparentBg). The asset is drawn alone — no grid/skeleton overlay. Null QImage if nothing to
    // render or GL is unavailable.
    QImage renderToImage(int scalePercent, bool transparentBg, bool cropToModel);

    // Orbit yaw in radians — read/write the horizontal camera angle. Used by the turntable capture to
    // spin the camera a whole revolution while rendering frames; setting it repaints.
    float orbitYaw() const { return m_yaw; }
    void  setOrbitYaw(float yaw) { m_yaw = yaw; update(); }
    QVector3D modelCenter() const;            // bbox centre (Y-up) — the turntable pivot
    QVector3D orbitTarget() const;            // current camera look-at target (may be panned)
    void setOrbitTarget(const QVector3D& t);  // move the look-at target (used to recenter for a turntable)

    // Animation playback (CPU skinning). Clips come from the loaded skeleton; index −1 is the bind
    // pose. Time is seconds into the clip; playback loops. All no-ops when the mesh is not skinned.
    // Playback is offered ONLY when the skeleton actually covers the mesh's joint palette
    // (skeletonMatchesMesh()); a body-armour piece skinned to a character rig it does not carry
    // (docs/FORMATS.md §7) reports false and is shown at bind pose, never animated against a
    // mismatched rig.
    bool skeletonMatchesMesh() const { return m_skelMatchesMesh; }
    bool isSkinnedMesh() const { return m_geo.skinned && !m_skel.bones.isEmpty(); }
    QStringList clipNames() const;
    int  clipCount() const { return m_skelMatchesMesh ? m_skel.clips.size() : 0; }
    void setClip(int index);                      // −1 = bind pose (static)
    // Replace the current skeleton's clip list in place (bones untouched), then reset to the bind
    // pose — used to drop in a player-animation-library clip retargeted onto this mesh's rig without
    // re-uploading geometry. Call setClip() afterwards to play one. clipNames()/clipCount() follow.
    void setClips(const QVector<AstSkeleton::Clip>& clips);
    int  currentClip() const { return m_clip; }
    void setPlaying(bool on);
    bool isPlaying() const { return m_playing; }
    void setAnimTime(float seconds);              // scrub; pauses implicitly is the caller's choice
    float animTime() const { return m_time; }
    float clipDuration() const { return m_clipDur; }
    // The current clip's authored frame rate (0 if no clip). With clipDuration() this gives the clip's
    // native frame count (round(dur·fps)) — the GIF exporters sample the clip at its OWN frames/rate so
    // an animation-loop is exact and plays at authored speed (matches D4's animFrameRate/animFrameCount).
    int   currentClipFps() const { return (m_clip >= 0 && m_clip < m_skel.clips.size()) ? m_skel.clips[m_clip].fps : 0; }

signals:
    void selectionChanged(const QSet<int>& parts);
    void partDoubleClicked(int part);            // frame only, no selection change
    void statsText(const QString& text);          // tri/vert counts for the status bar
    void animTimeChanged(float seconds, float duration);   // drives the timeline UI
    void viewportPartMenuRequested(const QPoint& globalPos);   // right-click in the viewport (§11)

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    struct GpuPart { int first = 0; int count = 0; int materialIndex = -1; int part = -1; };
    void rebuildBuffers();
    void drawModel(const QMatrix4x4& mvp);        // asset-only two-pass draw, shared by paintGL + renderToImage
    void buildGrid();                             // (re)build the ground grid sized to the model
    void uploadTextures();
    void fillVertexBuffer(QVector<float>& out, const QVector<RigMath::Mat4>* skin) const;
    void uploadSkinnedFrame();                    // recompute + re-upload the VBO for m_clip/m_time
    void advancePlayback();                       // step time from the clock, loop, request redraw
    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 projMatrix() const;
    int pickPart(const QPoint& pos);
    void emitStats();

    ModelGeometry m_geo;
    AstSkeleton::Skeleton m_skel;
    QVector<GpuPart> m_gpuParts;
    QVector<MaterialTextures> m_mat;      // per materialIndex
    struct GpuMat { GLuint base = 0, normal = 0, mr = 0, emissive = 0, specColor = 0; float subsurface[3] = {0,0,0}; float translucent = 0.0f; int alphaMode = 0;
        bool isFur = false; GLuint furNoise = 0, furMask = 0; float furDepth = 0.0f; };
    QVector<GpuMat> m_gpuMat;

    // Attachment GPU state — one self-contained buffer set per attachment (vertices pre-baked into the
    // body's Y-up frame at the parent bone), drawn after the body through the shared material passes.
    struct AttachGpu { GLuint vao = 0, vbo = 0, ibo = 0; QVector<GpuPart> parts; QVector<GpuMat> mats; bool visible = true; int boneIndex = -1; int vertCount = 0; };
    QVector<Attachment> m_attach;         // source data (kept so a skeleton reload / anim frame can re-bake)
    QVector<AttachGpu>  m_attachGpu;
    bool m_attachDirty = false;
    void rebuildAttachments();            // (re)bake + upload m_attach → m_attachGpu against m_skel
    void reskinAttachments(const QVector<RigMath::Mat4>* skin);  // re-place attachments for the current anim frame (rigid follow)
    void bakeAttachmentVerts(const ModelGeometry& geo, const RigMath::Mat4& xform, QVector<float>& out) const;
    void releaseAttachmentGpu();

    bool m_glBroken = false;   // set when the GL context can't be created (headless/no-GPU); draw nothing
    bool m_pendingUpload = false, m_pendingTex = false;
    GLuint m_prog = 0, m_pickProg = 0, m_lineProg = 0;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo{QOpenGLBuffer::VertexBuffer}, m_ibo{QOpenGLBuffer::IndexBuffer};
    GLuint m_gridVao = 0, m_gridVbo = 0; int m_gridVerts = 0;
    GLuint m_boneVao = 0, m_boneVbo = 0; int m_boneVerts = 0;

    Shading m_shading = Shaded;
    Channel m_channel = BaseColor;
    bool m_overlaysOn = true, m_showGrid = true, m_showSkeleton = false, m_showStats = false;

    // Camera (orbit).
    float m_yaw = 0.6f, m_pitch = 0.4f, m_dist = 3.0f;
    QVector3D m_target{0, 0, 0};
    float m_radius = 1.0f;
    QPoint m_lastPos, m_pressPos; Qt::MouseButton m_dragButton = Qt::NoButton;
    bool m_swallowNextRelease = false;
    bool m_dragging = false;   // latched true once a press has moved past the drag threshold

    QSet<int> m_selected;
    QSet<int> m_hidden;      // parts hidden in the viewport (Isolate/Hide); never affects export

    // Animation.
    int   m_clip = -1;
    float m_time = 0.0f, m_clipDur = 0.0f;
    bool  m_playing = false;
    bool  m_skelMatchesMesh = false;   // skeleton covers the mesh's joint palette → safe to animate
    bool  m_frameDirty = false;     // the VBO needs a skinned re-fill
    qint64 m_lastTickNs = 0;        // for wall-clock playback advance
    QElapsedTimer m_clock;
};
