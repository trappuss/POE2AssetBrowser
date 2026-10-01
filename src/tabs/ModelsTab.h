#pragma once
#include "index/AssetListModel.h"
#include "model/GlbExporter.h"
#include "model/AstSkeleton.h"
#include "store/AssetStore.h"
#include <QWidget>

class AssetStore;
class GLModelWidget;
class QLineEdit;
class QTableView;
class QTreeWidget;
class QTableWidget;
class QComboBox;
class QLabel;
class QTimer;
class QCheckBox;
class QSlider;
class QToolButton;
class QWidget;
class QTabWidget;
class QListWidget;
class FunnelFilter;
struct ModelGeometry;

// The Models tab: the shared asset list (.smd/.fmt) + the one shared GLModelWidget, with shading
// modes, a channel viewer, overlay toggles behind the master gate, a PARTS panel that mirrors the
// viewport selection both ways (§11), an INFO panel, single-model .glb export (Ctrl+E) and
// drag-out. The list, search box and QueryTerm matcher are the generic machinery of §4.
class QListView;
class QToolButton;
class ThumbnailCache;
class HoverPreview;
class ModelThumbnailRenderer;

class ModelsTab : public QWidget {
    Q_OBJECT
public:
    explicit ModelsTab(AssetStore* store, QWidget* parent = nullptr);

    void onIndexReady();       // repopulate when the background index finishes
    void onMaterialsReady();   // enable shader search/facets once the classification is built

signals:
    void status(const QString& text);

private slots:
    void applyFilter();
    void onListActivated(const QModelIndex&);
    void loadCurrentRow();               // load whatever row is current (selection/arrow-key driven)
    void onViewportSelectionChanged(const QSet<int>& parts);
    void onPartsSelectionChanged();
    void exportSelected();     // Export model(s) → .glb / .gltf (multi-select → one file per model)
    void exportParts();        // Export only the parts selected in the viewport (single model)
    void extractOriginals();   // Write the EXACT original game files (mesh/.sm/.mat/.dds) — non-destructive
    void saveImage();          // "Save preview image…" (§15): render the current view to a PNG
    void turntableGif();       // Export a 360° turntable GIF (§26)
    void animLoopGif();        // Export the current animation as a looping GIF (§26)

private:
    void loadModel(const QString& path);
    void rebuildPartsPanel(const ModelGeometry& geo);   // uses the geometry loadModel already parsed
    void refreshInfo(const ModelGeometry& geo);         // ditto — avoids re-parsing the mesh per selection
    void refreshAnimBar();           // populate/enable the clip bar for the current model
    void populateClipWidgets();      // fill the clip combo + Animations list from the current move source
    void populateMoveSources();      // fill the move selector once (this model + the player move library)
    void onAnimSourceChanged(int src);  // switch move source (this model's clips, or a library move)
    void playClipRow(int row);       // play clip `row` (0 = bind pose) from the current source
    void refreshAttachments();       // discover + list this body's attachments (Attachments tab)
    void showMaterialReport(const QString& modelPath);   // "Explain material…" (template §18)
    void showViewportPartMenu(const QPoint& globalPos);  // §11 viewport right-click part menu
    QVector<int> currentPartSubset() const;
    QStringList selectedModelPaths() const;              // .smd/.fmt paths currently selected in the list
    bool ensureLoaded(const QString& path);              // make `path` the model shown in the viewport (sync)
    // Capture one GIF (turntable/animLoop) per selected model, with a cancellable progress dialog.
    void exportGifs(bool turntable);
    // Export one model to `outPath` with `opt` (assembling attachments when opt.includeAttachments).
    // partSubset applies only to the single current model's viewport part selection.
    bool exportOne(const QString& modelPath, const GlbExporter::Options& opt, const QString& outPath, bool allowPartSubset);

    AssetStore* m_store;
    AssetListModel* m_model;
    QLineEdit* m_search = nullptr;
    QComboBox* m_extFacet = nullptr;
    FunnelFilter* m_funnel = nullptr;
    QTableView* m_list = nullptr;
    QListView* m_gridView = nullptr;         // icon-grid alternative to the detail table
    QToolButton* m_gridBtn = nullptr;
    QLabel* m_count = nullptr;
    GLModelWidget* m_view = nullptr;
    QComboBox* m_shading = nullptr;
    QComboBox* m_channel = nullptr;
    QCheckBox* m_overlaysOn = nullptr;
    QCheckBox* m_grid = nullptr;
    QCheckBox* m_skeleton = nullptr;
    QTabWidget*   m_panelTabs = nullptr;   // Parts · Animations · Attachments · Info (all separate)
    QTableWidget* m_parts = nullptr;
    QListWidget*  m_clipList = nullptr;    // Animations tab — one row per clip (name + duration)
    QWidget*      m_animSourceRow = nullptr;  // "Move:" selector row (shown only for player-rig meshes)
    QComboBox*    m_animSourceBox = nullptr;  // (this model) + player-animation-library moves
    QListWidget*  m_attachList = nullptr;  // Attachments tab — assembled pieces with show/hide checks
    QLabel* m_info = nullptr;
    QTimer* m_debounce = nullptr;
    QTimer* m_loadDebounce = nullptr;    // coalesces rapid arrow-key selection into one load

    // Animation bar.
    QWidget*     m_animBar = nullptr;
    QComboBox*   m_clipBox = nullptr;
    QToolButton* m_playBtn = nullptr;
    QSlider*     m_timeline = nullptr;
    QLabel*      m_timeLbl = nullptr;
    bool m_syncingTimeline = false;

    // Grid thumbnails: models render to flat-shaded icons offscreen (ModelThumbnailRenderer), driven
    // lazily through the shared ThumbnailCache in its throttled GUI mode (GL can't run off-thread).
    ThumbnailCache* m_thumbs = nullptr;
    HoverPreview* m_hover = nullptr;
    ModelThumbnailRenderer* m_thumbRenderer = nullptr;
    int m_iconPx = 160;
    bool m_texturedIcons = false;            // render grid icons with base-colour textures vs flat grey

    QString m_currentPath;
    bool m_syncingSelection = false;

    // Player animation library (only offered for a mesh on the player base rig). m_skel is the loaded
    // model's resolved skeleton with its OWN clips; a library clip is retargeted onto it by bone name
    // and swapped into the viewport via GLModelWidget::setClips. m_libActive = the viewport currently
    // holds a library clip (so switching back to "(this model)" restores m_skel's own clips).
    AstSkeleton::Skeleton m_skel;
    QVector<AssetStore::AnimCategory> m_animCats;
    AstSkeleton::Skeleton m_libHeader;       // selected move's clip list (names only, no keys)
    bool m_rigIsPlayer = false;
    bool m_libActive = false;

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;   // Ctrl+scroll icon resize + hover dwell
private:
    void setGridView(bool on);
    void setIconPx(int px);
    void showRowContextMenu(int row, const QPoint& globalPos);   // shared by table + grid
};
