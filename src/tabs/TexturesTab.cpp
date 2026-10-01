#include "tabs/TexturesTab.h"
#include "store/AssetStore.h"
#include "gl/GLTextureWidget.h"
#include "app/Config.h"
#include "util/ThumbnailCache.h"
#include "util/HoverPreview.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHelpEvent>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QDir>
#include <QSplitter>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

TexturesTab::TexturesTab(AssetStore* store, QWidget* parent) : QWidget(parent), m_store(store)
{
    m_model = new AssetListModel(this);

    auto* left = new QWidget(this);
    auto* lv = new QVBoxLayout(left); lv->setContentsMargins(4, 4, 4, 4);
    auto* searchRow = new QHBoxLayout();
    m_search = new QLineEdit(left);
    m_search->setPlaceholderText(QStringLiteral("Search textures  (space = AND,  -exclude,  a|b = OR)"));
    m_search->setClearButtonEnabled(true);
    searchRow->addWidget(m_search, 1);
    m_gridBtn = new QToolButton(left);
    m_gridBtn->setText(QStringLiteral("▦"));
    m_gridBtn->setCheckable(true);
    m_gridBtn->setToolTip(QStringLiteral("Toggle icon grid / detail list  (Ctrl+scroll to resize icons)"));
    searchRow->addWidget(m_gridBtn, 0);
    lv->addLayout(searchRow);
    m_list = new QTableView(left);
    m_list->setModel(m_model);
    m_list->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_list->verticalHeader()->setVisible(false);
    m_list->horizontalHeader()->setStretchLastSection(true);
    m_list->setColumnWidth(AssetListModel::ColName, 330);
    m_list->setColumnWidth(AssetListModel::ColTrueName, 190);    // item-icon in-game name
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setSortingEnabled(true);
    m_list->horizontalHeader()->setSortIndicatorShown(true);
    m_list->horizontalHeader()->setSectionsClickable(true);
    lv->addWidget(m_list, 1);

    // Icon grid (hidden until toggled). Same model + selection as the table, so switching views keeps
    // the current selection; captions are the short file name (grid mode on the model).
    m_grid = new QListView(left);
    m_grid->setModel(m_model);
    m_grid->setModelColumn(AssetListModel::ColName);
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setMovement(QListView::Static);
    // NOT uniformItemSizes — see ModelsTab: it collapses icons to thin bars when thumbnails arrive late.
    m_grid->setWordWrap(true);
    m_grid->setSpacing(8);
    m_grid->setSelectionModel(m_list->selectionModel());
    m_grid->setContextMenuPolicy(Qt::CustomContextMenu);
    m_grid->setMouseTracking(true);
    m_grid->viewport()->installEventFilter(this);   // Ctrl+scroll resize + hover dwell
    m_grid->hide();
    lv->addWidget(m_grid, 1);

    // Decode texture thumbnails off the GUI thread (a DDS decode is pure/thread-safe).
    m_thumbs = new ThumbnailCache([store = m_store](quint32 fi) -> QImage {
        const QString path = store->index().pathOf(fi);
        if (path.isEmpty()) return QImage();
        QImage img = store->loadTexture(path, nullptr, nullptr);
        if (img.isNull()) return QImage();
        return img.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }, /*background*/true, this);
    connect(m_thumbs, &ThumbnailCache::ready, this, [this](quint32 fi) { m_model->refreshIconForFile(fi); });
    m_hover = new HoverPreview(this);

    m_count = new QLabel(left); lv->addWidget(m_count);

    auto* right = new QWidget(this);
    auto* rv = new QVBoxLayout(right); rv->setContentsMargins(4, 4, 4, 4);
    auto* tool = new QHBoxLayout();
    m_channel = new QComboBox(right); m_channel->addItems({QStringLiteral("RGB"), QStringLiteral("R"), QStringLiteral("G"), QStringLiteral("B"), QStringLiteral("A")});
    m_checker = new QCheckBox(QStringLiteral("Alpha checker"), right);
    tool->addWidget(new QLabel(QStringLiteral("Channel:"))); tool->addWidget(m_channel);
    tool->addWidget(m_checker); tool->addStretch(1);
    auto* save = new QPushButton(QStringLiteral("Save PNG…"), right);
    tool->addWidget(save);
    rv->addLayout(tool);
    m_view = new GLTextureWidget(right); m_view->setMinimumSize(360, 360);
    rv->addWidget(m_view, 1);
    m_info = new QLabel(right); m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rv->addWidget(m_info);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(left); split->addWidget(right); split->setSizes({440, 700});
    auto* outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0); outer->addWidget(split);

    m_debounce = new QTimer(this); m_debounce->setSingleShot(true); m_debounce->setInterval(180);
    connect(m_debounce, &QTimer::timeout, this, &TexturesTab::applyFilter);
    connect(m_search, &QLineEdit::textChanged, this, [this] { m_debounce->start(); });
    connect(m_list, &QTableView::activated, this, &TexturesTab::onActivated);
    connect(m_grid, &QListView::activated, this, &TexturesTab::onActivated);
    // Load on selection (mouse or arrow keys), debounced so rapid scrubbing decodes only the settle.
    m_loadDebounce = new QTimer(this); m_loadDebounce->setSingleShot(true); m_loadDebounce->setInterval(110);
    connect(m_loadDebounce, &QTimer::timeout, this, &TexturesTab::loadCurrentRow);
    connect(m_list->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex& cur, const QModelIndex&) { if (cur.isValid()) m_loadDebounce->start(); });
    // Grid toggle + its shared context menu.
    connect(m_gridBtn, &QToolButton::toggled, this, [this](bool on) { setGridView(on); });
    connect(m_grid, &QListView::customContextMenuRequested, this, [this](const QPoint& p) {
        const QModelIndex idx = m_grid->indexAt(p);
        if (idx.isValid()) showContextMenu(m_model->pathAt(idx.row()), m_grid->viewport()->mapToGlobal(p));
    });
    connect(m_channel, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) { m_view->setChannel(GLTextureWidget::Channel(i)); });
    connect(m_checker, &QCheckBox::toggled, this, [this](bool on) { m_view->setCheckerboard(on); });
    connect(m_view, &GLTextureWidget::hoverPixel, this, [this](QPoint px, QColor c) {
        if (px.x() < 0) return;
        emit status(QStringLiteral("(%1, %2)  RGBA %3 %4 %5 %6").arg(px.x()).arg(px.y()).arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha()));
    });
    connect(m_list, &QTableView::customContextMenuRequested, this, [this](const QPoint& p) {
        const QModelIndex idx = m_list->indexAt(p);
        if (idx.isValid()) showContextMenu(m_model->pathAt(idx.row()), m_list->viewport()->mapToGlobal(p));
    });
    connect(save, &QPushButton::clicked, this, [this] {
        if (!m_view->hasImage()) return;
        savePng(m_lastTex);
    });
}

