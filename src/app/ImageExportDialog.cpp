#include "app/ImageExportDialog.h"
#include "app/Config.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QVBoxLayout>

// The offered scale ladder. Stored as the PERCENT itself (a stable value), never the combo index —
// inserting a rung would otherwise reinterpret every saved setting (template §3.1/§3.2).
static const int kScales[] = { 25, 50, 100, 200, 400 };

ImageExportDialog::ImageExportDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Save preview image"));

    auto* box = new QGroupBox(QStringLiteral("PNG image"), this);
    auto* form = new QFormLayout(box);

    m_scale = new QComboBox(box);
    for (int p : kScales) m_scale->addItem(QStringLiteral("%1%").arg(p), p);
    m_scale->setToolTip(QStringLiteral("Renders the view again at this size. 200% re-renders at double resolution\n"
        "for a sharper image — it is NOT an upscale of the on-screen pixels."));
    form->addRow(QStringLiteral("Scale:"), m_scale);

    m_transparent = new QCheckBox(QStringLiteral("Transparent background"), box);
    m_transparent->setToolTip(QStringLiteral("Save the model on a transparent background (alpha PNG) instead of the\n"
        "viewport backdrop — for icons and compositing."));
    form->addRow(QString(), m_transparent);

    m_crop = new QCheckBox(QStringLiteral("Crop to model"), box);
    m_crop->setToolTip(QStringLiteral("Trim the empty margin so the image is tight around the model.\n"
        "Only applies with a transparent background."));
    form->addRow(QString(), m_crop);

    // Crop only makes sense against a transparent background (there is no margin to detect otherwise).
    connect(m_transparent, &QCheckBox::toggled, m_crop, &QCheckBox::setEnabled);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Save…"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { save(); accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* outer = new QVBoxLayout(this);
    outer->addWidget(box);
    outer->addWidget(buttons);
    load();
}

int  ImageExportDialog::scalePercent() const  { return m_scale->currentData().toInt(); }
bool ImageExportDialog::transparentBg() const { return m_transparent->isChecked(); }
bool ImageExportDialog::cropToModel() const   { return m_transparent->isChecked() && m_crop->isChecked(); }

void ImageExportDialog::load()
{
    const int p = Config::imageScalePercent();
    int idx = m_scale->findData(p);
    if (idx < 0) idx = m_scale->findData(100);   // an unknown saved value falls back to 100%
    m_scale->setCurrentIndex(qMax(0, idx));
    m_transparent->setChecked(Config::imageTransparentBg());
    m_crop->setChecked(Config::imageCropToModel());
    m_crop->setEnabled(m_transparent->isChecked());
}

void ImageExportDialog::save() const
{
    Config::setImageScalePercent(m_scale->currentData().toInt());
    Config::setImageTransparentBg(m_transparent->isChecked());
    Config::setImageCropToModel(m_crop->isChecked());
}
