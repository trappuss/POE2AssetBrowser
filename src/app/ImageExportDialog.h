#pragma once
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;

// Options for "Save preview image…" (template §15 image/icon export), shown at save time and REMEMBERED
// back to Config so the next save defaults to the last choice — the same pattern as ExportOptionsDialog.
// Scale RE-RENDERS the viewport at a larger framebuffer (true supersampling, not an upscale), so 200 %
// is genuinely sharper. Transparent background drops the asset onto a transparent PNG; crop trims the
// transparent margin (only meaningful with a transparent background, so it is disabled otherwise).
class ImageExportDialog : public QDialog {
    Q_OBJECT
public:
    explicit ImageExportDialog(QWidget* parent = nullptr);

    int  scalePercent() const;    // 25 · 50 · 100 · 200 · 400 (the offered ladder)
    bool transparentBg() const;
    bool cropToModel() const;

private:
    void load();
    void save() const;

    QComboBox* m_scale = nullptr;
    QCheckBox* m_transparent = nullptr;
    QCheckBox* m_crop = nullptr;
};
