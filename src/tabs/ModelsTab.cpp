#include "tabs/ModelsTab.h"
#include "store/AssetStore.h"
#include "gl/GLModelWidget.h"
#include "gl/ModelThumbnailRenderer.h"
#include "model/GlbExporter.h"
#include "report/MaterialReport.h"
#include "index/FunnelFilter.h"
#include "app/Config.h"
#include "app/ExportConfig.h"
#include "util/NameTemplate.h"
#include "util/ThumbnailCache.h"
#include "util/HoverPreview.h"

#include <QListView>
#include <QEvent>
#include <QHelpEvent>
#include <QWheelEvent>

#include <QSettings>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QItemSelectionModel>
#include <QSet>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include "app/ExportOptionsDialog.h"
#include "app/ImageExportDialog.h"
#include "app/GifExportDialog.h"
#include "app/ExportCapture.h"
#include "app/ExportNotifier.h"
#include "util/TextReportDialog.h"
#include "util/HintBar.h"
#include <QImage>
#include <QProgressDialog>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

ModelsTab::ModelsTab(AssetStore* store, QWidget* parent) : QWidget(parent), m_store(store)
{
    m_model = new AssetListModel(this);

    // ── Left: search + facet + list ───────────────────────────────────────────────────────────
    auto* left = new QWidget(this);
    auto* lv = new QVBoxLayout(left); lv->setContentsMargins(4, 4, 4, 4);
    auto* searchRow = new QHBoxLayout();
    m_search = new QLineEdit(left);
    m_search->setPlaceholderText(QStringLiteral("Search path or in-game name  (space = AND,  -exclude,  a|b = OR,  #workflow:/#family:/#effect:,  digits or 0x-hex = id)"));
    m_search->setClearButtonEnabled(true);
    m_extFacet = new QComboBox(left);
    m_extFacet->addItem(QStringLiteral("all models"), QString());
    m_extFacet->addItem(QStringLiteral(".smd"), QStringLiteral(".smd"));
    m_extFacet->addItem(QStringLiteral(".fmt"), QStringLiteral(".fmt"));
    m_funnel = new FunnelFilter(left);
    m_gridBtn = new QToolButton(left);
    m_gridBtn->setText(QStringLiteral("▦")); m_gridBtn->setCheckable(true);
    m_gridBtn->setToolTip(QStringLiteral("Toggle icon grid / detail list  (Ctrl+scroll to resize icons)"));
    searchRow->addWidget(m_search, 1); searchRow->addWidget(m_extFacet); searchRow->addWidget(m_funnel); searchRow->addWidget(m_gridBtn);
    lv->addLayout(searchRow);
    lv->addWidget(m_funnel->chipBar());
    m_list = new QTableView(left);
    m_list->setModel(m_model);
    m_list->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->verticalHeader()->setVisible(false);
    m_list->horizontalHeader()->setStretchLastSection(true);
    m_list->setColumnWidth(AssetListModel::ColName, 340);
    m_list->setColumnWidth(AssetListModel::ColTrueName, 190);   // the true in-game name
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setSortingEnabled(true);                       // click a header to sort (Path/Type/Size/Bundle)
    m_list->horizontalHeader()->setSortIndicatorShown(true);
    m_list->horizontalHeader()->setSectionsClickable(true);
    lv->addWidget(m_list, 1);

    // Icon grid (hidden until toggled): same model + selection as the table.
    m_gridView = new QListView(left);
    m_gridView->setModel(m_model);
    m_gridView->setModelColumn(AssetListModel::ColName);
    m_gridView->setViewMode(QListView::IconMode);
    m_gridView->setResizeMode(QListView::Adjust);
    m_gridView->setMovement(QListView::Static);
    // NOT uniformItemSizes: it caches the tile size from the first item while its thumbnail is still the
    // blank placeholder, which collapses every icon to a thin bar once the real renders arrive.
    m_gridView->setWordWrap(true);
    m_gridView->setSpacing(8);
    m_gridView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_gridView->setSelectionModel(m_list->selectionModel());
    m_gridView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_gridView->setMouseTracking(true);
    m_gridView->viewport()->installEventFilter(this);
    m_gridView->hide();
    lv->addWidget(m_gridView, 1);

    // Thumbnail rendering: models draw as icons offscreen, on the GUI thread (a GL context is
    // thread-bound), so the cache runs in GUI mode with a small per-tick budget to stay responsive.
    m_texturedIcons = QSettings().value(QStringLiteral("grid/texturedIcons"), false).toBool();
    m_thumbs = new ThumbnailCache([this](quint32 fi) -> QImage {
        if (!m_thumbRenderer) m_thumbRenderer = new ModelThumbnailRenderer(192);
        const QString path = m_store->index().pathOf(fi);
        if (path.isEmpty()) return QImage();
        ModelGeometry geo;
        if (!m_store->loadModel(path, geo, nullptr)) return QImage();
        if (m_texturedIcons) {
            const QVector<GlbExporter::ExportMaterial> mats = m_store->resolveMaterials(geo, /*decode*/true);
            QVector<QImage> base(geo.materialPaths.size());
            for (int i = 0; i < mats.size() && i < base.size(); ++i) base[i] = mats[i].baseColor;
            return m_thumbRenderer->render(geo, &base);
        }
        return m_thumbRenderer->render(geo);
    }, /*background*/false, this);
    m_thumbs->setGuiBudget(1);              // one model render per tick — heavy work, keep the UI live
    connect(m_thumbs, &ThumbnailCache::ready, this, [this](quint32 fi) { m_model->refreshIconForFile(fi); });
    m_hover = new HoverPreview(this);

    // Grid options menu (base-colour vs flat icons) on the grid button's dropdown.
    {
        auto* gm = new QMenu(m_gridBtn);
        auto* tex = gm->addAction(QStringLiteral("Textured (base-colour) icons"));
        tex->setCheckable(true); tex->setChecked(m_texturedIcons);
        connect(tex, &QAction::toggled, this, [this](bool on) {
            m_texturedIcons = on;
            QSettings().setValue(QStringLiteral("grid/texturedIcons"), on);
            m_thumbs->clear();                 // re-render every thumbnail in the new mode
            m_model->refreshAllIcons();
        });
        m_gridBtn->setMenu(gm);
        m_gridBtn->setPopupMode(QToolButton::MenuButtonPopup);   // click toggles grid, arrow opens options
    }

    m_count = new QLabel(left); lv->addWidget(m_count);

    // ── Right: viewport + toolbar + panels ────────────────────────────────────────────────────
    auto* right = new QWidget(this);
    auto* rv = new QVBoxLayout(right); rv->setContentsMargins(4, 4, 4, 4);
    auto* tool = new QHBoxLayout();
    m_shading = new QComboBox(right); m_shading->addItems({QStringLiteral("Flat"), QStringLiteral("Shaded"), QStringLiteral("Wireframe")}); m_shading->setCurrentIndex(1);
    m_channel = new QComboBox(right); m_channel->addItems({QStringLiteral("Base Colour"), QStringLiteral("Normal"), QStringLiteral("Roughness"), QStringLiteral("Metallic"), QStringLiteral("AO"), QStringLiteral("Emissive"), QStringLiteral("Flat")});
    m_overlaysOn = new QCheckBox(QStringLiteral("Overlays"), right); m_overlaysOn->setChecked(true);
    m_grid = new QCheckBox(QStringLiteral("Grid"), right); m_grid->setChecked(true);
    m_skeleton = new QCheckBox(QStringLiteral("Skeleton"), right);
    tool->addWidget(new QLabel(QStringLiteral("Shading:"))); tool->addWidget(m_shading);
    tool->addWidget(new QLabel(QStringLiteral("Channel:"))); tool->addWidget(m_channel);
    tool->addStretch(1);
    tool->addWidget(m_overlaysOn); tool->addWidget(m_grid); tool->addWidget(m_skeleton);
    rv->addLayout(tool);
    m_view = new GLModelWidget(right);
    m_view->setMinimumSize(360, 360);
    rv->addWidget(m_view, 1);

    // ── Animation bar (clip selector · play/pause · scrub) — hidden until a skinned clip exists ──
    m_animBar = new QWidget(right);
    auto* ab = new QHBoxLayout(m_animBar); ab->setContentsMargins(0, 0, 0, 0);
    m_clipBox = new QComboBox(m_animBar);
    m_playBtn = new QToolButton(m_animBar); m_playBtn->setText(QStringLiteral("▶")); m_playBtn->setCheckable(true); m_playBtn->setToolTip(QStringLiteral("Play / pause"));
    m_timeline = new QSlider(Qt::Horizontal, m_animBar); m_timeline->setRange(0, 1000);
    m_timeLbl = new QLabel(QStringLiteral("0.00 / 0.00s"), m_animBar);
    ab->addWidget(new QLabel(QStringLiteral("Clip:"))); ab->addWidget(m_clipBox);
    ab->addWidget(m_playBtn); ab->addWidget(m_timeline, 1); ab->addWidget(m_timeLbl);
    m_animBar->setVisible(false);
    rv->addWidget(m_animBar);

    // ── Four separate panels, as tabs below the viewport: Parts · Animations · Attachments · Info ──
    m_panelTabs = new QTabWidget(right);
    m_panelTabs->setMinimumHeight(150);

    m_parts = new QTableWidget(0, 3, m_panelTabs);
    m_parts->setHorizontalHeaderLabels({QStringLiteral("Part"), QStringLiteral("Tris"), QStringLiteral("Material")});
    m_parts->horizontalHeader()->setStretchLastSection(true);
    m_parts->verticalHeader()->setVisible(false);
    m_parts->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_parts->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // Animations tab: a "Move" selector (this model's own clips, or a player-animation-library move)
    // above the clip list. The selector is shown only when the loaded mesh is on the player base rig.
    auto* animPanel = new QWidget(m_panelTabs);
    auto* apl = new QVBoxLayout(animPanel); apl->setContentsMargins(6, 6, 6, 6); apl->setSpacing(4);
    m_animSourceRow = new QWidget(animPanel);
    auto* srcLay = new QHBoxLayout(m_animSourceRow); srcLay->setContentsMargins(0, 0, 0, 0);
    srcLay->addWidget(new QLabel(QStringLiteral("Move:"), m_animSourceRow));
    m_animSourceBox = new QComboBox(m_animSourceRow);
    m_animSourceBox->setToolTip(QStringLiteral("Play this model's own clips, or a player move set (sprint, weapon attacks, "
                                               "skills…). Shown only for meshes on the player base rig."));
    srcLay->addWidget(m_animSourceBox, 1);
    m_animSourceRow->setVisible(false);
    apl->addWidget(m_animSourceRow);
    m_clipList = new QListWidget(animPanel);
    m_clipList->setToolTip(QStringLiteral("Animation clips of the selected source — click one to play it"));
    apl->addWidget(m_clipList, 1);

    m_attachList = new QListWidget(m_panelTabs);
    m_attachList->setToolTip(QStringLiteral("Attached objects this body declares (coat, hat, weapons…) — tick to show each one placed on its bone"));

    m_info = new QLabel(m_panelTabs); m_info->setAlignment(Qt::AlignTop | Qt::AlignLeft); m_info->setWordWrap(true); m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_info->setMargin(6);
    auto* infoScroll = new QScrollArea(m_panelTabs); infoScroll->setWidget(m_info); infoScroll->setWidgetResizable(true); infoScroll->setFrameShape(QFrame::NoFrame);

    m_panelTabs->addTab(m_parts,      QStringLiteral("Parts"));
    m_panelTabs->addTab(animPanel,    QStringLiteral("Animations"));
    m_panelTabs->addTab(m_attachList, QStringLiteral("Attachments"));
    m_panelTabs->addTab(infoScroll,   QStringLiteral("Info"));
    rv->addWidget(m_panelTabs);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(left); split->addWidget(right); split->setSizes({420, 720});
    auto* outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0);
    // First-run tip teaching the otherwise-invisible viewport controls (template §23). Dismiss with ✕
    // and it never returns.
    if (QWidget* hint = makeHintBar(this,
            QStringLiteral("Tip: click a part to select it · drag to orbit, wheel to zoom · press F1 for all controls · everything you can export is in the Export menu."),
            "hints/modelsViewport"))
        outer->addWidget(hint, 0);           // thin, fixed-height strip
    outer->addWidget(split, 1);              // the list + viewport take all remaining height

    // ── Wiring ────────────────────────────────────────────────────────────────────────────────
    m_debounce = new QTimer(this); m_debounce->setSingleShot(true); m_debounce->setInterval(180);
    connect(m_debounce, &QTimer::timeout, this, &ModelsTab::applyFilter);
    connect(m_search, &QLineEdit::textChanged, this, [this] { m_debounce->start(); });

    // Selecting a row (mouse or arrow keys) loads it — a short debounce coalesces rapid arrow-key
    // scrubbing so we don't decode every model passed over, only the one you settle on.
    m_loadDebounce = new QTimer(this); m_loadDebounce->setSingleShot(true); m_loadDebounce->setInterval(110);
    connect(m_loadDebounce, &QTimer::timeout, this, &ModelsTab::loadCurrentRow);
    connect(m_list->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex& cur, const QModelIndex&) { if (cur.isValid()) m_loadDebounce->start(); });
    connect(m_extFacet, qOverload<int>(&QComboBox::currentIndexChanged), this, &ModelsTab::applyFilter);
    // Restore the persisted facet selection, then re-filter whenever it changes.
    m_funnel->setActive(QSettings().value(QStringLiteral("facets/models")).toStringList(),
                        QSettings().value(QStringLiteral("facets/modelsMatchAny")).toBool());
    connect(m_funnel, &FunnelFilter::changed, this, [this] {
        QSettings().setValue(QStringLiteral("facets/models"), m_funnel->activeIds());
        QSettings().setValue(QStringLiteral("facets/modelsMatchAny"), m_funnel->matchAny());
        applyFilter();
    });
    connect(m_list, &QTableView::activated, this, &ModelsTab::onListActivated);
    connect(m_gridView, &QListView::activated, this, &ModelsTab::onListActivated);
    connect(m_list, &QTableView::customContextMenuRequested, this, [this](const QPoint& p) {
        const QModelIndex idx = m_list->indexAt(p);
        if (idx.isValid()) showRowContextMenu(idx.row(), m_list->viewport()->mapToGlobal(p));
    });
    connect(m_gridView, &QListView::customContextMenuRequested, this, [this](const QPoint& p) {
        const QModelIndex idx = m_gridView->indexAt(p);
        if (idx.isValid()) showRowContextMenu(idx.row(), m_gridView->viewport()->mapToGlobal(p));
    });
    connect(m_gridBtn, &QToolButton::toggled, this, [this](bool on) { setGridView(on); });
    connect(m_shading, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) { m_view->setShading(GLModelWidget::Shading(i)); });
    connect(m_channel, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) { m_view->setChannel(GLModelWidget::Channel(i)); });
    connect(m_overlaysOn, &QCheckBox::toggled, this, [this](bool on) { m_view->setOverlaysOn(on); });
    connect(m_grid, &QCheckBox::toggled, this, [this](bool on) { m_view->setShowGrid(on); });
    connect(m_skeleton, &QCheckBox::toggled, this, [this](bool on) { m_view->setShowSkeleton(on); });
    connect(m_view, &GLModelWidget::selectionChanged, this, &ModelsTab::onViewportSelectionChanged);
    connect(m_view, &GLModelWidget::viewportPartMenuRequested, this, &ModelsTab::showViewportPartMenu);
    connect(m_view, &GLModelWidget::partDoubleClicked, this, [this](int) { m_view->frameSelected(); });
    connect(m_view, &GLModelWidget::statsText, this, [this](const QString& s) { emit status(s); });
    connect(m_parts, &QTableWidget::itemSelectionChanged, this, &ModelsTab::onPartsSelectionChanged);

    // Animation bar wiring. Row 0 is "(bind pose)"; the clip combo and the Animations list stay in
    // lock-step and both drive playback through playClipRow (which handles library-move retargeting).
    connect(m_clipBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (i < 0) return;
        playClipRow(i);
    });
    connect(m_animSourceBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int src) {
        if (src < 0) return;
        onAnimSourceChanged(src);
    });
    connect(m_playBtn, &QToolButton::toggled, this, [this](bool on) {
        m_view->setPlaying(on); m_playBtn->setText(on ? QStringLiteral("❚❚") : QStringLiteral("▶"));
    });
    connect(m_timeline, &QSlider::sliderMoved, this, [this](int v) {
        const float dur = m_view->clipDuration();
        if (dur > 0) { m_playBtn->setChecked(false); m_view->setAnimTime(dur * v / 1000.0f); }
    });
    connect(m_view, &GLModelWidget::animTimeChanged, this, [this](float t, float dur) {
        m_syncingTimeline = true;
        if (dur > 0 && !m_timeline->isSliderDown()) m_timeline->setValue(int(t / dur * 1000.0f));
        m_timeLbl->setText(QStringLiteral("%1 / %2s").arg(t, 0, 'f', 2).arg(dur, 0, 'f', 2));
        m_syncingTimeline = false;
    });
    // Animations tab list ↔ clip bar: a row is a clip (row 0 = bind pose); selecting it plays it.
    connect(m_clipList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) return;
        playClipRow(row);
    });
    // Attachments tab: ticking a row shows/hides that piece in the assembled view.
    connect(m_attachList, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
        const int idx = it->data(Qt::UserRole).toInt();
        if (idx >= 0) m_view->setAttachmentVisible(idx, it->checkState() == Qt::Checked);
    });
}

