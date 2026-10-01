#include "app/MainWindow.h"
#include "app/Config.h"
#include "app/Hotkeys.h"
#include "app/SettingsDialog.h"
#include "app/ExportNotifier.h"
#include "util/TextReportDialog.h"
#include "store/AssetStore.h"
#include "store/IndexLoader.h"
#include "tabs/ModelsTab.h"
#include "tabs/TexturesTab.h"
#include "tabs/CustomizeTab.h"
#include "tabs/BulkTab.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QMenu>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QDesktopServices>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("POE2AssetBrowser"));
    resize(1240, 800);
    m_store = std::make_unique<AssetStore>();
    m_loader = new IndexLoader(m_store.get(), this);
    connect(m_loader, &IndexLoader::progress, this, &MainWindow::onIndexProgress);
    connect(m_loader, &IndexLoader::finished, this, &MainWindow::onIndexFinished);
    connect(m_loader, &IndexLoader::materialsReady, this, [this] {
        m_models->onMaterialsReady();
        m_textures->onMaterialsReady();
        m_customize->onMaterialsReady();
        m_bulk->onMaterialsReady();
        const MaterialFamilyIndex& mfx = m_store->materialIndex();
        const int named = m_store->isNameIndexReady() ? m_store->nameIndex().namedModels() : 0;
        m_status->setText(QStringLiteral("Ready — %1 shader families, %2 item names. "
            "Search by in-game name, or #workflow:/#family:/#effect:, or the Shader filters.")
            .arg(mfx.families().size() - 1).arg(named));
    });

    m_tabs = new QTabWidget(this);
    m_models = new ModelsTab(m_store.get(), this);
    m_textures = new TexturesTab(m_store.get(), this);
    m_customize = new CustomizeTab(m_store.get(), this);
    m_bulk = new BulkTab(m_store.get(), this);
    // Tab order: Textures · Models · Customize · Bulk.
    m_tabs->addTab(m_textures, QStringLiteral("Textures"));
    m_tabs->addTab(m_models, QStringLiteral("Models"));
    m_tabs->addTab(m_customize, QStringLiteral("Customize"));
    m_tabs->addTab(m_bulk, QStringLiteral("Bulk"));
    setCentralWidget(m_tabs);

    m_status = new QLabel(this);
    statusBar()->addWidget(m_status, 1);
    connect(m_models, &ModelsTab::status, m_status, &QLabel::setText);
    connect(m_textures, &TexturesTab::status, m_status, &QLabel::setText);
    connect(m_customize, &CustomizeTab::status, m_status, &QLabel::setText);
    connect(m_bulk, &BulkTab::status, m_status, &QLabel::setText);

    // One consistent export-completion notice (template §15): every export path calls
    // ExportNotifier::notify(summary, folder); we put the summary on the status bar and reveal the
    // output folder through a button that only appears when there is a folder to show.
    m_revealBtn = new QToolButton(this);
    m_revealBtn->setText(QStringLiteral("📂 Show in folder"));
    m_revealBtn->setAutoRaise(true);
    m_revealBtn->setCursor(Qt::PointingHandCursor);
    m_revealBtn->hide();
    statusBar()->addPermanentWidget(m_revealBtn);
    connect(m_revealBtn, &QToolButton::clicked, this, [this] {
        if (!m_lastExportFolder.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(m_lastExportFolder));
    });
    connect(&ExportNotifier::instance(), &ExportNotifier::exported, this, [this](const QString& text, const QString& folder) {
        m_status->setText(text);
        m_lastExportFolder = folder;
        m_revealBtn->setVisible(!folder.isEmpty());
        if (!folder.isEmpty()) m_revealBtn->setToolTip(QStringLiteral("Open %1").arg(folder));
    });

    auto* file = menuBar()->addMenu(QStringLiteral("&File"));
    file->addAction(QStringLiteral("Set Path of Exile 2 folder…"), this, &MainWindow::chooseGameFolder);
    file->addAction(QStringLiteral("Reload index"), this, &MainWindow::reloadIndex);
    file->addSeparator();
    file->addAction(QStringLiteral("Health check…"), this, &MainWindow::showHealthCheck);
    file->addSeparator();
    file->addAction(QStringLiteral("Settings…"), this, [this] {
        SettingsDialog dlg(this);
        connect(&dlg, &SettingsDialog::gameDirChanged, this, &MainWindow::reloadIndex);
        dlg.exec();
    });
    file->addAction(QStringLiteral("Quit"), qApp, &QApplication::quit);

    // The one place every export is done (Models-tab selection; each honours multi-select). Menu labels
    // show the current, rebindable shortcut so the menu and Settings ▸ Hotkeys never disagree.
    auto withKey = [](const QString& text, const QKeySequence& s) {
        return s.isEmpty() ? text : text + QStringLiteral("  (%1)").arg(s.toString(QKeySequence::NativeText)); };
    const QKeySequence seqExport = Hotkeys::seq(QStringLiteral("hotkeys/exportSelection"), QStringLiteral("Ctrl+E"));
    const QKeySequence seqSaveImg = Hotkeys::seq(QStringLiteral("hotkeys/saveImage"), QStringLiteral("Ctrl+Shift+I"));
    const QKeySequence seqTurn = Hotkeys::seq(QStringLiteral("hotkeys/turntable"), QString());
    const QKeySequence seqLoop = Hotkeys::seq(QStringLiteral("hotkeys/animLoop"), QString());
    auto* exportMenu = menuBar()->addMenu(QStringLiteral("&Export"));
    exportMenu->addAction(withKey(QStringLiteral("Export model(s)…"), seqExport), this, [this] { QMetaObject::invokeMethod(m_models, "exportSelected"); });
    exportMenu->addAction(QStringLiteral("Export selected parts…"), this, [this] { QMetaObject::invokeMethod(m_models, "exportParts"); });
    exportMenu->addSeparator();
    exportMenu->addAction(QStringLiteral("Extract original files (raw, unmodified)…"), this, [this] { QMetaObject::invokeMethod(m_models, "extractOriginals"); });
    exportMenu->addSeparator();
    exportMenu->addAction(withKey(QStringLiteral("Save preview image…"), seqSaveImg), this, [this] { QMetaObject::invokeMethod(m_models, "saveImage"); });
    exportMenu->addSeparator();
    exportMenu->addAction(withKey(QStringLiteral("Turntable GIF…"), seqTurn), this, [this] { QMetaObject::invokeMethod(m_models, "turntableGif"); });
    exportMenu->addAction(withKey(QStringLiteral("Animation-loop GIF…"), seqLoop), this, [this] { QMetaObject::invokeMethod(m_models, "animLoopGif"); });

    auto* help = menuBar()->addMenu(QStringLiteral("&Help"));
    help->addAction(QStringLiteral("Controls & shortcuts  (F1)"), this, &MainWindow::showHelp);

    // F1 opens the in-app cheat sheet from anywhere in the window.
    { auto* h = new QAction(this); h->setShortcut(QKeySequence(QKeySequence::HelpContents));
      if (h->shortcut().isEmpty()) h->setShortcut(QKeySequence(Qt::Key_F1));
      connect(h, &QAction::triggered, this, &MainWindow::showHelp); addAction(h); }

    // Export / save-image shortcuts, both user-rebindable via Settings ▸ Hotkeys and routed to the
    // Models tab so they only fire where a viewport is shown. (The menu labels above show the same keys.)
    if (!seqExport.isEmpty()) {
        auto* exp = new QAction(this); exp->setShortcut(seqExport);
        connect(exp, &QAction::triggered, this, [this] { if (m_tabs->currentWidget() == m_models) QMetaObject::invokeMethod(m_models, "exportSelected"); });
        addAction(exp);
    }
    if (!seqSaveImg.isEmpty()) {
        auto* img = new QAction(this); img->setShortcut(seqSaveImg);
        connect(img, &QAction::triggered, this, [this] { if (m_tabs->currentWidget() == m_models) QMetaObject::invokeMethod(m_models, "saveImage"); });
        addAction(img);
    }
    // GIF exports are unbound by default (template §29); a shortcut is only registered if the user set one.
    auto bindTo = [this](const QKeySequence& s, const char* slot) {
        if (s.isEmpty()) return;
        auto* a = new QAction(this); a->setShortcut(s);
        connect(a, &QAction::triggered, this, [this, slot] { if (m_tabs->currentWidget() == m_models) QMetaObject::invokeMethod(m_models, slot); });
        addAction(a);
    };
    bindTo(seqTurn, "turntableGif");
    bindTo(seqLoop, "animLoopGif");

    // Start the index build if a game folder is already configured.
    if (!Config::bundlesDir().isEmpty()) startIndex();
    else m_status->setText(QStringLiteral("Set your Path of Exile 2 folder from the File menu to begin."));
}

