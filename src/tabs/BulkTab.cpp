#include "tabs/BulkTab.h"
#include "index/AssetListModel.h"
#include "index/FunnelFilter.h"
#include "store/AssetStore.h"
#include "bulk/BulkExtractor.h"
#include "util/ExportLayout.h"
#include "app/Config.h"
#include "app/ExportConfig.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableView>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

BulkTab::BulkTab(AssetStore* store, QWidget* parent) : QWidget(parent), m_store(store)
{
    m_model = new AssetListModel(this);

    auto* outer = new QVBoxLayout(this);

    // ── Filter row (shared matcher + facet) + live count ───────────────────────────────────────
    auto* filterRow = new QHBoxLayout();
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("Filter  (space = AND,  -exclude,  a|b = OR,  #meta,  digits or 0x-hex = id)"));
    m_search->setClearButtonEnabled(true);
    m_facet = new QComboBox(this);
    m_facet->addItem(QStringLiteral("models + textures"), QString());
    m_facet->addItem(QStringLiteral(".smd"), QStringLiteral(".smd"));
    m_facet->addItem(QStringLiteral(".fmt"), QStringLiteral(".fmt"));
    m_facet->addItem(QStringLiteral(".dds"), QStringLiteral(".dds"));
    // No Shader group here: the shader facets only classify models, and the bulk set includes
    // textures — a workflow chip would silently hide every .dds. (Shader search tokens like
    // #workflow:metalrough still work via the material index set below.)
    m_funnel = new FunnelFilter(this, {QStringLiteral("Category"), QStringLiteral("Item type")});
    filterRow->addWidget(m_search, 1); filterRow->addWidget(m_facet); filterRow->addWidget(m_funnel);
    outer->addLayout(filterRow);
    outer->addWidget(m_funnel->chipBar());

    m_list = new QTableView(this);
    m_list->setModel(m_model);
    m_list->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_list->verticalHeader()->setVisible(false);
    m_list->horizontalHeader()->setStretchLastSection(true);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setColumnWidth(0, 420);
    outer->addWidget(m_list, 1);
    m_count = new QLabel(this); outer->addWidget(m_count);

    // ── Run controls ───────────────────────────────────────────────────────────────────────────
    auto* controls = new QGroupBox(QStringLiteral("Run"), this);
    auto* cv = new QVBoxLayout(controls);

    auto* dirRow = new QHBoxLayout();
    dirRow->addWidget(new QLabel(QStringLiteral("Output folder:")));
    m_outDir = new QLineEdit(Config::bulkOutDir(), this);
    auto* browse = new QPushButton(QStringLiteral("Browse…"), this);
    dirRow->addWidget(m_outDir, 1); dirRow->addWidget(browse);
    cv->addLayout(dirRow);

    auto* optRow = new QHBoxLayout();
    optRow->addWidget(new QLabel(QStringLiteral("Layout:")));
    m_layout = new QComboBox(this);
    m_layout->addItem(QStringLiteral("Flat"), ExportLayout::kFlat());
    m_layout->addItem(QStringLiteral("By type"), ExportLayout::kType());
    m_layout->addItem(QStringLiteral("Mirror game folders"), ExportLayout::kFolder());
    m_layout->addItem(QStringLiteral("Folder per model"), ExportLayout::kModel());
    optRow->addWidget(m_layout);
    m_onlyNew = new QCheckBox(QStringLiteral("Only new (skip already exported)"), this); m_onlyNew->setChecked(true);
    m_doModels = new QCheckBox(QStringLiteral("Models → .glb"), this); m_doModels->setChecked(true);
    m_doTextures = new QCheckBox(QStringLiteral("Textures → .png"), this); m_doTextures->setChecked(true);
    // Non-destructive: write the EXACT original game files (mesh + its .sm/.mat/.dds/.ao/.ast) instead
    // of converting to .glb/.png. Mirrors game paths; the convert-format checkboxes above are ignored.
    m_rawOriginals = new QCheckBox(QStringLiteral("Raw originals (exact files, unmodified)"), this);
    m_rawOriginals->setToolTip(QStringLiteral(
        "Extract the pristine authored files (a model pulls its .sm, .mat and .dds dependencies),\n"
        "byte-for-byte, in their original game folder structure — instead of converting to .glb/.png."));
    connect(m_rawOriginals, &QCheckBox::toggled, this, [this](bool on) {
        m_doModels->setEnabled(!on); m_doTextures->setEnabled(!on); m_yaw180->setEnabled(!on); });
    // Off by default: the original model orientation is preferred on export. Enable only for
    // thumbnail pipelines that need characters facing the camera. Persisted in Config.
    m_yaw180 = new QCheckBox(QStringLiteral("Rotate 180° (face camera / thumbnails)"), this);
    m_yaw180->setChecked(Config::exportYaw180());
    m_yaw180->setToolTip(QStringLiteral("Rotates models 180° about the up axis on export, for thumbnail\n"
        "generators that expect characters facing the camera. Off keeps the\n"
        "original orientation (preferred). Applied as a wrapper node — the mesh\n"
        "geometry itself is unchanged."));
    connect(m_yaw180, &QCheckBox::toggled, this, [](bool on) { Config::setExportYaw180(on); });
    optRow->addSpacing(12); optRow->addWidget(m_onlyNew);
    optRow->addSpacing(12); optRow->addWidget(m_doModels); optRow->addWidget(m_doTextures);
    optRow->addSpacing(12); optRow->addWidget(m_rawOriginals);
    optRow->addSpacing(12); optRow->addWidget(m_yaw180);
    optRow->addStretch(1);
    // Parallel worker count. 0 = Auto (idealThreadCount). Bulk work is CPU-bound, so more threads
    // cut wall time on multi-core machines; reads are thread-safe. Persisted in Config.
    optRow->addWidget(new QLabel(QStringLiteral("Threads:")));
    m_workers = new QSpinBox(this);
    m_workers->setRange(0, 32);
    m_workers->setSpecialValueText(QStringLiteral("Auto"));   // shown when value == 0
    m_workers->setValue(Config::bulkWorkers());
    m_workers->setToolTip(QStringLiteral("Parallel extraction threads. Auto uses one per CPU core.\n"
        "The work (decode + assemble + write) is CPU-bound and thread-safe,\n"
        "so more threads cut wall time on multi-core machines."));
    connect(m_workers, qOverload<int>(&QSpinBox::valueChanged), this, [](int n) { Config::setBulkWorkers(n); });
    optRow->addWidget(m_workers);
    cv->addLayout(optRow);

    auto* btnRow = new QHBoxLayout();
    m_run = new QPushButton(QStringLiteral("Extract matches"), this);
    m_pause = new QPushButton(QStringLiteral("Pause"), this); m_pause->setEnabled(false);
    m_cancel = new QPushButton(QStringLiteral("Cancel"), this); m_cancel->setEnabled(false);
    m_progress = new QProgressBar(this); m_progress->setTextVisible(true);
    btnRow->addWidget(m_run); btnRow->addWidget(m_pause); btnRow->addWidget(m_cancel);
    btnRow->addWidget(m_progress, 1);
    cv->addLayout(btnRow);

    m_eta = new QLabel(this); cv->addWidget(m_eta);
    m_console = new QPlainTextEdit(this); m_console->setReadOnly(true); m_console->setMaximumBlockCount(2000);
    m_console->setMinimumHeight(120);
    cv->addWidget(m_console);
    outer->addWidget(controls);

    // Restore the layout choice (stable string id, findData; unknown → Flat).
    { const int i = m_layout->findData(ExportLayout::mode()); m_layout->setCurrentIndex(i < 0 ? 0 : i); }

    // ── Wiring ───────────────────────────────────────────────────────────────────────────────
    m_debounce = new QTimer(this); m_debounce->setSingleShot(true); m_debounce->setInterval(180);
    connect(m_debounce, &QTimer::timeout, this, &BulkTab::applyFilter);
    connect(m_search, &QLineEdit::textChanged, this, [this] { m_debounce->start(); });
    connect(m_facet, qOverload<int>(&QComboBox::currentIndexChanged), this, &BulkTab::applyFilter);
    m_funnel->setActive(QSettings().value(QStringLiteral("facets/bulk")).toStringList(),
                        QSettings().value(QStringLiteral("facets/bulkMatchAny")).toBool());
    connect(m_funnel, &FunnelFilter::changed, this, [this] {
        QSettings().setValue(QStringLiteral("facets/bulk"), m_funnel->activeIds());
        QSettings().setValue(QStringLiteral("facets/bulkMatchAny"), m_funnel->matchAny());
        applyFilter();
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString start = m_outDir->text().isEmpty() ? QDir::homePath() : m_outDir->text();
        const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose output folder"), start);
        if (!d.isEmpty()) { m_outDir->setText(d); Config::setBulkOutDir(d); }
    });
    connect(m_layout, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        QSettings().setValue(QStringLiteral("export/folderLayout"), m_layout->currentData().toString());
    });
    connect(m_run, &QPushButton::clicked, this, &BulkTab::startRun);
    connect(m_pause, &QPushButton::clicked, this, &BulkTab::togglePause);
    connect(m_cancel, &QPushButton::clicked, this, &BulkTab::cancelRun);
}