// Decode a .dds and write it out as a full-RGBA PNG. Shared by the Save PNG button and the context menu.
void TexturesTab::savePng(const QString& ddsPath)
{
    if (ddsPath.isEmpty()) { emit status(QStringLiteral("No texture selected.")); return; }
    const QString base = Config::lastExportDir().isEmpty() ? QDir::homePath() : Config::lastExportDir();
    const QString suggested = QDir(base).filePath(QFileInfo(ddsPath).baseName() + QStringLiteral(".png"));
    QString out = QFileDialog::getSaveFileName(this, QStringLiteral("Save PNG"), suggested, QStringLiteral("PNG (*.png)"));
    if (out.isEmpty()) return;
    if (!out.toLower().endsWith(QStringLiteral(".png"))) out += QStringLiteral(".png");   // else QImage::save can't infer the format
    Config::setLastExportDir(QFileInfo(out).absolutePath());
    DdsImage::Info info;
    const QImage img = m_store->loadTexture(ddsPath, nullptr, &info);
    if (img.isNull()) { emit status(QStringLiteral("Couldn't decode the texture to save.")); return; }
    if (img.save(out, "PNG")) emit status(QStringLiteral("Saved %1").arg(QFileInfo(out).fileName()));
    else                      emit status(QStringLiteral("Failed to save %1").arg(QFileInfo(out).fileName()));
}

