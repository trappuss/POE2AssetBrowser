#pragma once
#include <QDialog>
#include <QVector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QLineEdit;
class QLabel;
class QKeySequenceEdit;
class QTabWidget;

// Settings dialog (template §16): top-level tabs in the order setup → presentation → per-area →
// what lands on disk → keys → upkeep → reference. Every tab is a QScrollArea so nothing clips; the
// tab bar never elides; Restore Defaults works by REMOVING keys (§3.10) so a later default change is
// picked up. One QSettings key per state (§3.1); combos persist stable strings, not indices (§3.2).
// The export options here are the DEFAULTS the export paths read (ExportConfig); the per-export
// dialog can still override them per run.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

signals:
    void gameDirChanged();   // emitted on accept when the game/bundles folder changed → MainWindow reindexes

protected:
    void showEvent(QShowEvent*) override;   // fold the tab bar's full width in so no label elides

private:
    QWidget* makeGeneralTab();
    QWidget* makeExportTab();
    QWidget* makeHotkeysTab();
    QWidget* makeMaintenanceTab();
    QWidget* makeInformationTab();
    QWidget* wrapScroll(QWidget* content);  // every tab is a scroll area

    void load();
    void save();
    void restoreExportDefaults();
    void refreshCacheList();

    QTabWidget* m_tabs = nullptr;

    // General
    QLineEdit* m_gameDir = nullptr;
    QLineEdit* m_bundlesOverride = nullptr;
    QString    m_origGameDir, m_origBundles;

    // Export ▸ Models
    QDoubleSpinBox* m_unitScale = nullptr;
    QComboBox*      m_format = nullptr;       // 0 .glb · 1 .gltf+.bin
    QComboBox*      m_animations = nullptr;   // 0 all · 1 none (default; "current clip" is per-export)
    QCheckBox*      m_skeleton = nullptr;
    QCheckBox*      m_attachments = nullptr;
    QCheckBox*      m_yaw180 = nullptr;
    QCheckBox*      m_embedTextures = nullptr;
    QCheckBox*      m_looseTextures = nullptr;
    QCheckBox*      m_normalZ = nullptr;
    QCheckBox*      m_exportShowPrompt = nullptr;
    QCheckBox*      m_exportReuseLastDir = nullptr;
    // Export ▸ Image
    QSpinBox*       m_imageScale = nullptr;
    QCheckBox*      m_imageTransparent = nullptr;
    QCheckBox*      m_imageCrop = nullptr;
    QCheckBox*      m_imageShowPrompt = nullptr;
    // Export ▸ Bulk
    QSpinBox*       m_bulkWorkers = nullptr;
    QComboBox*      m_bulkLayout = nullptr;

    // Hotkeys
    QVector<QKeySequenceEdit*> m_hotkeyEdits;

    // Maintenance
    QWidget* m_cacheBox = nullptr;
    QLabel*  m_cacheNote = nullptr;
};