MainWindow::~MainWindow() = default;

void MainWindow::chooseGameFolder()
{
    const QString start = Config::gameDir().isEmpty() ? QDir::homePath() : Config::gameDir();
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Select the Path of Exile 2 install folder"), start);
    if (dir.isEmpty()) return;
    QString game = dir;
    if (!QFileInfo::exists(QDir(game).filePath(QStringLiteral("Bundles2/_.index.bin")))) {
        // Maybe they picked Bundles2 itself, or a parent.
        if (QFileInfo::exists(QDir(dir).filePath(QStringLiteral("_.index.bin")))) game = QFileInfo(dir).path();
    }
    Config::setGameDir(game);
    startIndex();
}

void MainWindow::reloadIndex() { startIndex(); }

void MainWindow::startIndex()
{
    const QString bundles = Config::bundlesDir();
    if (bundles.isEmpty() || !QFileInfo::exists(QDir(bundles).filePath(QStringLiteral("_.index.bin")))) {
        QMessageBox::warning(this, QStringLiteral("No game data"),
                             QStringLiteral("Couldn't find Bundles2/_.index.bin under the configured folder.\nSet the Path of Exile 2 folder from the File menu."));
        return;
    }
    m_status->setText(QStringLiteral("Reading the game index…"));
    m_loader->start(bundles);
}