BulkTab::~BulkTab()
{
    if (m_worker) m_worker->requestCancel();
    if (m_thread) { m_thread->quit(); m_thread->wait(3000); }
}

void BulkTab::onIndexReady()
{
    m_model->setIndex(m_store->isOpen() ? &m_store->index() : nullptr,
                      {QStringLiteral(".smd"), QStringLiteral(".fmt"), QStringLiteral(".dds")});
    applyFilter();
}

void BulkTab::onMaterialsReady()
{
    m_model->setMaterialIndex(m_store->isMaterialIndexReady() ? &m_store->materialIndex() : nullptr);
    m_model->setNameIndex(m_store->isNameIndexReady() ? &m_store->nameIndex() : nullptr);
    applyFilter();
}

void BulkTab::applyFilter()
{
    m_model->applyFilters(m_search->text(), m_facet->currentData().toString(),
                          m_funnel->activeIds(), m_funnel->matchAny());
    m_funnel->setCounts(m_model->facetCounts());
    m_count->setText(QStringLiteral("%1 of %2 assets match — this is what will be extracted")
        .arg(m_model->rowCount()).arg(m_model->totalInBaseSet()));
}

void BulkTab::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape && m_worker) { cancelRun(); e->accept(); return; }
    QWidget::keyPressEvent(e);
}

