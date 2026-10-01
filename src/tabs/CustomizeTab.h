#pragma once
#include "model/ModelGeometry.h"
#include "model/AstSkeleton.h"
#include "store/AssetStore.h"
#include "gl/GLModelWidget.h"
#include <QHash>
#include <QMap>
#include <QVector>
#include <QWidget>

class AssetStore;
class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QToolButton;
class QTabWidget;
class QTableWidget;
class QListWidget;
class QSlider;

// The Customize tab (template §33 showpiece — the game-specific dressing room). Pick a class and it
// assembles that class's real body from the game's own character table, then per-slot dropdowns let
// you swap in any cosmetic mesh for that slot, an "outfit" dropdown fills a whole matching set at
// once, and a toggle restricts weapons to each class's signature type. Everything is backed by real
// `.datc64` tables and the model index — no name-substring guessing, no invented data:
//   • classes  ← data/balance/characters.datc64  (12 classes, attribute + base .ao)
//   • slots    ← data/balance/microtransactionslot.datc64 + the item-folder taxonomy (verified meshes)
//   • outfits  ← the cosmetic SET folders under .../microtransactions/<set>/  (198 real sets)
// Assembly reuses the SHARED GLModelWidget: worn armour composites in the body's rest space (verified:
// body/gloves/boots/helmet draw in place), weapons hang from the skeleton's aux_*_Weapon sockets, and
// a shapeshift form replaces the whole body.
class CustomizeTab : public QWidget {
    Q_OBJECT
public:
    explicit CustomizeTab(AssetStore* store, QWidget* parent = nullptr);

    void onIndexReady();       // classes load + slot catalogue is swept once the index is up
    void onMaterialsReady();   // in-game names become available → relabel options

signals:
    void status(const QString& text);

private slots:
    void onClassChanged();
    void onSlotChanged();
    void onOutfitChanged();
    void onLockWeaponsToggled();
    void resetToClassDefault();
    void randomize();          // fill every slot with a random option available to this class
    void exportOutfit();       // full outfit · full outfit without weapons · each item separately
    void exportSlot(int slotIndex, bool rawOriginals);       // export one slot's selected item
    void showSlotExportMenu(int slotIndex, const QPoint& globalPos);   // per-slot export context menu
    void savePreset();         // remember the current class + slots + lock as a named preset
    void deletePreset();       // remove the selected preset
    void applySelectedPreset();// load the selected preset into the controls

private:
    // One cosmetic option for a slot: the display name shown in the dropdown and the mesh it loads.
    struct Option { QString name; QString smd; QString attr; QString set; };
    // A wearable slot: where its meshes live in the index, and how the mesh attaches to the body.
    enum AttachMode { InPlace, Bone, ReplaceBody };
    struct SlotDef { QString id, label; QStringList folders; AttachMode mode; QString bone; bool weapon; };
    // A resolved default-outfit piece declared by the class .ao (hair, beard, starter clothes).
    struct DefaultPiece { QString smd; QString bone; QString aliasSlot; };  // aliasSlot: body/gloves/boots/…

    // The meshes currently making up the figure: {smd, bone ("" = in place / skinned), isWeapon}.
    struct AssembledPiece { QString smd; QString bone; bool weapon; };
    QVector<AssembledPiece> currentPieces() const;            // body-default cosmetics + user selections
    // Merge body + pieces into one geometry for export: in-place pieces keep their own skinning (same
    // rig); bone pieces (weapons) bake rigidly to the bone. Returns the body alone if nothing else.
    ModelGeometry buildOutfitGeometry(bool includeWeapons, QString* err) const;

    // A mesh decoded once and reused: swapping one slot re-assembles the whole figure, and without a
    // cache that re-loads and re-decodes the body and every other worn piece from scratch each time.
    struct DecodedMesh { ModelGeometry geo; QVector<GLModelWidget::MaterialTextures> tex; bool ok = false; };
    const DecodedMesh& decodedFor(const QString& smd);   // load + decode once, cache by smd path
    QStringList fxMeshesFor(const QString& equippedSmd); // the /fx/ effect meshes that ship with an item

    // Right-side inspection panels (ported from the Models tab): Info / Parts / Attachments / Animations.
    void buildPanels(QWidget* parent);         // construct the tab widget + anim bar
    void refreshPanels();                      // repopulate them after each (re)assembly
    void enterAnimationWith(const AstSkeleton::Skeleton& skel, int clipIndex);  // play MERGED skinned figure
    void exitAnimation();                      // restore the interactive attachment assembly (bind pose)