// Write the EXACT original .dds bytes (and its sibling .dds.header if present) — no decode, no convert.
// This is the texture in its original condition, matching the raw model extraction elsewhere.
void TexturesTab::saveOriginalDds(const QString& ddsPath)
{
    if (ddsPath.isEmpty()) { emit status(QStringLiteral("No texture selected.")); return; }
    const QByteArray dds = m_store->readFile(ddsPath, nullptr);
    if (dds.isEmpty()) { emit status(QStringLiteral("Couldn't read the original texture.")); return; }
    const QString base = Config::lastExportDir().isEmpty() ? QDir::homePath() : Config::lastExportDir();
    const QString suggested = QDir(base).filePath(QFileInfo(ddsPath).fileName());
    QString out = QFileDialog::getSaveFileName(this, QStringLiteral("Save original texture"), suggested, QStringLiteral("DDS texture (*.dds)"));
    if (out.isEmpty()) return;
    if (!out.toLower().endsWith(QStringLiteral(".dds"))) out += QStringLiteral(".dds");
    Config::setLastExportDir(QFileInfo(out).absolutePath());
    QFile f(out);
    if (!f.open(QIODevice::WriteOnly) || f.write(dds) != dds.size()) {
        emit status(QStringLiteral("Failed to write %1").arg(QFileInfo(out).fileName())); return;
    }
    f.close();
    // The .header sidecar carries the real dimensions/format; write it beside the .dds so the pair is
    // self-describing, exactly as it is in the game data.
    const QByteArray hdr = m_store->readFile(ddsPath + QStringLiteral(".header"), nullptr);
    int extra = 0;
    if (!hdr.isEmpty()) {
        QFile hf(out + QStringLiteral(".header"));
        if (hf.open(QIODevice::WriteOnly) && hf.write(hdr) == hdr.size()) extra = 1;
    }
    emit status(QStringLiteral("Saved original %1%2").arg(QFileInfo(out).fileName(),
                extra ? QStringLiteral(" (+ .header)") : QString()));
}

void TexturesTab::onIndexReady()
{
    m_model->setIndex(m_store->isOpen() ? &m_store->index() : nullptr, {QStringLiteral(".dds")});
    applyFilter();
}

void TexturesTab::onMaterialsReady()
{
    m_model->setNameIndex(m_store->isNameIndexReady() ? &m_store->nameIndex() : nullptr);
    applyFilter();
}

void TexturesTab::applyFilter()
{
    m_model->setQuery(m_search->text());
    m_count->setText(QStringLiteral("%1 of %2 textures").arg(m_model->rowCount()).arg(m_model->totalInBaseSet()));
}

void TexturesTab::onActivated(const QModelIndex& idx) { if (idx.isValid()) loadTexture(m_model->pathAt(idx.row())); }

void TexturesTab::loadCurrentRow()
{
    const QModelIndex cur = m_list->selectionModel() ? m_list->selectionModel()->currentIndex() : m_list->currentIndex();
    if (!cur.isValid()) return;
    const QString path = m_model->pathAt(cur.row());
    if (path.isEmpty() || path == m_lastTex) return;
    loadTexture(path);
}

void TexturesTab::loadTexture(const QString& path)
{
    if (path.isEmpty() || !m_store->isOpen()) return;
    m_lastTex = path;
    QString err; DdsImage::Info info;
    const QImage img = m_store->loadTexture(path, &err, &info);
    if (img.isNull()) { emit status(QStringLiteral("decode failed: %1").arg(err)); m_info->setText(err); return; }
    m_view->setImage(img);
    m_info->setText(QStringLiteral("%1   %2×%3   %4   mips %5")
        .arg(path.section(QLatin1Char('/'), -1)).arg(info.width).arg(info.height).arg(info.codecName).arg(info.mipCount));
}