void MainWindow::onIndexProgress(const QString& line) { m_status->setText(line); }

void MainWindow::onIndexFinished(bool ok, const QString& error)
{
    if (!ok) { m_status->setText(QStringLiteral("Index failed: %1").arg(error)); return; }
    m_models->onIndexReady();
    m_textures->onIndexReady();
    m_customize->onIndexReady();
    m_bulk->onIndexReady();
    const BundleIndex& idx = m_store->index();
    m_status->setText(QStringLiteral("Ready — %1 files in %2 bundles%3")
        .arg(idx.files().size()).arg(idx.bundles().size())
        .arg(idx.loadedFromCache() ? QStringLiteral(" (from cache)") : QString()));
}

void MainWindow::showHealthCheck()
{
    if (!m_store->isOpen()) { QMessageBox::information(this, QStringLiteral("Health check"), QStringLiteral("The index is not loaded yet.")); return; }
    const BundleIndex& idx = m_store->index();
    QStringList s;
    s << QStringLiteral("Bundles directory: %1").arg(idx.bundlesDir());
    s << QStringLiteral("Index fingerprint: %1").arg(idx.fingerprint());
    s << QStringLiteral("Total records in index: %1").arg(idx.totalRecordsInIndex());
    s << QStringLiteral("Listed (named) files: %1").arg(idx.files().size());
    s << QStringLiteral("Unnamed (path unresolved): %1").arg(idx.unnamedFiles());
    s << QStringLiteral("Files sharing a payload: %1").arg(idx.sharedPayloadFiles());
    s << QStringLiteral("Bundles: %1").arg(idx.bundles().size());
    s << QString();
    s << QStringLiteral("Not listed (shader caches, counted not indexed):");
    for (const auto& sk : idx.skippedRoots()) s << QStringLiteral("  %1 : %2").arg(sk.root).arg(sk.files);
    // A copyable, monospace report (the counts line up in columns) rather than a message box that
    // can't be selected — the same TextReport pane the material report uses (template §18).
    TextReport::show(this, QStringLiteral("Health check"), s.join(QLatin1Char('\n')));
}

