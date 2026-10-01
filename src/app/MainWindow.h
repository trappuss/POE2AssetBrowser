#pragma once
#include <QMainWindow>
#include <memory>

class AssetStore;
class IndexLoader;
class ModelsTab;
class TexturesTab;
class CustomizeTab;
class BulkTab;
class QTabWidget;
class QLabel;
class QToolButton;

// The application window: owns the AssetStore, kicks off the background index build, and hosts the
// tabs. Keeps the tool usable before the index is ready (the tabs simply have nothing to list).
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void chooseGameFolder();
    void reloadIndex();
    void onIndexProgress(const QString& line);
    void onIndexFinished(bool ok, const QString& error);
    void showHealthCheck();
    void showHelp();           // F1 — the in-app cheat sheet (controls, shortcuts, search syntax)

private:
    void startIndex();

    std::unique_ptr<AssetStore> m_store;
    IndexLoader* m_loader = nullptr;
    QTabWidget* m_tabs = nullptr;
    ModelsTab* m_models = nullptr;
    TexturesTab* m_textures = nullptr;
    CustomizeTab* m_customize = nullptr;
    BulkTab* m_bulk = nullptr;
    QLabel* m_status = nullptr;
    QToolButton* m_revealBtn = nullptr;   // "Show in folder" for the last export (ExportNotifier)
    QString m_lastExportFolder;
};
