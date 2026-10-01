#pragma once
// Renders a model's geometry to a small flat-shaded thumbnail, entirely offscreen (its own
// QOffscreenSurface + QOpenGLContext), so the grid view can show recognizable model icons without
// disturbing the main viewport. Deliberately FLAT (grey, normal-lit) and texture-free: the point of a
// thumbnail is the silhouette/shape, and skipping the material decode makes each render cheap enough to
// fill a grid progressively. GUI-thread only (a QOpenGLContext is not shared across threads), so the
// thumbnail cache that drives it runs in its throttled GUI mode.
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QVector>
#include <cstdint>

struct ModelGeometry;
class QOpenGLFunctions_3_3_Core;

class ModelThumbnailRenderer {
public:
    explicit ModelThumbnailRenderer(int size = 128);
    ~ModelThumbnailRenderer();

    // Render `geo` to a `size`×`size` RGBA image (transparent background). Null image on any failure
    // (no GL, empty geometry) — the cache treats that as "no icon" and stops retrying.
    //
    // baseColors (optional): one base-colour image per material index. When supplied, parts are drawn
    // textured with their material's base colour; otherwise the mesh is flat-shaded grey (faster, no
    // texture decode). Pass nullptr / empty for the flat path.
    QImage render(const ModelGeometry& geo, const QVector<QImage>* baseColors = nullptr);

    bool ok() const { return m_ok; }

private:
    bool ensureGl();
    int  m_size;
    bool m_ok = false;
    bool m_init = false;
    QOffscreenSurface m_surface;
    QOpenGLContext m_ctx;
    QOpenGLFunctions_3_3_Core* m_gl = nullptr;
    unsigned m_prog = 0, m_fbo = 0, m_col = 0, m_dep = 0, m_vao = 0, m_vbo = 0, m_ibo = 0;
    int m_uMVP = -1, m_uModel = -1, m_uHasTex = -1, m_uTex = -1;
};
