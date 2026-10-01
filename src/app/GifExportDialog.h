#pragma once
#include "app/ExportCapture.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QSpinBox;
class QLabel;

// Options for a GIF export (turntable or animation-loop), shown at save time and REMEMBERED to
// QSettings (gif/* keys) — the same pattern as ImageExportDialog. One dialog serves both GIF kinds;
// setMode() adjusts the wording and hides turntable-only controls for an animation loop (whose frame
// count comes from the clip, not the user).
class GifExportDialog : public QDialog {
    Q_OBJECT
public:
    enum Mode { Turntable, AnimLoop };
    explicit GifExportDialog(Mode mode, QWidget* parent = nullptr);

    ExportCapture::GifOptions options() const;   // the chosen options, also saved back to QSettings

private:
    void load();
    void save() const;

    Mode m_mode;
    QSpinBox*  m_fps = nullptr;
    QSpinBox*  m_frames = nullptr;     // turntable only
    QComboBox* m_scale = nullptr;      // 25/50/75/100
    QSpinBox*  m_colors = nullptr;     // 2..256
    QCheckBox* m_dither = nullptr;
    QCheckBox* m_transparent = nullptr;
    QCheckBox* m_crop = nullptr;
    QCheckBox* m_optimize = nullptr;
    QSpinBox*  m_targetMB = nullptr;
};