void ModelsTab::refreshAnimBar()
{
    const bool haveOwn = !m_view->clipNames().isEmpty();
    // The bar shows when there are own clips OR the move selector is available (player rig).
    m_animBar->setVisible(haveOwn || m_rigIsPlayer);
    // A skinned mesh whose rig we couldn't match (e.g. body armour skinned to a character skeleton
    // it does not carry, docs/FORMATS.md §7) shows at bind pose — say so rather than silently hiding.
    if (m_view->isSkinnedMesh() && !m_view->skeletonMatchesMesh())
        emit status(QStringLiteral("skinned mesh shown at bind pose — its rig is a character skeleton not carried by this asset (no clip playback)"));
    if (m_animSourceRow) m_animSourceRow->setVisible(m_rigIsPlayer);
    if (m_animSourceBox) { QSignalBlocker b(m_animSourceBox); m_animSourceBox->setCurrentIndex(0); }
    m_libHeader = AstSkeleton::Skeleton{};
    m_playBtn->setChecked(false); m_playBtn->setText(QStringLiteral("▶"));
    m_timeline->setValue(0);
    m_timeLbl->setText(QStringLiteral("0.00 / 0.00s"));
    populateClipWidgets();
}

// Fill both the clip combo (anim bar) and the Animations list from the current move source: source 0
// is the model's own clips (from the viewport), source i>0 is the library move m_libHeader. Row 0 is
// always "(bind pose)". The two widgets stay in lock-step through playClipRow.
void ModelsTab::populateClipWidgets()
{
    if (!m_clipList || !m_clipBox) return;
    const int src = m_animSourceBox ? m_animSourceBox->currentIndex() : 0;

    QStringList clips;
    if (src <= 0) {
        clips = m_view->clipNames();
    } else {
        for (const AstSkeleton::Clip& c : m_libHeader.clips)
            clips << (c.name.isEmpty() ? QStringLiteral("clip") : c.name);
    }

    QSignalBlocker bb(m_clipBox), bl(m_clipList);
    m_clipBox->clear(); m_clipList->clear();
    if (clips.isEmpty()) {
        m_clipList->addItem(src > 0 ? QStringLiteral("(move has no clips)")
                            : (m_view->isSkinnedMesh() ? QStringLiteral("(no playable clips — rig not carried by this asset)")
                                                       : QStringLiteral("(static mesh — no animation)")));
        m_clipList->setEnabled(false);
        m_panelTabs->setTabText(1, QStringLiteral("Animations"));
        return;
    }
    m_clipBox->addItem(QStringLiteral("(bind pose)"));
    m_clipBox->addItems(clips);
    m_clipBox->setCurrentIndex(0);
    m_clipList->setEnabled(true);
    m_clipList->addItem(QStringLiteral("(bind pose)"));
    for (const QString& n : clips) m_clipList->addItem(n);
    m_clipList->setCurrentRow(0);
    m_panelTabs->setTabText(1, QStringLiteral("Animations (%1)").arg(clips.size()));
}

