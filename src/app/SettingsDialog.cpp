#include "app/SettingsDialog.h"
#include "app/Config.h"
#include "app/Hotkeys.h"
#include "app/AppPaths.h"
#include "util/ExportLayout.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShowEvent>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Settings"));
    resize(560, 520);

    m_tabs = new QTabWidget(this);
    // Never elide tab labels (template §16): show every tab's full text, add scroll buttons as the
    // safety net rather than truncating ("General" once rendered as "eneral").
    m_tabs->tabBar()->setElideMode(Qt::ElideNone);
    m_tabs->tabBar()->setExpanding(false);
    m_tabs->tabBar()->setUsesScrollButtons(true);

    m_tabs->addTab(makeGeneralTab(),     QStringLiteral("General"));
    m_tabs->addTab(makeExportTab(),      QStringLiteral("Export"));
    m_tabs->addTab(makeHotkeysTab(),     QStringLiteral("Hotkeys"));
    m_tabs->addTab(makeMaintenanceTab(), QStringLiteral("Maintenance"));
    m_tabs->addTab(makeInformationTab(), QStringLiteral("Information"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::RestoreDefaults | QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::RestoreDefaults)->setText(QStringLiteral("Restore export defaults"));
    buttons->button(QDialogButtonBox::RestoreDefaults)->setToolTip(QStringLiteral("Clear the export options back to their defaults (leaves your folders and hotkeys)."));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        save();
        if (m_gameDir->text() != m_origGameDir || m_bundlesOverride->text() != m_origBundles) emit gameDirChanged();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, &SettingsDialog::restoreExportDefaults);

    auto* outer = new QVBoxLayout(this);
    outer->addWidget(m_tabs, 1);
    outer->addWidget(buttons);

    load();
}

QWidget* SettingsDialog::wrapScroll(QWidget* content)
{
    auto* sc = new QScrollArea(this);
    sc->setWidget(content);
    sc->setWidgetResizable(true);
    sc->setFrameShape(QFrame::NoFrame);
    return sc;
}

void SettingsDialog::showEvent(QShowEvent* e)
{
    QDialog::showEvent(e);
    // Ensure the window is at least as wide as the tab bar wants, so no tab label is clipped.
    const int need = m_tabs->tabBar()->sizeHint().width() + 24;
    if (width() < need) resize(need, height());
}

// ── General: directories ────────────────────────────────────────────────────────────────────
QWidget* SettingsDialog::makeGeneralTab()
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);

    auto* dirs = new QGroupBox(QStringLiteral("Directories"), w);
    auto* form = new QFormLayout(dirs);
    auto browseRow = [&](QLineEdit*& edit, const QString& caption) {
        auto* row = new QWidget(dirs); auto* h = new QHBoxLayout(row); h->setContentsMargins(0,0,0,0);
        edit = new QLineEdit(row);
        auto* btn = new QPushButton(QStringLiteral("Browse…"), row);
        connect(btn, &QPushButton::clicked, this, [this, edit, caption] {
            const QString d = QFileDialog::getExistingDirectory(this, caption, edit->text());
            if (!d.isEmpty()) edit->setText(d);
        });
        h->addWidget(edit, 1); h->addWidget(btn);
        return row;
    };
    form->addRow(QStringLiteral("Path of Exile 2 folder:"), browseRow(m_gameDir, QStringLiteral("Path of Exile 2 folder")));
    form->addRow(QStringLiteral("Bundles2 override (optional):"), browseRow(m_bundlesOverride, QStringLiteral("Bundles2 folder")));
    m_bundlesOverride->setToolTip(QStringLiteral("Leave blank to use <game folder>/Bundles2. Set this only if your bundles live elsewhere."));
    v->addWidget(dirs);

    auto* note = new QLabel(QStringLiteral("Changing the folder reloads the asset index when you press OK."), w);
    note->setWordWrap(true); note->setStyleSheet(QStringLiteral("color: palette(mid);"));
    v->addWidget(note);
    v->addStretch(1);
    return wrapScroll(w);
}

