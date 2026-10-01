#include "app/ExportOptionsDialog.h"
#include "app/Config.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ExportOptionsDialog::ExportOptionsDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Export options"));

    auto* box = new QGroupBox(QStringLiteral("glTF export"), this);
    auto* form = new QFormLayout(box);

    m_unitScale = new QDoubleSpinBox(box);
    m_unitScale->setDecimals(5); m_unitScale->setRange(0.00001, 1000.0); m_unitScale->setSingleStep(0.001);
    m_unitScale->setToolTip(QStringLiteral("Metres per native game unit. PoE2 units are ~0.5 cm, so 0.005 gives\n"
        "real-world-scale characters (~1.8 m). Use 1.0 to keep raw native units."));
    form->addRow(QStringLiteral("Unit scale (m per unit):"), m_unitScale);

    m_format = new QComboBox(box);
    m_format->addItems({QStringLiteral("Single file (.glb)"), QStringLiteral("Text + buffer (.gltf + .bin)")});
    form->addRow(QStringLiteral("Format:"), m_format);

    m_animMode = new QComboBox(box);
    m_animMode->addItems({QStringLiteral("All clips"), QStringLiteral("Current clip only"), QStringLiteral("None")});
    form->addRow(QStringLiteral("Animations:"), m_animMode);

    m_skeleton = new QCheckBox(QStringLiteral("Include skeleton"), box); form->addRow(QString(), m_skeleton);
    m_attachments = new QCheckBox(QStringLiteral("Include attachments (assembled character)"), box);
    m_attachments->setToolTip(QStringLiteral("Bake this body's attachments (coat, hat, weapons…) onto their bones so the\n"
        "exported file matches the assembled view. Attachments are weighted to their parent bone, so they follow the animation."));
    form->addRow(QString(), m_attachments);
    m_yaw180 = new QCheckBox(QStringLiteral("Rotate 180° about up (face camera / thumbnails)"), box); form->addRow(QString(), m_yaw180);
    m_embedTextures = new QCheckBox(QStringLiteral("Embed textures"), box); form->addRow(QString(), m_embedTextures);
    m_looseTextures = new QCheckBox(QStringLiteral("Loose textures (.gltf → sibling .png files)"), box);
    m_looseTextures->setToolTip(QStringLiteral("Write textures as separate .png files next to a .gltf instead of embedding them. Only applies to .gltf."));
    form->addRow(QString(), m_looseTextures);
    m_normalZ = new QCheckBox(QStringLiteral("Reconstruct normal-map Z (BC5)"), box); form->addRow(QString(), m_normalZ);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Export"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { save(); accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* outer = new QVBoxLayout(this);
    outer->addWidget(box);
    outer->addWidget(buttons);
    load();
}

void ExportOptionsDialog::setContext(int clipCount, int currentClip, bool hasAttachments, int modelCount)
{
    m_currentClip = currentClip;
    setWindowTitle(modelCount > 1 ? QStringLiteral("Export %1 models").arg(modelCount) : QStringLiteral("Export options"));
    // "Current clip only" needs a selected clip; if there isn't one, don't leave it chosen.
    const bool canCurrent = currentClip >= 0 && clipCount > 0;
    if (!canCurrent && m_animMode->currentIndex() == 1) m_animMode->setCurrentIndex(0);
    m_attachments->setEnabled(hasAttachments);
    if (!hasAttachments) m_attachments->setChecked(false);
    m_attachments->setToolTip(hasAttachments ? m_attachments->toolTip()
        : QStringLiteral("This model declares no attachments."));
}

void ExportOptionsDialog::load()
{
    m_unitScale->setValue(Config::exportUnitScale());
    m_yaw180->setChecked(Config::exportYaw180());
    m_skeleton->setChecked(Config::exportSkeleton());
    m_animMode->setCurrentIndex(Config::exportAnimations() ? 0 : 2);   // remembered All/None; Current is a live pick
    m_attachments->setChecked(Config::exportIncludeAttachments());
    m_embedTextures->setChecked(Config::exportEmbedTextures());
    m_looseTextures->setChecked(Config::exportLooseTextures());
    m_normalZ->setChecked(Config::exportReconstructNormalZ());
    m_format->setCurrentIndex(Config::exportGltf() ? 1 : 0);
}

void ExportOptionsDialog::save() const
{
    Config::setExportUnitScale(m_unitScale->value());
    Config::setExportYaw180(m_yaw180->isChecked());
    Config::setExportSkeleton(m_skeleton->isChecked());
    Config::setExportAnimations(m_animMode->currentIndex() != 2);   // All or Current → animations on
    Config::setExportIncludeAttachments(m_attachments->isChecked());
    Config::setExportEmbedTextures(m_embedTextures->isChecked());
    Config::setExportLooseTextures(m_looseTextures->isChecked());
    Config::setExportReconstructNormalZ(m_normalZ->isChecked());
    Config::setExportGltf(m_format->currentIndex() == 1);
}

GlbExporter::Options ExportOptionsDialog::options() const
{
    GlbExporter::Options o;
    o.unitScale          = float(m_unitScale->value());
    o.yaw180             = m_yaw180->isChecked();
    o.includeSkeleton    = m_skeleton->isChecked();
    o.includeAnimations  = m_animMode->currentIndex() != 2;
    o.onlyClip           = m_animMode->currentIndex() == 1 ? m_currentClip : -1;
    o.includeAttachments = m_attachments->isChecked();
    o.embedTextures      = m_embedTextures->isChecked();
    o.looseTextures      = m_looseTextures->isChecked();
    o.reconstructNormalZ = m_normalZ->isChecked();
    return o;
}

bool ExportOptionsDialog::wantGltf() const { return m_format->currentIndex() == 1; }
