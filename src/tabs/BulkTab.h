#pragma once
#include <QWidget>

class AssetStore;
class AssetListModel;
class BulkExtractor;
class FunnelFilter;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QProgressBar;
class QTableView;
class QTimer;
class QThread;

// The Bulk extraction tab (template §14): filter the whole index with the same search + facet as
// the asset list (the ONE shared matcher, §4 — the count you see is the set you export), pick an
// output folder and layout, then export everything in one background run. Run controls only; the
// export options themselves (layout, only-new) are the shared Settings ▸ Export keys (§15). The
// run machinery (manifest / cancel / pause / ETA / failures, §25) lives in BulkExtractor.
class BulkTab : public QWidget {
    Q_OBJECT
public:
    explicit BulkTab(AssetStore* store, QWidget* parent = nullptr);
    ~BulkTab() override;

    void onIndexReady();
    void onMaterialsReady();   // enable #family:/#workflow:/#effect: search once classified

signals:
    void status(const QString& text);

protected:
    void keyPressEvent(QKeyEvent*) override;   // Esc cancels a run

private slots:
    void applyFilter();
    void startRun();
    void cancelRun();
    void togglePause();

private:
    void setRunning(bool running);

    AssetStore* m_store;
    AssetListModel* m_model = nullptr;
    QLineEdit* m_search = nullptr;
    QComboBox* m_facet = nullptr;
    FunnelFilter* m_funnel = nullptr;
    QLabel* m_count = nullptr;
    QTableView* m_list = nullptr;

    QLineEdit* m_outDir = nullptr;
    QComboBox* m_layout = nullptr;
    QCheckBox* m_onlyNew = nullptr;
    QCheckBox* m_doModels = nullptr;
    QCheckBox* m_doTextures = nullptr;
    QCheckBox* m_rawOriginals = nullptr;
    QCheckBox* m_yaw180 = nullptr;
    QSpinBox* m_workers = nullptr;

    QPushButton* m_run = nullptr;
    QPushButton* m_pause = nullptr;
    QPushButton* m_cancel = nullptr;
    QProgressBar* m_progress = nullptr;
    QPlainTextEdit* m_console = nullptr;
    QLabel* m_eta = nullptr;
    QTimer* m_debounce = nullptr;

    QThread* m_thread = nullptr;
    BulkExtractor* m_worker = nullptr;
    bool m_paused = false;
};