// ── Export: Models + Bulk sub-tabs ──────────────────────────────────────────────────────────
QWidget* SettingsDialog::makeExportTab()
{
    auto* sub = new QTabWidget;
    sub->tabBar()->setElideMode(Qt::ElideNone);
    sub->tabBar()->setExpanding(false);

    // Models
    {
        auto* w = new QWidget; auto* form = new QFormLayout(w);
        m_unitScale = new QDoubleSpinBox(w); m_unitScale->setDecimals(5); m_unitScale->setRange(0.00001, 1000.0); m_unitScale->setSingleStep(0.001);
        m_unitScale->setToolTip(QStringLiteral("Metres per native game unit. PoE2 units are ~0.5 cm, so 0.005 gives real-world-scale\ncharacters (~1.8 m). Use 1.0 to keep raw native units."));
        form->addRow(QStringLiteral("Unit scale (m per unit):"), m_unitScale);
        m_format = new QComboBox(w); m_format->addItems({QStringLiteral("Single file (.glb)"), QStringLiteral("Text + buffer (.gltf + .bin)")});
        form->addRow(QStringLiteral("Format:"), m_format);
        m_animations = new QComboBox(w); m_animations->addItems({QStringLiteral("All clips"), QStringLiteral("None")});
        m_animations->setToolTip(QStringLiteral("Default animation scope. The per-export dialog can also pick the current clip only."));
        form->addRow(QStringLiteral("Animations:"), m_animations);
        m_skeleton = new QCheckBox(QStringLiteral("Include skeleton"), w); form->addRow(QString(), m_skeleton);
        m_attachments = new QCheckBox(QStringLiteral("Include attachments (assembled character)"), w);
        m_attachments->setToolTip(QStringLiteral("Bake a body's attachments (coat, hat, weapons…) onto their bones so the export matches the assembled view."));
        form->addRow(QString(), m_attachments);
        m_yaw180 = new QCheckBox(QStringLiteral("Rotate 180° about up (face camera / thumbnails)"), w); form->addRow(QString(), m_yaw180);
        m_embedTextures = new QCheckBox(QStringLiteral("Embed textures"), w); form->addRow(QString(), m_embedTextures);
        m_looseTextures = new QCheckBox(QStringLiteral("Loose textures (.gltf → sibling .png files)"), w);
        m_looseTextures->setToolTip(QStringLiteral("Write textures as separate .png files next to a .gltf instead of inside the file.\nOnly applies to .gltf export; a .glb is always self-contained."));
        form->addRow(QString(), m_looseTextures);
        m_normalZ = new QCheckBox(QStringLiteral("Reconstruct normal-map Z (BC5)"), w); form->addRow(QString(), m_normalZ);
        m_exportShowPrompt = new QCheckBox(QStringLiteral("Show export options each time"), w);
        m_exportShowPrompt->setToolTip(QStringLiteral("When off, exports skip the options dialog and use these saved defaults."));
        form->addRow(QString(), m_exportShowPrompt);
        m_exportReuseLastDir = new QCheckBox(QStringLiteral("Export to last-used folder (skip file picker)"), w);
        m_exportReuseLastDir->setToolTip(QStringLiteral("When on, exports go straight into the last folder you exported to, auto-naming the file."));
        form->addRow(QString(), m_exportReuseLastDir);
        sub->addTab(wrapScroll(w), QStringLiteral("Models"));
    }
    // Image (Save preview image / icon export)
    {
        auto* w = new QWidget; auto* form = new QFormLayout(w);
        m_imageScale = new QSpinBox(w); m_imageScale->setRange(25, 400); m_imageScale->setSingleStep(25); m_imageScale->setSuffix(QStringLiteral(" %"));
        m_imageScale->setToolTip(QStringLiteral("Render scale. 100%% is on-screen resolution; higher RE-RENDERS larger (true supersampling, sharper)."));
        form->addRow(QStringLiteral("Render scale:"), m_imageScale);
        m_imageTransparent = new QCheckBox(QStringLiteral("Transparent background"), w); form->addRow(QString(), m_imageTransparent);
        m_imageCrop = new QCheckBox(QStringLiteral("Crop to model (needs transparent background)"), w); form->addRow(QString(), m_imageCrop);
        m_imageShowPrompt = new QCheckBox(QStringLiteral("Show image options each time"), w);
        m_imageShowPrompt->setToolTip(QStringLiteral("When off, Save preview image skips the options dialog and uses these saved defaults."));
        form->addRow(QString(), m_imageShowPrompt);
        sub->addTab(wrapScroll(w), QStringLiteral("Image"));
    }
    // Bulk
    {
        auto* w = new QWidget; auto* form = new QFormLayout(w);
        m_bulkWorkers = new QSpinBox(w); m_bulkWorkers->setRange(0, 32); m_bulkWorkers->setSpecialValueText(QStringLiteral("Auto"));
        m_bulkWorkers->setToolTip(QStringLiteral("Worker threads for bulk extraction. 0 = Auto (one per core)."));
        form->addRow(QStringLiteral("Worker threads:"), m_bulkWorkers);
        m_bulkLayout = new QComboBox(w);
        m_bulkLayout->addItem(QStringLiteral("Flat"), ExportLayout::kFlat());
        m_bulkLayout->addItem(QStringLiteral("By type"), ExportLayout::kType());
        m_bulkLayout->addItem(QStringLiteral("Mirror game folders"), ExportLayout::kFolder());
        m_bulkLayout->addItem(QStringLiteral("Folder per model"), ExportLayout::kModel());
        m_bulkLayout->setToolTip(QStringLiteral("How bulk/multi-select exports group files into folders. Single-model export ignores this."));
        form->addRow(QStringLiteral("Folder layout:"), m_bulkLayout);
        sub->addTab(wrapScroll(w), QStringLiteral("Bulk"));
    }
    return sub;
}

