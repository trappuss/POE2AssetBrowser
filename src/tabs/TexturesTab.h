#pragma once
#include "index/AssetListModel.h"
#include <QWidget>

class AssetStore;
class GLTextureWidget;
class QLineEdit;
class QTableView;
class QListView;
class QComboBox;
class QLabel;
class QCheckBox;
class QToolButton;
class QTimer;
class ThumbnailCache;
class HoverPreview;

// The Textures tab (template §13): every `.dds` in the game, decoded in-tool, with channel
// isolation, scroll-zoom/pan, a pixel inspector, and the shared search + one-matcher machinery.
class TexturesTab : public QWidget {
    Q_OBJECT
public:
    explicit TexturesTab(AssetStore* store, QWidget* parent = nullptr);
    void onIndexReady();
    void onMaterialsReady();   // populate item-icon names once the name index is built

signals:
    void status(const QString& text);

private slots:
    void applyFilter();
    void onActivated(const QModelIndex&);
    void loadCurrentRow();

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;   // Ctrl+scroll icon resize + hover dwell

private:
    void loadTexture(const QString& path);
    void savePng(const QString& ddsPath);        // decode a .dds → PNG (full RGBA)
    void saveOriginalDds(const QString& ddsPath);// write the exact original .dds (+ .header) bytes
    void setGridView(bool on);                   // toggle the icon grid vs the detail table
    void setIconPx(int px);                      // grid/list thumbnail size (Ctrl+scroll)
    void showContextMenu(const QString& path, const QPoint& globalPos);   // shared by table + grid

    AssetStore* m_store;
    AssetListModel* m_model;
    QLineEdit* m_search = nullptr;
    QTableView* m_list = nullptr;
    QListView* m_grid = nullptr;
    QToolButton* m_gridBtn = nullptr;
    QLabel* m_count = nullptr;
    GLTextureWidget* m_view = nullptr;
    QComboBox* m_channel = nullptr;
    QCheckBox* m_checker = nullptr;
    QLabel* m_info = nullptr;
    QTimer* m_debounce = nullptr;
    QTimer* m_loadDebounce = nullptr;
    ThumbnailCache* m_thumbs = nullptr;
    HoverPreview* m_hover = nullptr;
    int m_iconPx = 128;
    QString m_lastTex;
};
