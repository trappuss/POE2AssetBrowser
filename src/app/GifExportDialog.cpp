#include "app/GifExportDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

// gif/* QSettings keys (one per state, stable values — template §3.1). Read directly here rather than
// through Config, the same way ExportLayout owns its export/folderLayout key.
namespace {
constexpr auto kFps    = "gif/fps";
constexpr auto kFrames = "gif/turntableFrames";
constexpr auto kScale  = "gif/scale";
constexpr auto kColors = "gif/maxColors";
constexpr auto kDither = "gif/dither";
constexpr auto kTransp = "gif/transparentBg";
constexpr auto kCrop   = "gif/cropToModel";
constexpr auto kOpt    = "gif/optimize";
constexpr auto kTarget = "gif/targetMB";
const int kScales[] = { 25, 50, 75, 100 };
}

GifExportDialog::GifExportDialog(Mode mode, QWidget* parent) : QDialog(parent), m_mode(mode)
{
    setWindowTitle(mode == Turntable ? QStringLiteral("Turntable GIF") : QStringLiteral("Animation-loop GIF"));

    auto* box = new QGroupBox(QStringLiteral("Animated GIF"), this);
    auto* form = new QFormLayout(box);

    if (mode == Turntable) {
        // Frame rate governs only the turntable's orbit smoothness/playback speed. An animation-loop
        // GIF instead uses the clip's OWN authored frame rate, so it plays at real speed — no fps knob.
        m_fps = new QSpinBox(box); m_fps->setRange(1, 60); m_fps->setSuffix(QStringLiteral(" fps"));
        form->addRow(QStringLiteral("Frame rate:"), m_fps);

        m_frames = new QSpinBox(box); m_frames->setRange(8, 240);
        m_frames->setToolTip(QStringLiteral("Frames in one full revolution. When a clip is playing this is snapped\n"
            "to a whole number of clip cycles so the loop wraps seamlessly."));
        form->addRow(QStringLiteral("Frames / turn:"), m_frames);
    } else {
        auto* note = new QLabel(QStringLiteral("Plays the clip at its own authored frame rate."), box);
        note->setStyleSheet(QStringLiteral("color: gray;"));
        form->addRow(QString(), note);
    }

    m_scale = new QComboBox(box);
    for (int p : kScales) m_scale->addItem(QStringLiteral("%1%").arg(p), p);
    m_scale->setToolTip(QStringLiteral("Downscale the GIF. GIF is not a detail medium, so only 100% and below\n"
        "are offered — the single biggest lever on file size."));
    form->addRow(QStringLiteral("Scale:"), m_scale);

    m_colors = new QSpinBox(box); m_colors->setRange(2, 256);
    m_colors->setToolTip(QStringLiteral("Palette size. Fewer colours = smaller file, coarser image."));
    form->addRow(QStringLiteral("Max colours:"), m_colors);

    m_dither = new QCheckBox(QStringLiteral("Dither (reduce banding)"), box);
    m_dither->setToolTip(QStringLiteral("Ordered pattern that breaks up palette banding. Position-only, so it adds\n"
        "no frame-to-frame shimmer. Turn off for a flatter, more compressible image."));
    form->addRow(QString(), m_dither);

    m_transparent = new QCheckBox(QStringLiteral("Transparent background"), box);
    m_transparent->setToolTip(QStringLiteral("1-bit transparency (GIF has no partial alpha) instead of the dark backdrop."));
    form->addRow(QString(), m_transparent);

    m_crop = new QCheckBox(QStringLiteral("Crop to model"), box);
    m_crop->setToolTip(QStringLiteral("Trim to the model's silhouette across the whole sequence (one box for all frames)."));
    form->addRow(QString(), m_crop);

    m_optimize = new QCheckBox(QStringLiteral("Optimise to target size"), box);
    m_targetMB = new QSpinBox(box); m_targetMB->setRange(1, 200); m_targetMB->setSuffix(QStringLiteral(" MB"));
    m_optimize->setToolTip(QStringLiteral("Re-encode (palette → dither → downscale) until the file fits the target,\n"
        "shipping the smallest attempt. Frames are captured once, so this only re-encodes."));
    connect(m_optimize, &QCheckBox::toggled, m_targetMB, &QSpinBox::setEnabled);
    form->addRow(m_optimize, m_targetMB);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Save…"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { save(); accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* outer = new QVBoxLayout(this);
    outer->addWidget(box);
    outer->addWidget(buttons);
    load();
}

ExportCapture::GifOptions GifExportDialog::options() const
{
    ExportCapture::GifOptions o;
    o.fps           = m_fps ? m_fps->value() : 25;   // turntable-only; anim-loop uses the clip's own rate
    o.turntableFrames = m_frames ? m_frames->value() : 48;
    o.scalePercent  = m_scale->currentData().toInt();
    o.maxColors     = m_colors->value();
    o.dither        = m_dither->isChecked();
    o.transparentBg = m_transparent->isChecked();
    o.cropToModel   = m_crop->isChecked();
    o.optimize      = m_optimize->isChecked();
    o.targetMB      = m_targetMB->value();
    return o;
}

void GifExportDialog::load()
{
    QSettings s;
    if (m_fps) m_fps->setValue(qBound(1, s.value(kFps, 25).toInt(), 60));
    if (m_frames) m_frames->setValue(qBound(8, s.value(kFrames, 48).toInt(), 240));
    int idx = m_scale->findData(qBound(25, s.value(kScale, 100).toInt(), 100));
    m_scale->setCurrentIndex(idx < 0 ? m_scale->count() - 1 : idx);
    m_colors->setValue(qBound(2, s.value(kColors, 256).toInt(), 256));
    m_dither->setChecked(s.value(kDither, true).toBool());
    m_transparent->setChecked(s.value(kTransp, false).toBool());
    m_crop->setChecked(s.value(kCrop, false).toBool());
    m_optimize->setChecked(s.value(kOpt, false).toBool());
    m_targetMB->setValue(qBound(1, s.value(kTarget, 10).toInt(), 200));
    m_targetMB->setEnabled(m_optimize->isChecked());
}

void GifExportDialog::save() const
{
    QSettings s;
    if (m_fps) s.setValue(kFps, m_fps->value());
    if (m_frames) s.setValue(kFrames, m_frames->value());
    s.setValue(kScale, m_scale->currentData().toInt());
    s.setValue(kColors, m_colors->value());
    s.setValue(kDither, m_dither->isChecked());
    s.setValue(kTransp, m_transparent->isChecked());
    s.setValue(kCrop, m_crop->isChecked());
    s.setValue(kOpt, m_optimize->isChecked());
    s.setValue(kTarget, m_targetMB->value());
}