void MainWindow::showHelp()
{
    // The in-app cheat sheet (template §19). Shortcuts are read from the Hotkeys registry so a rebind in
    // Settings shows up here too; the mouse controls and search syntax mirror the wiki's Keyboard & mouse
    // page — keep the two in step when either changes.
    const QString kExport  = Hotkeys::seq(QStringLiteral("hotkeys/exportSelection"), QStringLiteral("Ctrl+E")).toString(QKeySequence::NativeText);
    const QString kSaveImg = Hotkeys::seq(QStringLiteral("hotkeys/saveImage"), QStringLiteral("Ctrl+Shift+I")).toString(QKeySequence::NativeText);
    auto row = [](const QString& k, const QString& what) {
        return QStringLiteral("<tr><td style='padding-right:16px;white-space:nowrap'><b>%1</b></td><td>%2</td></tr>").arg(k.toHtmlEscaped(), what); };

    QString html;
    html += QStringLiteral("<h3>Viewport (Models tab)</h3><table>");
    html += row(QStringLiteral("Left-drag"),               QStringLiteral("Orbit the camera"));
    html += row(QStringLiteral("Mouse wheel"),             QStringLiteral("Zoom in / out"));
    html += row(QStringLiteral("Middle-drag / Alt+right-drag"), QStringLiteral("Pan"));
    html += row(QStringLiteral("Click a part"),            QStringLiteral("Select it (highlights, and mirrors into the Parts panel)"));
    html += row(QStringLiteral("Ctrl / Shift + click"),    QStringLiteral("Add or remove a part from the selection"));
    html += row(QStringLiteral("Double-click a part"),     QStringLiteral("Frame it (no selection change)"));
    html += row(QStringLiteral("Right-click a part"),      QStringLiteral("Menu: frame · isolate · export parts · copy name (show all to unhide)"));
    html += QStringLiteral("</table>");

    html += QStringLiteral("<h3>Textures tab</h3><table>");
    html += row(QStringLiteral("Mouse wheel"),   QStringLiteral("Zoom"));
    html += row(QStringLiteral("Drag"),          QStringLiteral("Pan"));
    html += row(QStringLiteral("Double-click"),  QStringLiteral("Reset the view"));
    html += QStringLiteral("</table>");

    html += QStringLiteral("<h3>Keyboard</h3><table>");
    html += row(kExport.isEmpty()  ? QStringLiteral("(unbound)") : kExport,  QStringLiteral("Export the selected model(s)"));
    html += row(kSaveImg.isEmpty() ? QStringLiteral("(unbound)") : kSaveImg, QStringLiteral("Save the current view as a PNG image"));
    html += row(QStringLiteral("Esc"), QStringLiteral("Cancel a running bulk extraction"));
    html += row(QStringLiteral("F1"),  QStringLiteral("This help"));
    html += QStringLiteral("</table><p style='color:gray'>Shortcuts are rebindable in <b>Settings ▸ Hotkeys</b>.</p>");

    html += QStringLiteral("<h3>Export menu</h3><p>All exports live in the <b>Export</b> menu — "
                           "model(s), selected parts, preview image, turntable GIF and animation-loop GIF. "
                           "Each opens its own options and honours a multi-selection in the list "
                           "(one file per model).</p>");

    html += QStringLiteral("<h3>Search box (all tabs)</h3><table>");
    html += row(QStringLiteral("space"),  QStringLiteral("AND — every term must match (<i>robe gloves</i>)"));
    html += row(QStringLiteral("-term"),  QStringLiteral("Exclude (<i>robe -drop</i>)"));
    html += row(QStringLiteral("a|b"),     QStringLiteral("OR within one term (<i>helmet|gloves</i>)"));
    html += row(QStringLiteral("a number"), QStringLiteral("Find by file id (decimal, or 0x… hash)"));
    html += QStringLiteral("</table>");

    html += QStringLiteral("<p style='color:gray'>The full manual is in the project wiki — see "
                           "<b>Keyboard &amp; mouse</b>, <b>The tabs</b> and <b>Exporting</b>.</p>");

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Controls & shortcuts"));
    dlg.resize(560, 640);
    auto* v = new QVBoxLayout(&dlg);
    auto* browser = new QTextBrowser(&dlg);
    browser->setOpenExternalLinks(false);
    browser->setHtml(html);
    v->addWidget(browser, 1);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    v->addWidget(bb);
    dlg.exec();
}