// ── Hotkeys editor over the central registry ────────────────────────────────────────────────
QWidget* SettingsDialog::makeHotkeysTab()
{
    auto* w = new QWidget; auto* form = new QFormLayout(w);
    m_hotkeyEdits.clear();
    for (const Hotkeys::Def& d : Hotkeys::defs()) {
        auto* edit = new QKeySequenceEdit(w);
        form->addRow(d.label + QLatin1Char(':'), edit);
        m_hotkeyEdits.append(edit);
    }
    auto* note = new QLabel(QStringLiteral("Cleared fields are unbound. Some shortcuts apply after the next launch."), w);
    note->setWordWrap(true); note->setStyleSheet(QStringLiteral("color: palette(mid);"));
    form->addRow(note);
    return wrapScroll(w);
}

// ── Maintenance: caches & reset ─────────────────────────────────────────────────────────────
QWidget* SettingsDialog::makeMaintenanceTab()
{
    auto* w = new QWidget; auto* v = new QVBoxLayout(w);
    auto* box = new QGroupBox(QStringLiteral("Caches && reset"), w);   // && so Qt shows one literal &
    auto* bv = new QVBoxLayout(box);
    m_cacheBox = new QWidget(box); new QVBoxLayout(m_cacheBox);
    bv->addWidget(m_cacheBox);
    auto* clearAll = new QPushButton(QStringLiteral("Clear all caches"), box);
    clearAll->setToolTip(QStringLiteral("Delete every cache in data\\. They rebuild automatically on the next launch (slower first open)."));
    connect(clearAll, &QPushButton::clicked, this, [this] {
        const QDir d(AppPaths::dataDir());
        for (const QString& fn : d.entryList(QStringList{QStringLiteral("*.bin")}, QDir::Files)) QFile::remove(d.filePath(fn));
        refreshCacheList();
    });
    bv->addWidget(clearAll);
    v->addWidget(box);
    m_cacheNote = new QLabel(w); m_cacheNote->setWordWrap(true); m_cacheNote->setStyleSheet(QStringLiteral("color: palette(mid);"));
    m_cacheNote->setText(QStringLiteral("Caches live in data\\ beside the executable and are keyed to the game build; old versions are pruned automatically."));
    v->addWidget(m_cacheNote);
    v->addStretch(1);
    refreshCacheList();
    return wrapScroll(w);
}