// Fill the move selector once: "(this model)" + every player-animation-library move. Only reached for
// a player-rig mesh, so building the catalogue (a one-time index scan) is paid lazily on first need.
void ModelsTab::populateMoveSources()
{
    if (!m_animSourceBox || m_animSourceBox->count() > 0) return;
    m_animCats = m_store->playerAnimCategories();
    QSignalBlocker b(m_animSourceBox);
    m_animSourceBox->addItem(QStringLiteral("(this model)"));
    for (const AssetStore::AnimCategory& c : m_animCats) m_animSourceBox->addItem(c.name);
}

// Move source changed: for a library move, read its header (clip names only); restore the model's own
// clips to the viewport if a library clip was active; then relist and show the bind pose.
void ModelsTab::onAnimSourceChanged(int src)
{
    if (src > 0 && src - 1 < m_animCats.size()) {
        m_libHeader = m_store->playerAnimHeader(m_animCats[src - 1].path);
        if (!m_libHeader.valid)
            emit status(QStringLiteral("Couldn't read move ‘%1’.").arg(m_animCats[src - 1].name));
    } else {
        m_libHeader = AstSkeleton::Skeleton{};
    }
    if (m_libActive) { m_view->setClips(m_skel.clips); m_libActive = false; }
    m_view->setClip(-1);
    m_playBtn->setChecked(false); m_playBtn->setText(QStringLiteral("▶"));
    populateClipWidgets();
}

