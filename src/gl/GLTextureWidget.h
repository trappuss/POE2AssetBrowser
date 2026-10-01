#pragma once
#include <QImage>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>
#include <QPoint>
#include <QString>

// Displays a decoded RGBA texture aspect-fit, with channel isolation (RGB/R/G/B/A), scroll-zoom,
// drag-pan, double-click reset and an alpha checkerboard — the Textures-tab viewport (template
// §13). PoE2 textures are CPU-decoded to RGBA8 by tex/DdsImage (bcdec), so this widget takes a
// QImage rather than a compressed payload; that keeps it engine-agnostic.
class GLTextureWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit GLTextureWidget(QWidget* parent = nullptr);
    ~GLTextureWidget() override;

    void setImage(const QImage& img);        // RGBA; null clears
    void clearImage();
    enum Channel { RGB = 0, R, G, B, A };
    void setChannel(Channel c);
    void setCheckerboard(bool on);
    void resetView();
    bool hasImage() const { return !m_image.isNull(); }

signals:
    void hoverPixel(QPoint px, QColor rgba);   // (-1,-1) when outside

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    void uploadPending();
    bool widgetToPixel(const QPoint& p, QPoint& px) const;

    QImage  m_image;
    bool    m_glBroken = false;
    bool    m_pendingUpload = false;
    GLuint  m_prog = 0, m_vao = 0, m_vbo = 0, m_tex = 0;
    int     m_texW = 0, m_texH = 0;
    Channel m_channel = RGB;
    bool    m_checker = false;
    float   m_zoom = 1.0f, m_panX = 0, m_panY = 0;
    bool    m_dragging = false;
    QPoint  m_lastPos;
};
