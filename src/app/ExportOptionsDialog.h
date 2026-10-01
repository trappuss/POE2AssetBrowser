#pragma once
#include "model/GlbExporter.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;

// Per-export options dialog, shown when exporting one or many models (Ctrl+E / context menu). It is
// the same set of knobs as Settings, but chosen at export time and REMEMBERED back to Config so the
// next export defaults to the last choice. Two things beyond Settings: an animation-scope choice (all
// clips / the currently-selected clip only / none) and the output format (.glb vs .gltf + .bin), plus
// an "include attachments" toggle that bakes the body's assembled attachments into the export.
class ExportOptionsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportOptionsDialog(QWidget* parent = nullptr);

    // Configure to the model being exported: how many clips it has, which one is selected in the
    // viewport (for "current clip only"; -1 = none/bind), whether it has attachments, and whether more
    // than one model is being exported (tweaks the window title/wording). Call before exec().
    void setContext(int clipCount, int currentClip, bool hasAttachments, int modelCount);

    GlbExporter::Options options() const;   // the chosen options (onlyClip set for "current clip only")
    bool wantGltf() const;                  // true → write .gltf + .bin; false → single .glb

private:
    void load();
    void save() const;

    int m_currentClip = -1;

    QDoubleSpinBox* m_unitScale = nullptr;
    QCheckBox*      m_yaw180 = nullptr;
    QCheckBox*      m_skeleton = nullptr;
    QComboBox*      m_animMode = nullptr;      // 0 all · 1 current clip only · 2 none
    QCheckBox*      m_attachments = nullptr;
    QCheckBox*      m_embedTextures = nullptr;
    QCheckBox*      m_looseTextures = nullptr;
    QCheckBox*      m_normalZ = nullptr;
    QComboBox*      m_format = nullptr;        // 0 .glb · 1 .gltf + .bin
};