// The shared right-click menu, used by both the table and the grid.
void TexturesTab::showContextMenu(const QString& path, const QPoint& globalPos)
{
    if (path.isEmpty()) return;
    QMenu menu(this);
    menu.addAction(QStringLiteral("Preview"), [this, path] { loadTexture(path); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Save as PNG…"),          [this, path] { savePng(path); });
    menu.addAction(QStringLiteral("Save original (.dds)…"), [this, path] { saveOriginalDds(path); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Copy path"), [this, path] { qApp->clipboard()->setText(path); });
    menu.addAction(QStringLiteral("Copy name"), [this, path] { qApp->clipboard()->setText(QFileInfo(path).fileName()); });
    menu.exec(globalPos);
}

// Toggle between the detail table and the icon grid. The grid installs the thumbnail provider (which
// decodes lazily); the table clears it so list mode doesn't decode every visible row.
void TexturesTab::setGridView(bool on)
{
    m_model->setGridMode(on);
    if (on) {
        m_model->setIconProvider([this](quint32 fi) { return m_thumbs->get(fi); });
        setIconPx(m_iconPx);
        m_list->hide(); m_grid->show(); m_grid->setFocus();
    } else {
        m_model->setIconProvider(nullptr);
        m_grid->hide(); m_list->show(); m_list->setFocus();
    }
    if (m_gridBtn->isChecked() != on) { QSignalBlocker b(m_gridBtn); m_gridBtn->setChecked(on); }
}

void TexturesTab::setIconPx(int px)
{
    m_iconPx = qBound(48, px, 320);
    m_model->setIconPx(m_iconPx);
    m_grid->setIconSize(QSize(m_iconPx, m_iconPx));
    m_grid->setGridSize(QSize(m_iconPx + 16, m_iconPx + 52));   // icon + a caption line
}

bool TexturesTab::eventFilter(QObject* obj, QEvent* ev)
{
    const bool onGrid = (m_grid && obj == m_grid->viewport());
    if (onGrid && ev->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(ev);
        const int dy = we->angleDelta().y();
        if (we->modifiers() & Qt::ControlModifier) { setIconPx(m_iconPx + (dy > 0 ? 12 : -12)); return true; }
        if (m_hover->isShowing()) { m_hover->stepSize(dy > 0 ? 40 : -40); return true; }  // resize the preview
        m_hover->hidePreview();
        return false;
    }
    // Dwell → a richer hover preview than the plain path tooltip (uses Qt's own tooltip timing). The
    // source is the actual decoded texture, so it stays crisp as the user scrolls to enlarge it.
    if (onGrid && ev->type() == QEvent::ToolTip) {
        auto* he = static_cast<QHelpEvent*>(ev);
        const QModelIndex idx = m_grid->indexAt(m_grid->viewport()->mapFromGlobal(he->globalPos()));
        if (idx.isValid()) {
            const QString path = m_model->pathAt(idx.row());
            DdsImage::Info info;
            const QImage img = m_store->loadTexture(path, nullptr, &info);
            const QPixmap pm = img.isNull() ? m_thumbs->get(m_model->fileIndexAt(idx.row())) : QPixmap::fromImage(img);
            const QString html = QStringLiteral(
                "<b>%1</b><br><span style='color:#9a9a9a'>%2×%3 · %4</span>")
                .arg(QFileInfo(path).fileName()).arg(info.width).arg(info.height).arg(info.codecName);
            m_hover->showFor(pm, html, he->globalPos());
            return true;
        }
    }
    if (onGrid && ev->type() == QEvent::Leave) m_hover->hidePreview();
    return QWidget::eventFilter(obj, ev);
}