// Play clip `row` (0 = bind pose) from the current source. A library clip is decoded on demand and
// retargeted onto the loaded rig by bone name, then swapped into the viewport via setClips.
void ModelsTab::playClipRow(int row)
{
    if (row < 0) return;
    { QSignalBlocker b1(m_clipBox);  m_clipBox->setCurrentIndex(row); }
    { QSignalBlocker b2(m_clipList); m_clipList->setCurrentRow(row); }

    const int src = m_animSourceBox ? m_animSourceBox->currentIndex() : 0;
    if (src <= 0) {
        if (m_libActive) { m_view->setClips(m_skel.clips); m_libActive = false; }
        m_view->setClip(row - 1);
    } else if (row == 0) {
        if (m_libActive) { m_view->setClips(m_skel.clips); m_libActive = false; }
        m_view->setClip(-1);
    } else {
        const int ci = row - 1;
        if (src - 1 >= m_animCats.size() || ci >= m_libHeader.clips.size()) return;
        AstSkeleton::Skeleton lib = m_store->playerAnimClip(m_animCats[src - 1].path, ci);
        if (ci >= lib.clips.size() || lib.clips[ci].keys.isEmpty()) {
            emit status(QStringLiteral("That clip has no motion data.")); return;
        }
        AstSkeleton::Clip rc = AstSkeleton::retargetClip(lib, lib.clips[ci], m_skel);
        if (rc.keys.isEmpty()) { emit status(QStringLiteral("That move doesn't match this rig.")); return; }
        m_view->setClips(QVector<AstSkeleton::Clip>{rc});
        m_libActive = true;
        m_view->setClip(0);
    }

    const bool play = row > 0;
    m_playBtn->setChecked(play);
    m_view->setPlaying(play);
    m_playBtn->setText(play ? QStringLiteral("❚❚") : QStringLiteral("▶"));
}

// Populate the Attachments tab for the current model. Filled in by the assembly stage (the attachment
// index reverse-maps this body mesh to its .ao's AttachedAnimatedObject list); until then it reports
// discovery state so the tab is never a silent blank.
void ModelsTab::refreshAttachments()
{
    if (!m_attachList) return;
    QSignalBlocker b(m_attachList);
    m_attachList->clear();

    // Discover this body's attachments and build a drawable set for the viewport (each piece's mesh +
    // materials placed on its parent bone). A piece whose mesh could not be resolved is listed but not
    // drawn (so the panel is honest about what it found vs. what it could render).
    const AssetStore::Assembly asmbl = m_store->attachmentsForModel(m_currentPath);
    QVector<GLModelWidget::Attachment> draw;
    int drawable = 0;
    for (const AssetStore::AttachmentPiece& p : asmbl.pieces) {
        auto* item = new QListWidgetItem(m_attachList);
        item->setText(QStringLiteral("%1   ·   on %2%3").arg(p.label, p.bone,
                      p.smdPath.isEmpty() ? QStringLiteral("   (mesh unresolved)") : QString()));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        if (p.smdPath.isEmpty()) {
            item->setCheckState(Qt::Unchecked);
            item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
            item->setData(Qt::UserRole, -1);
            continue;
        }
        ModelGeometry g; QString err;
        if (!m_store->loadModel(p.smdPath, g, &err)) { item->setText(item->text() + QStringLiteral("  (load failed)")); item->setData(Qt::UserRole, -1); continue; }
        const QVector<GlbExporter::ExportMaterial> mats = m_store->resolveMaterials(g, /*decodeTextures*/true);
        GLModelWidget::Attachment att; att.geo = g; att.bone = p.bone; att.label = p.label; att.visible = true;
        att.mats.resize(mats.size());
        for (int i = 0; i < mats.size(); ++i) {
            att.mats[i].baseColor = mats[i].baseColor;   att.mats[i].normal = mats[i].normal;
            att.mats[i].metalRough = mats[i].metallicRoughness; att.mats[i].emissive = mats[i].emissive;
            att.mats[i].specColor = mats[i].specularColor;
            for (int c = 0; c < 3; ++c) att.mats[i].subsurface[c] = mats[i].subsurface[c];
            att.mats[i].translucent = mats[i].transmissionFactor; att.mats[i].alphaMode = mats[i].alphaMode;
        }
        item->setData(Qt::UserRole, draw.size());   // maps this row → attachment index in the viewport
        item->setCheckState(Qt::Checked);
        draw.append(att);
        ++drawable;
    }
    m_view->setAttachments(draw);

    if (asmbl.pieces.isEmpty()) {
        auto* it = new QListWidgetItem(QStringLiteral("(no attachments declared for this model)"), m_attachList);
        it->setFlags(it->flags() & ~Qt::ItemIsUserCheckable);
        m_panelTabs->setTabText(2, QStringLiteral("Attachments"));
    } else {
        m_panelTabs->setTabText(2, QStringLiteral("Attachments (%1)").arg(drawable));
    }
    m_attachList->setEnabled(true);
}

void ModelsTab::onIndexReady()
{
    m_model->setIndex(m_store->isOpen() ? &m_store->index() : nullptr, {QStringLiteral(".smd"), QStringLiteral(".fmt")});
    applyFilter();
}

void ModelsTab::onMaterialsReady()
{
    // The background classification finished — hand the shader index (facets + `#family:`/`#workflow:`
    // search) and the true-name index (the "In-game name" column + name search) to the list.
    m_model->setMaterialIndex(m_store->isMaterialIndexReady() ? &m_store->materialIndex() : nullptr);
    m_model->setNameIndex(m_store->isNameIndexReady() ? &m_store->nameIndex() : nullptr);
    applyFilter();
}

void ModelsTab::applyFilter()
{
    m_model->applyFilters(m_search->text(), m_extFacet->currentData().toString(),
                          m_funnel->activeIds(), m_funnel->matchAny());
    m_funnel->setCounts(m_model->facetCounts());
    m_count->setText(QStringLiteral("%1 of %2 models").arg(m_model->rowCount()).arg(m_model->totalInBaseSet()));
}

void ModelsTab::onListActivated(const QModelIndex& idx)
{
    if (!idx.isValid()) return;
    loadModel(m_model->pathAt(idx.row()));
}

void ModelsTab::loadCurrentRow()
{
    // Read the SHARED selection model directly (both the table and the grid drive it), so a click in
    // the grid loads the model even though the table view is hidden.
    const QModelIndex cur = m_list->selectionModel() ? m_list->selectionModel()->currentIndex() : m_list->currentIndex();
    if (!cur.isValid()) return;
    const QString path = m_model->pathAt(cur.row());
    if (path.isEmpty() || path == m_currentPath) return;   // don't reload the one already shown
    loadModel(path);
}

void ModelsTab::loadModel(const QString& path)
{
    if (path.isEmpty() || !m_store->isOpen()) return;
    ModelGeometry geo; QString err;
    if (!m_store->loadModel(path, geo, &err)) { emit status(QStringLiteral("load failed: %1").arg(err)); m_info->setText(err); return; }
    m_currentPath = path;
    // Decode clips too, so the animation bar can play them (skeleton overlay only needs bones, but
    // the cost of decompressing the key bundle is paid once per load and preview is not hot-path).
    AstSkeleton::Skeleton skel = m_store->loadSkeletonFor(path, /*decodeClips*/true, geo.jointPaletteSize());
    m_skel = skel;                         // kept so library clips can be retargeted onto this rig
    m_view->setModel(geo, skel);
    // Is this mesh on the player base rig? (skinned, its rig covers the palette, and its bone names
    // largely match the base rig). Only then does the player animation library apply — a monster or
    // prop rig does not get player moves. Classify by authored bone names, not a path guess.
    m_rigIsPlayer = false;
    m_libActive = false;
    if (m_view->isSkinnedMesh() && m_view->skeletonMatchesMesh() && !skel.bones.isEmpty()) {
        const QSet<QString>& base = m_store->playerRigBoneNames();
        if (!base.isEmpty()) {
            int hit = 0;
            for (const AstSkeleton::Bone& b : skel.bones) if (base.contains(b.name)) ++hit;
            m_rigIsPlayer = hit >= int(base.size() * 0.8);
        }
    }
    if (m_rigIsPlayer) populateMoveSources();
    // Decode the full PBR texture set per material through the same resolver the exporter uses
    // (docs/FORMATS.md §6.1), so the viewport and the .glb agree on channel packing.
    const QVector<GlbExporter::ExportMaterial> mats = m_store->resolveMaterials(geo, /*decodeTextures*/true);
    QVector<GLModelWidget::MaterialTextures> tex(geo.materialPaths.size());
    for (int i = 0; i < mats.size() && i < tex.size(); ++i) {
        tex[i].baseColor = mats[i].baseColor;
        tex[i].normal    = mats[i].normal;
        tex[i].metalRough = mats[i].metallicRoughness;
        tex[i].emissive  = mats[i].emissive;
        tex[i].specColor = mats[i].specularColor;
        for (int c = 0; c < 3; ++c) tex[i].subsurface[c] = mats[i].subsurface[c];
        tex[i].translucent = mats[i].transmissionFactor;
        tex[i].alphaMode = mats[i].alphaMode;
        tex[i].isFur = mats[i].isFur; tex[i].furNoise = mats[i].furNoise; tex[i].furMask = mats[i].furMask; tex[i].furDepth = mats[i].furDepth;
    }
    m_view->setMaterialTextures(tex);
    m_skeleton->setEnabled(!skel.bones.isEmpty());
    refreshAnimBar();
    rebuildPartsPanel(geo);
    refreshInfo(geo);
    refreshAttachments();
    m_view->frameAll();
}