void SettingsDialog::refreshCacheList()
{
    if (!m_cacheBox) return;
    auto* lay = qobject_cast<QVBoxLayout*>(m_cacheBox->layout());
    QLayoutItem* it; while ((it = lay->takeAt(0))) { if (it->widget()) it->widget()->deleteLater(); delete it; }
    const QDir d(AppPaths::dataDir());
    const QStringList bins = d.entryList(QStringList{QStringLiteral("*.bin")}, QDir::Files, QDir::Name);
    if (bins.isEmpty()) { lay->addWidget(new QLabel(QStringLiteral("(no caches built yet)"), m_cacheBox)); return; }
    for (const QString& fn : bins) {
        const qint64 sz = QFileInfo(d.filePath(fn)).size();
        auto* row = new QWidget(m_cacheBox); auto* h = new QHBoxLayout(row); h->setContentsMargins(0,0,0,0);
        h->addWidget(new QLabel(QStringLiteral("%1  —  %2").arg(fn, QLocale().formattedDataSize(sz)), row), 1);
        auto* clr = new QPushButton(QStringLiteral("Clear"), row);
        const QString full = d.filePath(fn);
        connect(clr, &QPushButton::clicked, this, [this, full] { QFile::remove(full); refreshCacheList(); });
        h->addWidget(clr);
        lay->addWidget(row);
    }
}

// ── Information: explain the confusable options in-tool ──────────────────────────────────────
QWidget* SettingsDialog::makeInformationTab()
{
    auto* w = new QWidget; auto* v = new QVBoxLayout(w);
    auto* lbl = new QLabel(w); lbl->setWordWrap(true); lbl->setTextFormat(Qt::RichText);
    lbl->setText(QStringLiteral(
        "<b>Unit scale</b> — metres per native unit. PoE2 art is ~0.5&nbsp;cm/unit, so <b>0.005</b> makes a "
        "character about 1.8&nbsp;m in Blender. Set 1.0 to keep raw units.<br><br>"
        "<b>Format · .glb vs .gltf&nbsp;+&nbsp;.bin</b> — <b>.glb</b> is one self-contained binary file (simplest). "
        "<b>.gltf&nbsp;+&nbsp;.bin</b> is a text glTF beside an external binary buffer — pick it if a pipeline wants to edit the JSON.<br><br>"
        "<b>Reconstruct normal-map Z</b> — PoE2 normals often store only X,Y (BC5-style). Rebuilding Z = √(1−x²−y²) makes DCC apps light them correctly. Leave on.<br><br>"
        "<b>Include attachments</b> — exports the assembled character (body + coat/hat/weapons on their bones) as one file, matching the assembled view, instead of just the selected mesh.<br><br>"
        "<b>Rotate 180°</b> — off by default (original orientation preferred). Turn on only for thumbnail tools that expect characters facing the camera.<br><br>"
        "<b>Folder layout</b> (bulk / multi-select) — how exported files are grouped into folders. Single-model export always ignores it and writes exactly where you point it.<br><br>"
        "<b>Save preview image · Scale</b> — the image is RE-RENDERED at the chosen size, so 200% is a genuinely sharper picture, not an enlargement of the on-screen pixels. <b>Transparent background</b> gives an alpha PNG; <b>Crop to model</b> trims the empty margin."));
    v->addWidget(lbl);
    v->addStretch(1);
    return wrapScroll(w);
}