void BulkTab::startRun()
{
    if (m_worker) return;   // already running
    if (!m_store->isOpen()) { emit status(QStringLiteral("Index not loaded yet.")); return; }
    const QString out = m_outDir->text().trimmed();
    if (out.isEmpty()) { emit status(QStringLiteral("Choose an output folder first.")); return; }
    Config::setBulkOutDir(out);

    // Snapshot the matched set now (the count you see is the set you export, §14).
    QVector<ExportLayout::Item> items;
    items.reserve(m_model->rowCount());
    for (int r = 0; r < m_model->rowCount(); ++r) {
        const uint32_t fi = m_model->fileIndexAt(r);
        if (fi != UINT32_MAX) items.push_back({fi, m_model->pathAt(r)});
    }
    if (items.isEmpty()) { emit status(QStringLiteral("No matches to extract.")); return; }

    BulkExtractor::Options opt;
    opt.outDir = out;
    opt.layout = m_layout->currentData().toString();
    opt.onlyNew = m_onlyNew->isChecked();
    opt.rawOriginals = m_rawOriginals->isChecked();
    // Raw mode extracts both models (with their dependencies) and textures as originals.
    opt.models = opt.rawOriginals ? true : m_doModels->isChecked();
    opt.textures = opt.rawOriginals ? true : m_doTextures->isChecked();
    opt.workers = m_workers->value();         // 0 = auto; clamped in the extractor
    opt.glb = exportOptionsFromConfig();      // scale/orientation/skeleton/etc. from Settings
    opt.glb.yaw180 = m_yaw180->isChecked();   // this tab's checkbox is the live per-run override

    m_console->clear();
    m_console->appendPlainText(QStringLiteral("Extracting %1 assets → %2 (%3)")
        .arg(items.size()).arg(out).arg(opt.layout.isEmpty() ? QStringLiteral("Flat") : opt.layout));
    m_progress->setRange(0, items.size()); m_progress->setValue(0);

    m_thread = new QThread(this);
    m_worker = new BulkExtractor(m_store, std::move(items), opt);
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::started, m_worker, &BulkExtractor::run);
    connect(m_worker, &BulkExtractor::progress, this, [this](int done, int total, int e, int s, int f, const QString& cur, int eta) {
        m_progress->setMaximum(total); m_progress->setValue(done);
        m_progress->setFormat(QStringLiteral("%1/%2  (%3 ok, %4 skip, %5 fail)").arg(done).arg(total).arg(e).arg(s).arg(f));
        if (eta >= 0) m_eta->setText(QStringLiteral("~%1s remaining · %2").arg(eta).arg(cur));
        else m_eta->setText(cur);
    });
    connect(m_worker, &BulkExtractor::message, this, [this](const QString& line) { m_console->appendPlainText(line); });
    connect(m_worker, &BulkExtractor::finished, this, [this](int e, int s, int f, const QString& summary) {
        emit status(summary);
        m_console->appendPlainText(summary);
        m_thread->quit(); m_thread->wait();
        m_worker->deleteLater(); m_worker = nullptr;
        m_thread->deleteLater(); m_thread = nullptr;
        setRunning(false);
        Q_UNUSED(e); Q_UNUSED(s); Q_UNUSED(f);
    });
    setRunning(true);
    m_thread->start();
}

void BulkTab::cancelRun()
{
    if (m_worker) { m_worker->requestCancel(); m_worker->setPaused(false); m_console->appendPlainText(QStringLiteral("Cancelling…")); }
}

void BulkTab::togglePause()
{
    if (!m_worker) return;
    m_paused = !m_paused;
    m_worker->setPaused(m_paused);
    m_pause->setText(m_paused ? QStringLiteral("Resume") : QStringLiteral("Pause"));
}

void BulkTab::setRunning(bool running)
{
    m_run->setEnabled(!running);
    m_pause->setEnabled(running);
    m_cancel->setEnabled(running);
    m_search->setEnabled(!running);
    m_facet->setEnabled(!running);
    m_outDir->setEnabled(!running);
    m_layout->setEnabled(!running);
    if (m_workers) m_workers->setEnabled(!running);
    if (!running) { m_paused = false; m_pause->setText(QStringLiteral("Pause")); }
}