// The shared right-click menu for a model row, used by both the table and the grid.
void ModelsTab::showRowContextMenu(int row, const QPoint& globalPos)
{
    const QModelIndex idx = m_model->index(row, AssetListModel::ColName);
    if (!idx.isValid()) return;
    const QString path = m_model->pathAt(row);
    QMenu menu(this);
    menu.addAction(QStringLiteral("Load / preview"), [this, idx] { onListActivated(idx); });
    menu.addAction(QStringLiteral("Copy path"), [this, path] { qApp->clipboard()->setText(path); });
    const int selCount = selectedModelPaths().size();
    menu.addAction(selCount > 1 ? QStringLiteral("Export %1 models…").arg(selCount) : QStringLiteral("Export model…"),
                   [this, idx, selCount] { if (selCount <= 1) onListActivated(idx); exportSelected(); });
    menu.addAction(QStringLiteral("Save preview image…"), [this, idx] { onListActivated(idx); saveImage(); });
    menu.addAction(QStringLiteral("Turntable GIF…"), [this, idx] { onListActivated(idx); turntableGif(); });
    menu.addAction(QStringLiteral("Animation-loop GIF…"), [this, idx] { onListActivated(idx); animLoopGif(); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Explain material…"), [this, path] { showMaterialReport(path); });
    menu.exec(globalPos);
}

// Toggle the icon grid vs the detail table. The grid installs the (lazy) thumbnail provider; the table
// clears it so list mode doesn't render a thumbnail for every visible row.
void ModelsTab::setGridView(bool on)
{
    m_model->setGridMode(on);
    if (on) {
        m_model->setIconProvider([this](quint32 fi) { return m_thumbs->get(fi); });
        setIconPx(m_iconPx);
        m_list->hide(); m_gridView->show(); m_gridView->setFocus();
    } else {
        m_model->setIconProvider(nullptr);
        m_gridView->hide(); m_list->show(); m_list->setFocus();
    }
    if (m_gridBtn->isChecked() != on) { QSignalBlocker b(m_gridBtn); m_gridBtn->setChecked(on); }
}

void ModelsTab::setIconPx(int px)
{
    m_iconPx = qBound(64, px, 320);
    m_model->setIconPx(m_iconPx);
    m_gridView->setIconSize(QSize(m_iconPx, m_iconPx));
    m_gridView->setGridSize(QSize(m_iconPx + 16, m_iconPx + 52));   // icon + a caption line
}

bool ModelsTab::eventFilter(QObject* obj, QEvent* ev)
{
    const bool onGrid = (m_gridView && obj == m_gridView->viewport());
    if (onGrid && ev->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(ev);
        const int dy = we->angleDelta().y();
        if (we->modifiers() & Qt::ControlModifier) { setIconPx(m_iconPx + (dy > 0 ? 14 : -14)); return true; }
        if (m_hover->isShowing()) { m_hover->stepSize(dy > 0 ? 40 : -40); return true; }  // resize the preview
        m_hover->hidePreview();                                                            // else let the list scroll
        return false;
    }
    if (onGrid && ev->type() == QEvent::ToolTip) {
        auto* he = static_cast<QHelpEvent*>(ev);
        const QModelIndex idx = m_gridView->indexAt(m_gridView->viewport()->mapFromGlobal(he->globalPos()));
        if (idx.isValid()) {
            const uint32_t fi = m_model->fileIndexAt(idx.row());
            const QString path = m_model->pathAt(idx.row());
            const QPixmap pm = m_thumbs->get(fi);           // best-res source; the card scales it
            const QString name = m_store->isNameIndexReady() ? m_store->nameIndex().nameFor(path) : QString();
            const QString html = QStringLiteral("<b>%1</b><br><span style='color:#9a9a9a'>%2</span>")
                               .arg(name.isEmpty() ? QFileInfo(path).fileName() : name, QFileInfo(path).fileName());
            m_hover->showFor(pm, html, he->globalPos());
            return true;
        }
    }
    if (onGrid && ev->type() == QEvent::Leave) m_hover->hidePreview();
    return QWidget::eventFilter(obj, ev);
}

void ModelsTab::showMaterialReport(const QString& modelPath)
{
    if (modelPath.isEmpty() || !m_store->isOpen()) return;
    const QString text = MaterialReport::explainModel(*m_store, modelPath);
    // One shared read-only, monospace, copyable report pane (template §18) — the same TextReport used
    // by the Health check, so the two can't drift in how they present a report.
    TextReport::show(this, QStringLiteral("Explain material — %1").arg(QFileInfo(modelPath).fileName()), text);
}

// The viewport right-click part menu (§11). Built here (not in the widget) so it can reach the same
// Export-parts path the rest of the tab uses — one set of part actions, offered wherever parts are
// acted on. The selection was already scoped by the widget (right-click outside the selection replaced
// it), so this acts on m_view->selectedParts().
void ModelsTab::showViewportPartMenu(const QPoint& globalPos)
{
    if (m_currentPath.isEmpty() || m_view->partCount() == 0) return;
    const QSet<int> sel = m_view->selectedParts();
    const int total = m_view->partCount();
    QMenu menu(this);

    if (!sel.isEmpty()) {
        // Pluralise from one place (template §13 vocabulary): "part" vs "3 parts".
        const QString parts = sel.size() == 1 ? QStringLiteral("part") : QStringLiteral("%1 parts").arg(sel.size());
        menu.addAction(QStringLiteral("Frame %1").arg(parts), this, [this] { m_view->frameSelected(); });
        menu.addAction(QStringLiteral("Isolate %1").arg(parts), this, [this, sel] { m_view->isolateParts(sel); });
        menu.addAction(QStringLiteral("Export selected parts…"), this, [this] { exportParts(); });
        // Copy the selected parts' names, in part order, one per line.
        menu.addAction(sel.size() == 1 ? QStringLiteral("Copy part name") : QStringLiteral("Copy %1 part names").arg(sel.size()),
                       this, [this, sel] {
            QStringList names; for (int i = 0; i < m_view->partCount(); ++i) if (sel.contains(i)) names << m_view->partName(i);
            qApp->clipboard()->setText(names.join(QLatin1Char('\n')));
        });
        menu.addSeparator();
    }

    if (!m_view->hiddenParts().isEmpty())
        menu.addAction(QStringLiteral("Show all parts (%1 hidden)").arg(m_view->hiddenParts().size()),
                       this, [this] { m_view->clearHiddenParts(); });
    menu.addAction(QStringLiteral("Select all parts"), this, [this, total] {
        QSet<int> all; for (int i = 0; i < total; ++i) all.insert(i);
        m_view->setSelectedParts(all);
    });
    if (!sel.isEmpty())
        menu.addAction(QStringLiteral("Clear selection"), this, [this] { m_view->setSelectedParts({}); });

    menu.exec(globalPos);
}

void ModelsTab::rebuildPartsPanel(const ModelGeometry& geo)
{
    m_syncingSelection = true;
    m_parts->setRowCount(geo.parts.size());
    for (int i = 0; i < geo.parts.size(); ++i) {
        auto* n = new QTableWidgetItem(geo.parts[i].name);
        n->setData(Qt::UserRole, i);
        m_parts->setItem(i, 0, n);
        m_parts->setItem(i, 1, new QTableWidgetItem(QString::number(geo.parts[i].indexCount / 3)));
        m_parts->setItem(i, 2, new QTableWidgetItem(QFileInfo(geo.parts[i].material).fileName()));
    }
    m_syncingSelection = false;
}

void ModelsTab::refreshInfo(const ModelGeometry& geo)
{
    QStringList lines;
    lines << QStringLiteral("<b>%1</b>").arg(m_currentPath.section(QLatin1Char('/'), -1));
    lines << m_currentPath;
    lines << QStringLiteral("format: .%1 v%2   vertexFormat 0x%3")
                 .arg(m_currentPath.endsWith(QStringLiteral(".fmt")) ? QStringLiteral("fmt") : QStringLiteral("smd"))
                 .arg(geo.formatVersion).arg(geo.vertexFormat, 0, 16);
    lines << QStringLiteral("%1 parts · %2 verts · %3 tris%4")
                 .arg(geo.parts.size()).arg(geo.vertices.size()).arg(geo.triangleCount())
                 .arg(geo.skinned ? QStringLiteral(" · skinned") : QString());
    if (!geo.materialPaths.isEmpty()) {
        lines << QStringLiteral("<b>Materials:</b>");
        for (const QString& m : geo.materialPaths) lines << QStringLiteral("• %1").arg(m);
    }
    m_info->setText(lines.join(QStringLiteral("<br>")));
}

void ModelsTab::onViewportSelectionChanged(const QSet<int>& parts)
{
    // One named slot both surfaces call (§3.9): sync the parts table without re-emitting.
    if (m_syncingSelection) return;
    m_syncingSelection = true;
    m_parts->clearSelection();
    for (int r = 0; r < m_parts->rowCount(); ++r)
        if (parts.contains(m_parts->item(r, 0)->data(Qt::UserRole).toInt())) m_parts->selectRow(r);
    m_syncingSelection = false;
}

void ModelsTab::onPartsSelectionChanged()
{
    if (m_syncingSelection) return;
    m_syncingSelection = true;
    QSet<int> sel;
    for (const QModelIndex& idx : m_parts->selectionModel()->selectedRows())
        sel.insert(m_parts->item(idx.row(), 0)->data(Qt::UserRole).toInt());
    m_view->setSelectedParts(sel);
    m_syncingSelection = false;
}

QVector<int> ModelsTab::currentPartSubset() const
{
    QVector<int> v;
    for (int p : m_view->selectedParts()) v.append(p);
    std::sort(v.begin(), v.end());
    return v;
}

QStringList ModelsTab::selectedModelPaths() const
{
    QStringList paths;
    if (m_list->selectionModel())
        for (const QModelIndex& idx : m_list->selectionModel()->selectedRows()) {
            const QString p = m_model->pathAt(idx.row());
            const QString pl = p.toLower();
            if (pl.endsWith(QStringLiteral(".smd")) || pl.endsWith(QStringLiteral(".fmt"))) paths << p;
        }
    paths.removeDuplicates();
    return paths;
}

// Make `path` the model shown in the viewport, loading it if it isn't already. Synchronous — after
// this returns true the viewport holds `path` and renderToImage / a GIF capture will render it (they
// flush any pending upload themselves). Used by the image/GIF exporters to visit each selected model.
bool ModelsTab::ensureLoaded(const QString& path)
{
    if (path.isEmpty()) return false;
    if (path != m_currentPath) loadModel(path);
    return path == m_currentPath;   // loadModel sets m_currentPath only on success
}

bool ModelsTab::exportOne(const QString& modelPath, const GlbExporter::Options& opt, const QString& outPath, bool allowPartSubset)
{
    QString err;
    ModelGeometry body;
    if (!m_store->loadModel(modelPath, body, &err)) { emit status(err); return false; }
    AstSkeleton::Skeleton skel = m_store->loadSkeletonFor(modelPath, /*decodeClips*/true, body.jointPaletteSize());
    // Assembled export merges the attachments (bone-weighted) into one skinned mesh; otherwise the body.
    ModelGeometry geo = opt.includeAttachments ? m_store->assembleForExport(modelPath, skel) : body;
    // The viewport part selection only makes sense for the single model currently shown.
    if (allowPartSubset && modelPath == m_currentPath && !opt.includeAttachments) {
        const QVector<int> subset = currentPartSubset();
        if (!subset.isEmpty()) for (int i = 0; i < geo.parts.size(); ++i) geo.parts[i].visible = subset.contains(i);
    }
    const QVector<GlbExporter::ExportMaterial> mats = m_store->resolveMaterials(geo, /*decode*/true);
    if (!GlbExporter::write(geo, skel, mats, opt, outPath, &err)) { emit status(QStringLiteral("export failed: %1").arg(err)); return false; }
    return true;
}

// Build export Options straight from the saved Settings (used when the per-export prompt is off).
static GlbExporter::Options optionsFromConfig()
{
    GlbExporter::Options o;
    o.unitScale          = float(Config::exportUnitScale());
    o.includeSkeleton    = Config::exportSkeleton();
    o.includeAnimations  = Config::exportAnimations();
    o.embedTextures      = Config::exportEmbedTextures();
    o.looseTextures      = Config::exportLooseTextures();
    o.reconstructNormalZ = Config::exportReconstructNormalZ();
    o.includeAttachments = Config::exportIncludeAttachments();
    o.yaw180             = Config::exportYaw180();
    return o;
}

void ModelsTab::exportSelected()
{
    if (!m_store->isOpen()) return;
    QStringList paths = selectedModelPaths();
    if (paths.isEmpty() && !m_currentPath.isEmpty()) paths << m_currentPath;   // fall back to the shown model
    if (paths.isEmpty()) { emit status(QStringLiteral("Select one or more models to export.")); return; }

    // The per-export options dialog can be turned off (Settings) — then the saved defaults are used.
    GlbExporter::Options opt; bool wantGltf;
    if (Config::exportShowPrompt()) {
        const bool curHasAttach = !m_currentPath.isEmpty() && !m_store->attachmentsForModel(m_currentPath).isEmpty();
        ExportOptionsDialog dlg(this);
        dlg.setContext(m_view->clipCount(), m_view->currentClip(), curHasAttach || paths.size() > 1, paths.size());
        if (dlg.exec() != QDialog::Accepted) return;
        opt = dlg.options(); wantGltf = dlg.wantGltf();
    } else {
        opt = optionsFromConfig(); wantGltf = Config::exportGltf();
    }
    const QString ext = wantGltf ? QStringLiteral(".gltf") : QStringLiteral(".glb");
    const QString base = Config::lastExportDir().isEmpty() ? QDir::homePath() : Config::lastExportDir();

    if (paths.size() == 1) {
        QString out;
        // Reuse-last-folder: skip the file picker and auto-name into the last export folder.
        if (Config::exportReuseLastDir() && !Config::lastExportDir().isEmpty() && QFileInfo::exists(base)) {
            out = QDir(base).filePath(QFileInfo(paths[0]).baseName() + ext);
            for (int n = 1; QFileInfo::exists(out); ++n) out = QDir(base).filePath(QStringLiteral("%1_%2%3").arg(QFileInfo(paths[0]).baseName()).arg(n).arg(ext));
        } else {
            const QString suggested = QDir(base).filePath(QFileInfo(paths[0]).baseName() + ext);
            const QString filter = wantGltf ? QStringLiteral("glTF (*.gltf)") : QStringLiteral("glTF binary (*.glb)");
            out = QFileDialog::getSaveFileName(this, QStringLiteral("Export model"), suggested, filter);
            if (out.isEmpty()) return;
            if (!out.toLower().endsWith(ext)) out += ext;
        }
        Config::setLastExportDir(QFileInfo(out).absolutePath());
        if (exportOne(paths[0], opt, out, /*allowPartSubset*/true))
            ExportNotifier::instance().notify(QStringLiteral("Exported %1").arg(QFileInfo(out).fileName()), QFileInfo(out).absolutePath());
        return;
    }

    // Multi-select: one file per model into a chosen folder (or straight into the last folder).
    QString dir;
    if (Config::exportReuseLastDir() && !Config::lastExportDir().isEmpty() && QFileInfo::exists(base)) dir = base;
    else dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Export %1 models to folder").arg(paths.size()), base);
    if (dir.isEmpty()) return;
    Config::setLastExportDir(dir);
    int ok = 0, fail = 0;
    for (const QString& p : paths) {
        QString name = QFileInfo(p).baseName();
        QString out = QDir(dir).filePath(name + ext);
        for (int n = 1; QFileInfo::exists(out); ++n) out = QDir(dir).filePath(QStringLiteral("%1_%2%3").arg(name).arg(n).arg(ext));  // avoid collisions
        if (exportOne(p, opt, out, /*allowPartSubset*/false)) ++ok; else ++fail;
    }
    ExportNotifier::instance().notify(QStringLiteral("Exported %1 model%2 to %3%4").arg(ok).arg(ok == 1 ? QString() : QStringLiteral("s"))
                    .arg(QDir(dir).dirName()).arg(fail ? QStringLiteral(" (%1 failed)").arg(fail) : QString()), dir);
}

// Save the current viewport view as a PNG (template §15 image/icon export). Single-model only — an
// image is of one thing on screen (unlike the batch .glb export, which fans out); the options come from
// ImageExportDialog and the render is done offscreen by GLModelWidget::renderToImage (re-rendered at the
// chosen scale, so a larger scale is genuinely sharper, not an upscale).
void ModelsTab::saveImage()
{
    if (!m_store->isOpen()) return;
    QStringList paths = selectedModelPaths();
    if (paths.isEmpty() && !m_currentPath.isEmpty()) paths << m_currentPath;
    if (paths.isEmpty()) { emit status(QStringLiteral("Load or select a model, then Save preview image.")); return; }

    // The save-image options dialog can be turned off (Settings) — then the saved defaults are used.
    int scale; bool tbg, crop;
    if (Config::imageShowPrompt()) {
        ImageExportDialog dlg(this);
        if (dlg.exec() != QDialog::Accepted) return;
        scale = dlg.scalePercent(); tbg = dlg.transparentBg(); crop = dlg.cropToModel();
    } else {
        scale = Config::imageScalePercent(); tbg = Config::imageTransparentBg(); crop = Config::imageCropToModel();
    }

    if (paths.size() == 1) {
        if (!ensureLoaded(paths[0])) { emit status(QStringLiteral("Couldn't load %1").arg(QFileInfo(paths[0]).fileName())); return; }
        const QImage img = m_view->renderToImage(scale, tbg, crop);
        if (img.isNull()) { emit status(QStringLiteral("Couldn't render an image — the model isn't in the viewport.")); return; }
        const QString base = Config::lastExportDir().isEmpty() ? QDir::homePath() : Config::lastExportDir();
        QString out;
        if (Config::exportReuseLastDir() && !Config::lastExportDir().isEmpty() && QFileInfo::exists(base)) {
            out = QDir(base).filePath(QFileInfo(paths[0]).baseName() + QStringLiteral(".png"));
            for (int n = 1; QFileInfo::exists(out); ++n) out = QDir(base).filePath(QStringLiteral("%1_%2.png").arg(QFileInfo(paths[0]).baseName()).arg(n));
        } else {
            const QString suggested = QDir(base).filePath(QFileInfo(paths[0]).baseName() + QStringLiteral(".png"));
            out = QFileDialog::getSaveFileName(this, QStringLiteral("Save preview image"), suggested, QStringLiteral("PNG image (*.png)"));
            if (out.isEmpty()) return;
            if (!out.toLower().endsWith(QStringLiteral(".png"))) out += QStringLiteral(".png");
        }
        Config::setLastExportDir(QFileInfo(out).absolutePath());
        if (img.save(out, "PNG"))
            ExportNotifier::instance().notify(QStringLiteral("Saved %1 (%2×%3)").arg(QFileInfo(out).fileName()).arg(img.width()).arg(img.height()), QFileInfo(out).absolutePath());
        else
            emit status(QStringLiteral("Failed to save %1").arg(QFileInfo(out).fileName()));
        return;
    }

    // Multi-select: one PNG per model into a chosen folder, visiting each in turn.
    const QString base = Config::lastExportDir().isEmpty() ? QDir::homePath() : Config::lastExportDir();
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Save %1 preview images to folder").arg(paths.size()), base);
    if (dir.isEmpty()) return;
    Config::setLastExportDir(dir);
    QProgressDialog prog(QStringLiteral("Saving preview images…"), QStringLiteral("Cancel"), 0, paths.size(), this);
    prog.setWindowModality(Qt::WindowModal);
    int ok = 0, fail = 0;
    for (int i = 0; i < paths.size(); ++i) {
        prog.setValue(i); if (prog.wasCanceled()) break;
        if (!ensureLoaded(paths[i])) { ++fail; continue; }
        const QImage img = m_view->renderToImage(scale, tbg, crop);
        QString name = QFileInfo(paths[i]).baseName();
        QString out = QDir(dir).filePath(name + QStringLiteral(".png"));
        for (int n = 1; QFileInfo::exists(out); ++n) out = QDir(dir).filePath(QStringLiteral("%1_%2.png").arg(name).arg(n));
        if (!img.isNull() && img.save(out, "PNG")) ++ok; else ++fail;
    }
    prog.setValue(paths.size());
    ExportNotifier::instance().notify(QStringLiteral("Saved %1 image%2 to %3%4").arg(ok).arg(ok == 1 ? QString() : QStringLiteral("s"))
                    .arg(QDir(dir).dirName()).arg(fail ? QStringLiteral(" (%1 failed)").arg(fail) : QString()), dir);
}

// Export only the parts selected in the viewport — a single-model operation (parts belong to the shown
// model). Reuses the normal export path with the part subset; requires at least one part selected.
void ModelsTab::exportParts()
{
    if (!m_store->isOpen()) return;
    if (m_currentPath.isEmpty()) { emit status(QStringLiteral("Load a model and select parts to export.")); return; }
    if (currentPartSubset().isEmpty()) { emit status(QStringLiteral("Select one or more parts in the viewport first (click a part).")); return; }

    ExportOptionsDialog dlg(this);
    const bool curHasAttach = !m_store->attachmentsForModel(m_currentPath).isEmpty();
    dlg.setContext(m_view->clipCount(), m_view->currentClip(), curHasAttach, 1);
    if (dlg.exec() != QDialog::Accepted) return;
    GlbExporter::Options opt = dlg.options();
    opt.includeAttachments = false;   // a part subset is about the body's own parts, not attachments
    const QString ext = dlg.wantGltf() ? QStringLiteral(".gltf") : QStringLiteral(".glb");
    const QString base = Config::lastExportDir().isEmpty() ? QDir::homePath() : Config::lastExportDir();
    const QString suggested = QDir(base).filePath(QFileInfo(m_currentPath).baseName() + QStringLiteral("_parts") + ext);
    const QString filter = dlg.wantGltf() ? QStringLiteral("glTF (*.gltf)") : QStringLiteral("glTF binary (*.glb)");
    QString out = QFileDialog::getSaveFileName(this, QStringLiteral("Export selected parts"), suggested, filter);
    if (out.isEmpty()) return;
    if (!out.toLower().endsWith(ext)) out += ext;
    Config::setLastExportDir(QFileInfo(out).absolutePath());
    if (exportOne(m_currentPath, opt, out, /*allowPartSubset*/true))
        ExportNotifier::instance().notify(QStringLiteral("Exported %1 (%2 parts)").arg(QFileInfo(out).fileName()).arg(currentPartSubset().size()), QFileInfo(out).absolutePath());
}

// Non-destructive extraction: write the EXACT original game files a model is made of — the mesh, its
// .sm descriptor, the .mat materials and every .dds texture (plus the .ao/.ast and headers) — as their
// authored bytes, decoded and converted by nothing. Files mirror their game paths under the chosen
// folder, so the .mat→.dds and .sm→.smd references still resolve. This is how a user gets a model in
// its original condition, alongside the convenience .glb export.
void ModelsTab::extractOriginals()
{
    if (!m_store->isOpen()) return;
    QStringList paths = selectedModelPaths();
    if (paths.isEmpty() && !m_currentPath.isEmpty()) paths << m_currentPath;
    if (paths.isEmpty()) { emit status(QStringLiteral("Select one or more models to extract.")); return; }
    const QString base = Config::lastExportDir().isEmpty() ? QDir::homePath() : Config::lastExportDir();
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Extract original files to folder"), base);
    if (dir.isEmpty()) return;
    Config::setLastExportDir(dir);
    int files = 0, fail = 0; QSet<QString> written;
    for (const QString& p : paths) {
        const QStringList deps = m_store->collectAssetFiles(p, /*textures*/true, /*skeleton*/true);
        for (const QString& gp : deps) {
            if (written.contains(gp)) continue;
            written.insert(gp);
            const QByteArray bytes = m_store->readFile(gp, nullptr);
            if (bytes.isEmpty()) { ++fail; continue; }
            const QString outPath = QDir(dir).filePath(gp);           // mirror the full game path
            QDir().mkpath(QFileInfo(outPath).absolutePath());
            QFile f(outPath);
            if (f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size()) ++files; else ++fail;
        }
    }
    ExportNotifier::instance().notify(QStringLiteral("Extracted %1 original file%2 for %3 model%4%5")
        .arg(files).arg(files == 1 ? QString() : QStringLiteral("s")).arg(paths.size())
        .arg(paths.size() == 1 ? QString() : QStringLiteral("s"))
        .arg(fail ? QStringLiteral(" (%1 failed)").arg(fail) : QString()), dir);
}

void ModelsTab::turntableGif() { exportGifs(/*turntable*/true); }
void ModelsTab::animLoopGif()  { exportGifs(/*turntable*/false); }

// Shared GIF export for both kinds: one GIF per selected model, with a cancellable progress dialog that
// tracks total frames across all models. Single-select uses a save dialog; multi-select a folder.
void ModelsTab::exportGifs(bool turntable)
{
    if (!m_store->isOpen()) return;
    QStringList paths = selectedModelPaths();
    if (paths.isEmpty() && !m_currentPath.isEmpty()) paths << m_currentPath;
    if (paths.isEmpty()) { emit status(QStringLiteral("Load or select a model to export a GIF.")); return; }

    GifExportDialog dlg(turntable ? GifExportDialog::Turntable : GifExportDialog::AnimLoop, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const ExportCapture::GifOptions opt = dlg.options();

    auto capture = [&](const QString& outPath, const ExportCapture::ProgressFn& p) {
        return turntable ? ExportCapture::turntableGif(m_view, outPath, opt, p)
                         : ExportCapture::animLoopGif(m_view, outPath, opt, p);
    };
    const QString kind = turntable ? QStringLiteral("Turntable GIF") : QStringLiteral("Animation-loop GIF");
    const QString base = Config::lastExportDir().isEmpty() ? QDir::homePath() : Config::lastExportDir();

    if (paths.size() == 1) {
        if (!ensureLoaded(paths[0])) { emit status(QStringLiteral("Couldn't load %1").arg(QFileInfo(paths[0]).fileName())); return; }
        if (!turntable && m_view->currentClip() < 0) { emit status(QStringLiteral("Select and play a clip first — an animation-loop GIF needs a clip.")); return; }
        const QString suggested = QDir(base).filePath(QFileInfo(paths[0]).baseName() + QStringLiteral(".gif"));
        QString out = QFileDialog::getSaveFileName(this, kind, suggested, QStringLiteral("Animated GIF (*.gif)"));
        if (out.isEmpty()) return;
        if (!out.toLower().endsWith(QStringLiteral(".gif"))) out += QStringLiteral(".gif");
        Config::setLastExportDir(QFileInfo(out).absolutePath());
        QProgressDialog prog(QStringLiteral("Rendering %1…").arg(kind), QStringLiteral("Cancel"), 0, 100, this);
        prog.setWindowModality(Qt::WindowModal); prog.setMinimumDuration(0);
        const bool ok = capture(out, [&](int done, int total) {
            prog.setMaximum(total); prog.setValue(done); QApplication::processEvents(); return !prog.wasCanceled(); });
        prog.close();
        if (ok) ExportNotifier::instance().notify(QStringLiteral("Saved %1").arg(QFileInfo(out).fileName()), QFileInfo(out).absolutePath());
        else    emit status(QStringLiteral("%1 cancelled or failed").arg(kind));
        return;
    }

    // Multi-select: one GIF per model into a folder.
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Save %1 GIFs to folder").arg(paths.size()), base);
    if (dir.isEmpty()) return;
    Config::setLastExportDir(dir);
    QProgressDialog prog(QStringLiteral("Rendering %1s…").arg(kind), QStringLiteral("Cancel"), 0, paths.size(), this);
    prog.setWindowModality(Qt::WindowModal); prog.setMinimumDuration(0);
    int ok = 0, fail = 0, skip = 0;
    for (int i = 0; i < paths.size(); ++i) {
        prog.setLabelText(QStringLiteral("Rendering %1s… (%2/%3)").arg(kind).arg(i + 1).arg(paths.size()));
        prog.setValue(i); QApplication::processEvents(); if (prog.wasCanceled()) break;
        if (!ensureLoaded(paths[i])) { ++fail; continue; }
        if (!turntable && m_view->currentClip() < 0) { ++skip; continue; }   // no clip → no anim-loop
        QString name = QFileInfo(paths[i]).baseName();
        QString out = QDir(dir).filePath(name + QStringLiteral(".gif"));
        for (int n = 1; QFileInfo::exists(out); ++n) out = QDir(dir).filePath(QStringLiteral("%1_%2.gif").arg(name).arg(n));
        if (capture(out, {})) ++ok; else ++fail;
    }
    prog.setValue(paths.size());
    ExportNotifier::instance().notify(QStringLiteral("Saved %1 GIF%2 to %3%4%5").arg(ok).arg(ok == 1 ? QString() : QStringLiteral("s"))
                    .arg(QDir(dir).dirName())
                    .arg(fail ? QStringLiteral(" (%1 failed)").arg(fail) : QString())
                    .arg(skip ? QStringLiteral(" (%1 had no clip)").arg(skip) : QString()), dir);
}