// ── load / save / reset ─────────────────────────────────────────────────────────────────────
void SettingsDialog::load()
{
    m_origGameDir = Config::gameDir();
    m_origBundles = Config::bundlesDirOverride();
    m_gameDir->setText(m_origGameDir);
    m_bundlesOverride->setText(m_origBundles);

    m_unitScale->setValue(Config::exportUnitScale());
    m_format->setCurrentIndex(Config::exportGltf() ? 1 : 0);
    m_animations->setCurrentIndex(Config::exportAnimations() ? 0 : 1);
    m_skeleton->setChecked(Config::exportSkeleton());
    m_attachments->setChecked(Config::exportIncludeAttachments());
    m_yaw180->setChecked(Config::exportYaw180());
    m_embedTextures->setChecked(Config::exportEmbedTextures());
    m_looseTextures->setChecked(Config::exportLooseTextures());
    m_normalZ->setChecked(Config::exportReconstructNormalZ());
    m_exportShowPrompt->setChecked(Config::exportShowPrompt());
    m_exportReuseLastDir->setChecked(Config::exportReuseLastDir());
    m_imageScale->setValue(Config::imageScalePercent());
    m_imageTransparent->setChecked(Config::imageTransparentBg());
    m_imageCrop->setChecked(Config::imageCropToModel());
    m_imageShowPrompt->setChecked(Config::imageShowPrompt());
    m_bulkWorkers->setValue(Config::bulkWorkers());
    { const int i = m_bulkLayout->findData(ExportLayout::mode()); m_bulkLayout->setCurrentIndex(i < 0 ? 0 : i); }

    const QVector<Hotkeys::Def> defs = Hotkeys::defs();
    for (int i = 0; i < defs.size() && i < m_hotkeyEdits.size(); ++i)
        m_hotkeyEdits[i]->setKeySequence(Hotkeys::seq(defs[i].key, defs[i].def));
}

void SettingsDialog::save()
{
    Config::setGameDir(m_gameDir->text().trimmed());
    Config::setBundlesDirOverride(m_bundlesOverride->text().trimmed());

    Config::setExportUnitScale(m_unitScale->value());
    Config::setExportGltf(m_format->currentIndex() == 1);
    Config::setExportAnimations(m_animations->currentIndex() == 0);
    Config::setExportSkeleton(m_skeleton->isChecked());
    Config::setExportIncludeAttachments(m_attachments->isChecked());
    Config::setExportYaw180(m_yaw180->isChecked());
    Config::setExportEmbedTextures(m_embedTextures->isChecked());
    Config::setExportLooseTextures(m_looseTextures->isChecked());
    Config::setExportReconstructNormalZ(m_normalZ->isChecked());
    Config::setExportShowPrompt(m_exportShowPrompt->isChecked());
    Config::setExportReuseLastDir(m_exportReuseLastDir->isChecked());
    Config::setImageScalePercent(m_imageScale->value());
    Config::setImageTransparentBg(m_imageTransparent->isChecked());
    Config::setImageCropToModel(m_imageCrop->isChecked());
    Config::setImageShowPrompt(m_imageShowPrompt->isChecked());
    Config::setBulkWorkers(m_bulkWorkers->value());
    ExportLayout::setMode(m_bulkLayout->currentData().toString());

    const QVector<Hotkeys::Def> defs = Hotkeys::defs();
    QSettings s;
    for (int i = 0; i < defs.size() && i < m_hotkeyEdits.size(); ++i) {
        const QString v = m_hotkeyEdits[i]->keySequence().toString(QKeySequence::PortableText);
        if (v.isEmpty()) s.remove(defs[i].key); else s.setValue(defs[i].key, v);
    }
}

void SettingsDialog::restoreExportDefaults()
{
    // Reset by REMOVING keys (§3.10), so a later change to a default is picked up; leave paths/hotkeys.
    QSettings s;
    s.beginGroup(QStringLiteral("export"));
    s.remove(QString());   // removes everything under export/
    s.endGroup();
    // The Image-export options on this same tab live under image/, not export/ — clear them too, or
    // "Restore export defaults" would silently leave the Image sub-tab untouched.
    s.beginGroup(QStringLiteral("image"));
    s.remove(QString());
    s.endGroup();
    load();
}