    void reloadPresetList();                   // refresh the preset dropdown from saved settings
    void buildSlotCatalogue();                 // sweep the index once → m_optionsBySlot / m_sets
    void loadClasses();                        // read characters.datc64
    void repopulateSlotCombos();               // fill each slot combo for the current class attribute
    void rebuildAssembly();                    // push body + selected pieces to the viewport
    bool resolveBodyMesh(const QString& aoPath, QString& smdOut, QString& astHint,
                         QVector<DefaultPiece>& defaultsOut) const;   // class .ao → body .smd (+ defaults)
    static QString attrOfStem(const QString& stem);   // trailing str/dex/int/strdex/dexint/strint, or ""
    static bool isNoiseMesh(const QString& path);     // drop/LOD/monster variants we never list
    int slotIndexById(const QString& id) const;

    AssetStore* m_store = nullptr;
    GLModelWidget* m_view = nullptr;

    // Left panel widgets.
    QComboBox* m_classBox = nullptr;
    QComboBox* m_outfitBox = nullptr;
    QCheckBox* m_lockWeapons = nullptr;
    QCheckBox* m_showHair = nullptr;   // show/hide the default hairstyle + beard pieces
    QCheckBox* m_showHead = nullptr;   // show/hide the character's head/face parts of the base body
    QCheckBox* m_showFx   = nullptr;   // load + show/hide equipped-item /fx/ effect meshes
    QLabel*    m_classInfo = nullptr;
    QPushButton* m_resetBtn = nullptr;
    QPushButton* m_randomBtn = nullptr;
    QPushButton* m_exportBtn = nullptr;
    QComboBox*   m_presetBox = nullptr;
    QPushButton* m_savePresetBtn = nullptr;
    QPushButton* m_delPresetBtn = nullptr;
    QVector<QComboBox*> m_slotCombos;          // parallel to m_slots

    // Class table.
    struct CharClass { QString display, aoPath, attr; bool released; };
    QVector<CharClass> m_classes;

    // Slot catalogue (built once).
    QVector<SlotDef> m_slots;
    QHash<QString, QVector<Option>> m_optionsBySlot;          // slotId → all options (every attribute)
    QMap<QString, QHash<QString, Option>> m_sets;             // setName → (slotId → piece)
    QHash<QString, DecodedMesh> m_meshCache;                  // smd path → decoded geo + textures (see decodedFor)

    // Current class resolution.
    QString m_bodySmd;
    AstSkeleton::Skeleton m_skel;
    QVector<DefaultPiece> m_defaults;
    bool m_ready = false;
    bool m_syncing = false;                    // suppress combo signals during programmatic fills
    int  m_exportMode = 0;                     // 0 full outfit · 1 no weapons · 2 each item separately

    // Inspection panels.
    QTabWidget*   m_panelTabs = nullptr;
    QTableWidget* m_partsTable = nullptr;      // parts of the assembled body
    QListWidget*  m_attachPanel = nullptr;     // the equipped/default pieces making up the figure
    QLabel*       m_infoPanel = nullptr;       // summary of the assembled figure
    QListWidget*  m_clipList = nullptr;        // clips of the currently-selected animation source
    QComboBox*    m_animSourceBox = nullptr;   // "(this class rig)" + player-animation-library moves
    QWidget*      m_animBar = nullptr;
    QComboBox*    m_clipBox = nullptr;
    QToolButton*  m_playBtn = nullptr;
    QSlider*      m_timeline = nullptr;
    QLabel*       m_timeLbl = nullptr;
    bool m_syncingTimeline = false;
    bool m_animating = false;                  // true while showing the merged, animatable figure
    QStringList m_pieceLabels;                 // labels of the pieces last assembled (for the panel)

    // Player animation library. Source 0 is the class's own rig (m_skel); source i>0 is
    // m_animCats[i-1], a per-move .ast whose chosen clip is retargeted onto m_skel by bone name and
    // played on the merged figure. m_playSkel holds the rig+clip currently handed to the viewport.
    QVector<AssetStore::AnimCategory> m_animCats;
    AstSkeleton::Skeleton m_libHeader;         // header (clip list) of the selected library move
    AstSkeleton::Skeleton m_playSkel;          // skeleton actually driving playback (own rig or lib)
    void onAnimSourceChanged(int sourceRow);   // repopulate the clip list for the chosen source
    void populateClipList();                   // fill m_clipList / m_clipBox from the current source
    void playAnimClip(int clipRow);            // resolve + retarget + play the chosen clip
};
