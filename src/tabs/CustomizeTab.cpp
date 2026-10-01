#include "tabs/CustomizeTab.h"
#include "store/AssetStore.h"
#include "store/DatFile.h"
#include "app/Config.h"
#include "app/SearchableCombo.h"
#include "model/AssetText.h"
#include "model/GlbExporter.h"
#include "model/RigMath.h"
#include "bundle/BundleIndex.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSlider>
#include <QTableWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

// ── Static, evidence-backed slot catalogue ───────────────────────────────────────────────────────
// Folders are the real index taxonomy (art/models/items/armours/<slot>/, .../weapons/…); attach modes
// were verified by rendering real pieces through the shipping viewport: worn armour draws in the body's
// rest space (InPlace), weapons hang from the skeleton's aux_*_Weapon sockets, a shapeshift form
// replaces the whole body. Belts/amulets/rings/flasks carry NO mesh in the data (0 smds), so the
// wearable-mesh set the user asked for omits them.
namespace {
// The base body's head/face parts, by the anatomical names PoE2 gives them (measured from the real
// character bodies: skull / forehead / jaw / cheeks / nose / teeth / ear / eye / neck…). Hiding these
// previews a full helmet. Matched as name substrings; none of the torso/limb part names contain them.
bool isHeadPartName(const QString& name)
{
    static const char* kHead[] = {
        "skull","scalp","forehead","jaw","cheek","nose","teeth","tongue","ear","eye","brow","lash",
        "lip","chin","mouth","gum","neck","head","face"};
    const QString n = name.toLower();
    for (const char* t : kHead) if (n.contains(QLatin1String(t))) return true;
    return false;
}
// A default-outfit piece that is hair or beard (lives under head_attach/face_attach with a hairstyle
// mesh). Lets the Hair toggle hide just the hair while other head attachments (e.g. earrings) stay.
bool isHairDefault(const QString& aliasSlot, const QString& smd)
{
    const QString a = aliasSlot.toLower(), p = smd.toLower();
    const bool headGroup = a.contains(QStringLiteral("head")) || a.contains(QStringLiteral("face"));
    return headGroup && (p.contains(QStringLiteral("hair")) || p.contains(QStringLiteral("beard")));
}
}  // namespace

CustomizeTab::CustomizeTab(AssetStore* store, QWidget* parent) : QWidget(parent), m_store(store)
{
    // The wearable/mesh slots (the user's choice), in dressing-room order.
    auto add = [&](const QString& id, const QString& label, const QStringList& folders,
                   AttachMode mode, const QString& bone, bool weapon) {
        m_slots.push_back({id, label, folders, mode, bone, weapon});
    };
    add("shapeshift","Shapeshift Form", {"art/models/charactersfour/shapeshifters/"}, ReplaceBody, QString(), false);
    add("body",  "Body Armour", {"art/models/items/armours/bodyarmours/"}, InPlace, QString(), false);
    add("helmet","Helmet",      {"art/models/items/armours/helmets/"},     InPlace, QString(), false);
    add("gloves","Gloves",      {"art/models/items/armours/gloves/"},      InPlace, QString(), false);
    add("boots", "Boots",       {"art/models/items/armours/boots/"},       InPlace, QString(), false);
    add("cape",  "Back (Cape)", {"art/models/items/armours/capes/"},       InPlace, QString(), false);
    add("mainhand","Main Hand", {"art/models/items/weapons/onehandweapons/","art/models/items/weapons/twohandweapons/"},
        Bone, QStringLiteral("aux_R_Weapon_attachment_jntBnd"), true);
    add("offhand","Off Hand",   {"art/models/items/weapons/onehandweapons/","art/models/items/armours/shields/"},
        Bone, QStringLiteral("aux_L_Weapon_attachment_jntBnd"), true);

    // ── Layout: a scrollable control column on the left, the shared viewport on the right ──────────
    auto* root = new QHBoxLayout(this);
    auto* panel = new QWidget(this);
    auto* pv = new QVBoxLayout(panel);
    pv->setContentsMargins(8, 8, 8, 8);

    auto* clsRow = new QFormLayout();
    m_classBox = new QComboBox(panel);
    clsRow->addRow(QStringLiteral("Class"), m_classBox);
    m_outfitBox = new SearchableCombo(panel);
    clsRow->addRow(QStringLiteral("Outfit (cosmetic set)"), m_outfitBox);
    pv->addLayout(clsRow);

    m_classInfo = new QLabel(panel);
    m_classInfo->setWordWrap(true);
    m_classInfo->setStyleSheet(QStringLiteral("color:#9aa;"));
    pv->addWidget(m_classInfo);

    m_lockWeapons = new QCheckBox(QStringLiteral("Lock weapons to class (thematic)"), panel);
    m_lockWeapons->setToolTip(QStringLiteral(
        "PoE2 has no hard class-weapon restriction — this is a thematic filter (Warrior→maces, "
        "Ranger→bows, Mercenary→crossbows, Huntress→spears, Monk→quarterstaves, casters→wands/staves)."));
    pv->addWidget(m_lockWeapons);

    // Display toggles for the figure. Hair and Head default to shown; FX default to hidden (effects can
    // be visually noisy and cost extra meshes, so opt in).
    auto* toggleRow = new QHBoxLayout();
    m_showHair = new QCheckBox(QStringLiteral("Hair"), panel);   m_showHair->setChecked(true);
    m_showHead = new QCheckBox(QStringLiteral("Head"), panel);   m_showHead->setChecked(true);
    m_showFx   = new QCheckBox(QStringLiteral("FX"),   panel);   m_showFx->setChecked(false);
    m_showHair->setToolTip(QStringLiteral("Show the class's default hairstyle and beard."));
    m_showHead->setToolTip(QStringLiteral("Show the character's head/face (hide it to preview a full helmet)."));
    m_showFx->setToolTip(QStringLiteral("Show the glowing effect meshes that ship inside equipped cosmetic items."));
    toggleRow->addWidget(new QLabel(QStringLiteral("Show:"), panel));
    toggleRow->addWidget(m_showHair);
    toggleRow->addWidget(m_showHead);
    toggleRow->addWidget(m_showFx);
    toggleRow->addStretch(1);
    pv->addLayout(toggleRow);

    // Slot/outfit lists can carry hundreds of items, so each is a SearchableCombo: a normal
    // click-to-open dropdown that ALSO filters as you type (app/SearchableCombo.h). Each row also has a
    // small export button, and its label offers the same export menu on right-click.
    auto* slotBox = new QGroupBox(QStringLiteral("Slots"), panel);
    auto* slotForm = new QFormLayout(slotBox);
    for (const SlotDef& s : m_slots) {
        auto* cb = new SearchableCombo(slotBox);
        auto* exBtn = new QToolButton(slotBox);
        exBtn->setText(QStringLiteral("⤓"));
        exBtn->setToolTip(QStringLiteral("Export this slot's selected item…"));
        exBtn->setAutoRaise(true);
        auto* field = new QWidget(slotBox);
        auto* fl = new QHBoxLayout(field);
        fl->setContentsMargins(0, 0, 0, 0); fl->setSpacing(4);
        fl->addWidget(cb, 1); fl->addWidget(exBtn, 0);
        auto* lbl = new QLabel(s.label, slotBox);
        lbl->setContextMenuPolicy(Qt::CustomContextMenu);
        slotForm->addRow(lbl, field);
        m_slotCombos.push_back(cb);
        const int si = int(m_slotCombos.size()) - 1;
        connect(exBtn, &QToolButton::clicked, this,
                [this, si, exBtn]{ showSlotExportMenu(si, exBtn->mapToGlobal(exBtn->rect().bottomLeft())); });
        connect(lbl, &QLabel::customContextMenuRequested, this,
                [this, si, lbl](const QPoint& p){ showSlotExportMenu(si, lbl->mapToGlobal(p)); });
    }
    pv->addWidget(slotBox);

    auto* btnRow = new QHBoxLayout();
    m_resetBtn = new QPushButton(QStringLiteral("Reset to class default"), panel);
    m_randomBtn = new QPushButton(QStringLiteral("Random"), panel);
    m_randomBtn->setToolTip(QStringLiteral("Randomly fill every slot from the options available to this class."));
    btnRow->addWidget(m_resetBtn); btnRow->addWidget(m_randomBtn);
    pv->addLayout(btnRow);

    m_exportBtn = new QPushButton(QStringLiteral("Export outfit…"), panel);
    auto* exMenu = new QMenu(m_exportBtn);
    exMenu->addAction(QStringLiteral("Full outfit (one file)"), this, [this]{ m_exportMode = 0; exportOutfit(); });
    exMenu->addAction(QStringLiteral("Full outfit, no weapons"), this, [this]{ m_exportMode = 1; exportOutfit(); });
    exMenu->addAction(QStringLiteral("Each item separately…"), this, [this]{ m_exportMode = 2; exportOutfit(); });
    exMenu->addSeparator();
    exMenu->addAction(QStringLiteral("Original files (raw, every piece)…"), this, [this]{ m_exportMode = 3; exportOutfit(); });
    m_exportBtn->setMenu(exMenu);
    pv->addWidget(m_exportBtn);

    // Presets: save the current look and reload it later.
    auto* presetBox = new QGroupBox(QStringLiteral("Presets"), panel);
    auto* pl = new QVBoxLayout(presetBox);
    m_presetBox = new QComboBox(presetBox);
    m_presetBox->setToolTip(QStringLiteral("Saved outfits. Selecting one loads it."));
    pl->addWidget(m_presetBox);
    auto* pRow = new QHBoxLayout();
    m_savePresetBtn = new QPushButton(QStringLiteral("Save current…"), presetBox);
    m_delPresetBtn = new QPushButton(QStringLiteral("Delete"), presetBox);
    pRow->addWidget(m_savePresetBtn); pRow->addWidget(m_delPresetBtn);
    pl->addLayout(pRow);
    pv->addWidget(presetBox);
    pv->addStretch(1);

    auto* scroll = new QScrollArea(this);
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(320);
    scroll->setMaximumWidth(420);
    root->addWidget(scroll, 0);

    // Center column: the viewport with an animation bar beneath it.
    auto* center = new QWidget(this);
    auto* cv = new QVBoxLayout(center); cv->setContentsMargins(0, 0, 0, 0); cv->setSpacing(4);
    m_view = new GLModelWidget(center);
    // Characters are authored facing −Z, so the default orbit shows their back. Add a half-turn so the
    // dressing room opens on the FRONT of the character (the face), like an in-game character screen.
    m_view->setOrbitYaw(m_view->orbitYaw() + 3.14159265f);
    cv->addWidget(m_view, 1);
    buildPanels(this);                 // creates m_animBar + m_panelTabs (Info/Parts/Attachments/Animations)
    cv->addWidget(m_animBar);
    root->addWidget(center, 1);
    root->addWidget(m_panelTabs, 0);

    connect(m_classBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CustomizeTab::onClassChanged);
    connect(m_outfitBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CustomizeTab::onOutfitChanged);
    connect(m_lockWeapons, &QCheckBox::toggled, this, &CustomizeTab::onLockWeaponsToggled);
    // Display toggles just re-assemble the figure with the new visibility rules.
    connect(m_showHair, &QCheckBox::toggled, this, [this]{ if (m_ready) rebuildAssembly(); });
    connect(m_showHead, &QCheckBox::toggled, this, [this]{ if (m_ready) rebuildAssembly(); });
    connect(m_showFx,   &QCheckBox::toggled, this, [this]{ if (m_ready) rebuildAssembly(); });
    connect(m_resetBtn, &QPushButton::clicked, this, &CustomizeTab::resetToClassDefault);
    connect(m_randomBtn, &QPushButton::clicked, this, &CustomizeTab::randomize);
    connect(m_savePresetBtn, &QPushButton::clicked, this, &CustomizeTab::savePreset);
    connect(m_delPresetBtn, &QPushButton::clicked, this, &CustomizeTab::deletePreset);
    connect(m_presetBox, QOverload<int>::of(&QComboBox::activated), this, &CustomizeTab::applySelectedPreset);
    for (QComboBox* cb : m_slotCombos)
        connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CustomizeTab::onSlotChanged);
}

int CustomizeTab::slotIndexById(const QString& id) const
{
    for (int i = 0; i < m_slots.size(); ++i) if (m_slots[i].id == id) return i;
    return -1;
}

// Trailing attribute suffix of a mesh stem: strfourb-style variants end in one of these. Compound
// suffixes (strint/strdex/dexint) are tested before the single-attribute ones.
QString CustomizeTab::attrOfStem(const QString& stem)
{
    static const char* kCompound[] = {"strint","strdex","dexint"};
    for (const char* c : kCompound) if (stem.endsWith(QLatin1String(c))) return QString::fromLatin1(c);
    static const char* kSingle[] = {"str","dex","int"};
    for (const char* c : kSingle) if (stem.endsWith(QLatin1String(c))) return QString::fromLatin1(c);
    return QString();
}

// Meshes we never surface: LODs, dropped/ground variants, and monster-borrowed weapon art.
bool CustomizeTab::isNoiseMesh(const QString& path)
{
    // Not standalone wearables: LODs and ground/drop meshes; monster art; the sub-part meshes a parent
    // armour already includes (/attachments/ — skirts, sleeves, shoulderpads, beards, tassels); effect
    // meshes (/fx/, /vfx/, trails); and Maya-source (.mb) and localized (.japan/.korean/.thai/…)
    // duplicate exports.
    static const char* kBad[] = {
        "/monsters/", "/monster/", "/attachments/", "/attachment/", "/fx/", "/vfx/", "/particles/",
        "_lod", "/lod", "drop", "_attach", "trail",
        ".mb", ".japan", ".korean", ".thai", ".russian", ".german", ".french", ".spanish", ".portuguese"
    };
    for (const char* b : kBad) if (path.contains(QLatin1String(b))) return true;
    return false;
}

// Class table: data/balance/characters.datc64 — display name @8, base .ao @16, attribute tag @492.
// The 8 released classes are labelled; the 4 legacy/unreleased ones (models present, not playable) are
// flagged so the picker is honest about what they are.
void CustomizeTab::loadClasses()
{
    m_classes.clear();
    DatFile t;
    if (!t.load(m_store->readFile(QStringLiteral("data/balance/characters.datc64"))) || !t.isValid()) return;
    static const QSet<QString> released = {
        "Warrior","Witch","Ranger","Sorceress","Mercenary","Monk","Huntress","Druid"};
    for (uint32_t r = 0; r < t.rowCount(); ++r) {
        CharClass c;
        c.display = t.str(r, 8).trimmed();
        c.aoPath  = t.str(r, 16).trimmed().toLower();
        c.attr    = t.str(r, 492).trimmed().toLower();
        if (c.display.isEmpty() || c.aoPath.isEmpty()) continue;
        c.released = released.contains(c.display);
        m_classes.push_back(c);
    }
    // Released first, then the legacy set — both alphabetical.
    std::stable_sort(m_classes.begin(), m_classes.end(), [](const CharClass& a, const CharClass& b){
        if (a.released != b.released) return a.released;
        return a.display < b.display;
    });
}

// Every way to read an attribute out of a mesh stem: each of the six real body-type combos, at every
// position it occurs, with the base being the stem minus that occurrence. Family validation (below)
// then keeps only the reading that groups with sibling variants — so a stem that merely CONTAINS
// "int" (e.g. "pointtrails") is never mistaken for an Int item.
static QVector<QPair<QString,QString>> attrCandidates(const QString& stem)
{
    static const char* kCombos[] = {"strint","strdex","dexint","str","dex","int"};
    QVector<QPair<QString,QString>> out;
    for (const char* c : kCombos) {
        const QString tok = QString::fromLatin1(c);
        for (int at = stem.indexOf(tok); at >= 0; at = stem.indexOf(tok, at + 1))
            out.append({ stem.left(at) + stem.mid(at + tok.size()), tok });   // base, attr
    }
    return out;
}

// Fallback attribute for a mesh that family validation left as an orphan: a lone alternate ("…b")
// still carries its body type as a SUFFIX. Strip trailing decoration + one variant letter, then match
// an attribute at the true end — so "altarrobestrdexb" reads strdex while "pointtrails" (int mid-word)
// stays universal.
static QString suffixAttr(QString s)
{
    for (const char* dec : {"_l_armour","_r_armour","_armour","_rig","_attachment","_held"})
        if (s.endsWith(QLatin1String(dec))) { s.chop(int(qstrlen(dec))); break; }
    static const char* kCombos[] = {"strint","strdex","dexint","str","dex","int"};
    for (const char* c : kCombos) {
        const QString tok = QString::fromLatin1(c);
        if (s.endsWith(tok)) return tok;                                   // …<attr>
        if (s.size() > tok.size() + 0 && s.endsWith(tok + s.right(1)) && s.right(1).at(0).isLetter()
            && QString::fromLatin1("abr").contains(s.right(1))) return tok; // …<attr>b / …<attr>r / …<attr>a
    }
    return QString();
}

// A readable label from a family base: drop trailing decoration/variant marks, strip a redundant set
// name, spell the set out, title-case. Never leaves a bare "_" or a lone variant letter as the label.
static QString prettify(QString base, const QString& set)
{
    for (const char* dec : {"_l_armour","_r_armour","_armour","_rig","_attachment","_held","_model"})
        if (base.endsWith(QLatin1String(dec))) { base.chop(int(qstrlen(dec))); break; }
    if (!set.isEmpty() && base.startsWith(set)) base = base.mid(set.size());
    while (base.startsWith('_') || base.startsWith(' ')) base.remove(0, 1);
    while (base.endsWith('_')   || base.endsWith(' '))   base.chop(1);
    auto alnum = [](QString s){ return s.remove(QRegularExpression(QStringLiteral("[^a-z0-9]"))); };
    // A base that is empty, a lone variant letter, or already contained in the set name adds nothing.
    if (alnum(base).size() <= 1 || (!set.isEmpty() && alnum(set).contains(alnum(base)))) base.clear();
    auto spell = [](QString s){ s.replace('_', ' '); if (!s.isEmpty()) s[0] = s[0].toUpper(); return s; };
    const QString setLabel = spell(set);
    const QString baseLabel = spell(base);
    if (!setLabel.isEmpty() && !baseLabel.isEmpty()) return setLabel + QStringLiteral(" · ") + baseLabel;
    if (!setLabel.isEmpty()) return setLabel;
    return baseLabel;
}

// Sweep the index ONCE, classify every wearable .smd into its slot with a ROBUST body-type attribute
// (family-validated, not a naive suffix), and build the outfit list from real multi-piece armour sets
// only. Deduped per slot by (family name, attribute) so each item appears once.
void CustomizeTab::buildSlotCatalogue()
{
    m_optionsBySlot.clear();
    if (!m_store->isOpen()) return;
    const BundleIndex& idx = m_store->index();

    // Pass 1: collect the meshes of each slot, and note which armour slots each cosmetic set covers.
    struct Mesh { QString path, stem, set; };
    QHash<QString, QVector<Mesh>> bySlot;                 // slotId → meshes
    QMap<QString, QSet<QString>> setArmourSlots;          // set → armour slot ids it has pieces in
    static const QSet<QString> armourSlotIds = {"body","helmet","gloves","boots","cape"};
    const auto& files = idx.files();
    for (uint32_t i = 0; i < files.size(); ++i) {
        const uint16_t ext = files[i].extId;
        if (ext >= idx.extensions().size() || idx.extensions()[ext] != QStringLiteral(".smd")) continue;
        const QString path = idx.pathOf(i).toLower();
        if (isNoiseMesh(path)) continue;
        for (const SlotDef& s : m_slots) {
            bool in = false; for (const QString& f : s.folders) if (path.startsWith(f)) { in = true; break; }
            if (!in) continue;
            const int lastSlash = path.lastIndexOf('/');
            QString stem = path.mid(lastSlash + 1).chopped(4);
            static const QRegularExpression kHash(QStringLiteral("^[0-9a-f]{6,}$"));
            const int us = stem.lastIndexOf('_');
            if (us > 0 && kHash.match(stem.mid(us + 1)).hasMatch()) stem = stem.left(us);   // drop _<hash>
            // Wings live in the bodyarmours art folder but are BACK attachments — route them to the Back
            // slot (verified: they attach in place on the back exactly like a cape).
            QString slotId = path.contains(QStringLiteral("/wings/")) ? QStringLiteral("cape") : s.id;
            // In shapeshifters a "rig" mesh is the after-image effect (real forms are named), so skip it.
            // Everywhere else a generic mesh name — most weapons are "<weapon-folder>/rig_<hash>.smd" —
            // takes its identity from the PARENT FOLDER, so 1000+ weapons aren't lost as nameless "rig".
            if (stem == QStringLiteral("rig") && slotId == QStringLiteral("shapeshift")) break;
            if (stem == QStringLiteral("rig") || stem == QStringLiteral("model") || stem == QStringLiteral("mesh")) {
                const int prev = path.lastIndexOf('/', lastSlash - 1);
                if (prev >= 0) stem = path.mid(prev + 1, lastSlash - prev - 1);
            }
            QString set;
            const int mi = path.indexOf(QStringLiteral("/microtransactions/"));
            if (mi >= 0) { const int a = mi + 19, b = path.indexOf('/', a); if (b > a) set = path.mid(a, b - a); }
            bySlot[slotId].push_back({path, stem, set});
            if (!set.isEmpty() && armourSlotIds.contains(slotId)) setArmourSlots[set].insert(slotId);
            break;
        }
    }

    // Pass 2 per slot: tally attribute families, then give each mesh the reading that lands in the
    // largest confirmed family (≥2 distinct attributes); meshes in no family are universal (attr "").
    for (const SlotDef& s : m_slots) {
        const QVector<Mesh>& meshes = bySlot.value(s.id);
        QHash<QString, QSet<QString>> famAttrs;                       // base → distinct attrs
        QVector<QVector<QPair<QString,QString>>> cands(meshes.size());
        for (int m = 0; m < meshes.size(); ++m) {
            cands[m] = attrCandidates(meshes[m].stem);
            for (const auto& c : cands[m]) famAttrs[c.first].insert(c.second);
        }
        QSet<QString> seen;                                           // dedup: familyName|attr
        for (int m = 0; m < meshes.size(); ++m) {
            QString base, attr; int bestFam = 1;
            for (const auto& c : cands[m]) {
                const int fam = famAttrs.value(c.first).size();
                if (fam >= 2 && (fam > bestFam || (fam == bestFam && c.second.size() > attr.size()))) {
                    bestFam = fam; base = c.first; attr = c.second;
                }
            }
            if (base.isEmpty()) {                                     // orphan: try the true-end suffix
                attr = suffixAttr(meshes[m].stem);
                base = meshes[m].stem;
                if (!attr.isEmpty()) {                                // strip the attr from the label base
                    const int at = base.lastIndexOf(attr);
                    if (at >= 0) base = base.left(at) + base.mid(at + attr.size());
                }
            }
            const QString name = prettify(base, meshes[m].set);
            const QString key = name + '|' + attr;
            if (seen.contains(key)) continue;
            seen.insert(key);
            m_optionsBySlot[s.id].push_back({name, meshes[m].path, attr, meshes[m].set});
        }
        std::sort(m_optionsBySlot[s.id].begin(), m_optionsBySlot[s.id].end(),
                  [](const Option& a, const Option& b){ return a.name.localeAwareCompare(b.name) < 0; });

        // Env-gated diagnostic (POE2AB_CUSTOMIZE_AUDIT): per-slot attribute coverage + sample of the
        // universal (attribute-less) options, which are where mis-slotted effects / back items surface.
        if (qEnvironmentVariableIsSet("POE2AB_CUSTOMIZE_AUDIT")) {
            const auto& opts = m_optionsBySlot[s.id];
            int uni = 0; for (const Option& o : opts) if (o.attr.isEmpty()) ++uni;
            fprintf(stderr, "[audit] slot %-9s total=%4d universal(attr='')=%3d\n", qPrintable(s.id), opts.size(), uni);
            int k = 0; for (const Option& o : opts) if (o.attr.isEmpty() && k++ < 30) fprintf(stderr, "    U  %-40s  %s\n", qPrintable(o.name), qPrintable(o.smd));
        }
    }

    // Outfit list = only real cosmetic ARMOUR sets (pieces in ≥2 armour slots). This drops the hundreds
    // of single-item and weapon-only "sets" that were polluting the selector.
    QStringList setList;
    for (auto it = setArmourSlots.begin(); it != setArmourSlots.end(); ++it)
        if (it.value().size() >= 2) setList << it.key();
    setList.sort();
    QSignalBlocker b(m_outfitBox);
    m_outfitBox->clear();
    m_outfitBox->addItem(QStringLiteral("— none —"), QString());
    for (const QString& s : setList) { auto cap = s; if (!cap.isEmpty()) cap[0] = cap[0].toUpper(); m_outfitBox->addItem(cap, s); }
}

// Class .ao → body .smd, capturing the class's declared default-outfit pieces (hair/beard/starter
// clothes) so the assembled figure starts dressed as it does in-game. Each attached_object is
// "<bone> <childAo> alias <slot_attach>"; a "<root>" bone means the piece is authored in place.
bool CustomizeTab::resolveBodyMesh(const QString& aoPath, QString& smdOut, QString& astHint,
                                   QVector<DefaultPiece>& defaultsOut) const
{
    AssetText::AnimatedObject ao = AssetText::parseAo(m_store->readFile(aoPath));
    if (ao.smPath.isEmpty()) return false;
    astHint = ao.skeletonAst;
    AssetText::SkinnedMeshDesc sm = AssetText::parseSm(m_store->readFile(ao.smPath));
    smdOut = sm.smdPath;
    if (smdOut.isEmpty()) return false;

    for (const auto& at : ao.attachments) {
        QString child = at.aoPath, alias;
        const int ai = child.indexOf(QStringLiteral(" alias "));
        if (ai >= 0) { alias = child.mid(ai + 7).trimmed(); child = child.left(ai).trimmed(); }
        child = child.trimmed();
        if (child.isEmpty()) continue;
        AssetText::AnimatedObject cao = AssetText::parseAo(m_store->readFile(child));
        if (cao.smPath.isEmpty()) continue;
        AssetText::SkinnedMeshDesc csm = AssetText::parseSm(m_store->readFile(cao.smPath));
        if (csm.smdPath.isEmpty()) continue;
        DefaultPiece dp;
        dp.smd = csm.smdPath;
        dp.bone = (at.bone.compare(QStringLiteral("<root>"), Qt::CaseInsensitive) == 0) ? QString() : at.bone;
        dp.aliasSlot = alias;   // e.g. body_attach / gloves_attach / boots_attach / head_attach / face_attach
        defaultsOut.push_back(dp);
    }
    return true;
}

void CustomizeTab::onIndexReady()
{
    m_meshCache.clear();   // a new game build can change what lives at a given smd path
    loadClasses();
    QSignalBlocker b(m_classBox);
    m_classBox->clear();
    for (const CharClass& c : m_classes) {
        const QString tag = c.released ? QString() : QStringLiteral("  (unreleased)");
        m_classBox->addItem(c.display + tag, m_classBox->count());
    }
    // Slot options need the index too; names improve after onMaterialsReady.
    buildSlotCatalogue();
    reloadPresetList();
    m_ready = true;
    if (!m_classes.isEmpty()) { m_classBox->setCurrentIndex(0); onClassChanged(); }
}

void CustomizeTab::onMaterialsReady()
{
    // The slot meshes are body-type variant .smds that the in-game name tables don't cover (verified:
    // NameIndex returns nothing for them), so there is nothing to re-label — the catalogue built at
    // index time is final. Skipping a second full sweep here keeps the tab's load time down.
}

// Fill every slot combo with the options valid for the current class's attribute (plus attribute-less
// universal pieces), each starting at "— none —". Weapons additionally obey the lock toggle.
void CustomizeTab::repopulateSlotCombos()
{
    if (m_classBox->currentIndex() < 0 || m_classBox->currentIndex() >= m_classes.size()) return;
    const CharClass& cls = m_classes[m_classBox->currentIndex()];

    // Thematic weapon-type keywords per class (soft filter; PoE2 has no hard restriction).
    static const QHash<QString, QStringList> kThematic = {
        {"Warrior",  {"mace"}}, {"Ranger",  {"bow"}}, {"Huntress", {"spear"}},
        {"Mercenary",{"crossbow"}}, {"Monk", {"quarterstaff","warstaff","staff"}},
        {"Sorceress",{"wand","sceptre","staff","focus","foci"}},
        {"Witch",    {"wand","sceptre","staff","focus","foci"}},
        {"Druid",    {"staff","mace"}},
    };
    const QStringList weaponKeywords = kThematic.value(cls.display);
    const bool lock = m_lockWeapons->isChecked() && !weaponKeywords.isEmpty();

    m_syncing = true;
    for (int si = 0; si < m_slots.size(); ++si) {
        const SlotDef& s = m_slots[si];
        QComboBox* cb = m_slotCombos[si];
        const QString keep = cb->currentData().toString();   // preserve selection across refills
        cb->clear();
        cb->addItem(QStringLiteral("— none —"), QString());
        for (const Option& o : m_optionsBySlot.value(s.id)) {
            if (!o.attr.isEmpty() && o.attr != cls.attr) continue;         // wrong body-attribute variant
            if (s.weapon && lock) {
                bool ok = false; for (const QString& k : weaponKeywords) if (o.smd.contains(k)) { ok = true; break; }
                if (!ok) continue;
            }
            cb->addItem(o.name, o.smd);
        }
        const int at = keep.isEmpty() ? 0 : cb->findData(keep);
        cb->setCurrentIndex(at >= 0 ? at : 0);
    }
    m_syncing = false;
}

void CustomizeTab::onClassChanged()
{
    if (!m_ready || m_classBox->currentIndex() < 0 || m_classBox->currentIndex() >= m_classes.size()) return;
    const CharClass& cls = m_classes[m_classBox->currentIndex()];

    m_bodySmd.clear(); m_defaults.clear();
    QString astHint;
    const bool ok = resolveBodyMesh(cls.aoPath, m_bodySmd, astHint, m_defaults);
    m_classInfo->setText(QStringLiteral("%1 · %2%3%4")
        .arg(cls.display, cls.attr.toUpper(),
             cls.released ? QString() : QStringLiteral(" · unreleased"),
             ok ? QString() : QStringLiteral(" · body mesh not found")));
    if (ok) {
        ModelGeometry body; QString err;
        if (m_store->loadModel(m_bodySmd, body, &err))
            // decodeClips: the class rig carries idle/loop clips the Animations panel plays.
            m_skel = m_store->loadSkeletonFor(m_bodySmd, /*decodeClips*/true, body.jointPaletteSize());
    }
    // Populate the Animations panel: the move selector (this rig + the player animation library) is
    // built once; the clip list is rebuilt for the new class's own rig (source 0).
    if (m_animSourceBox) {
        if (m_animSourceBox->count() == 0) {
            m_animCats = m_store->playerAnimCategories();
            QSignalBlocker b(m_animSourceBox);
            m_animSourceBox->addItem(QStringLiteral("(this class rig)"));
            for (const AssetStore::AnimCategory& c : m_animCats) m_animSourceBox->addItem(c.name);
        }
        { QSignalBlocker b(m_animSourceBox); m_animSourceBox->setCurrentIndex(0); }
        m_libHeader = AstSkeleton::Skeleton{};
        populateClipList();
    }
    m_syncing = true;
    { QSignalBlocker b(m_outfitBox); m_outfitBox->setCurrentIndex(0); }
    m_syncing = false;
    repopulateSlotCombos();
    rebuildAssembly();
    emit status(QStringLiteral("Customize: %1 (%2)").arg(cls.display, cls.attr.toUpper()));
}

void CustomizeTab::onSlotChanged() { if (!m_syncing) rebuildAssembly(); }

void CustomizeTab::onLockWeaponsToggled() { if (m_ready) repopulateSlotCombos(); }

// Selecting a cosmetic set fills each slot with that set's attribute-matching piece (and clears slots
// the set does not cover). "— none —" leaves the current per-slot choices alone.
void CustomizeTab::onOutfitChanged()
{
    if (m_syncing) return;
    const QString set = m_outfitBox->currentData().toString();
    if (set.isEmpty()) return;
    if (m_classBox->currentIndex() < 0 || m_classBox->currentIndex() >= m_classes.size()) return;
    const CharClass& cls = m_classes[m_classBox->currentIndex()];

    m_syncing = true;
    for (int si = 0; si < m_slots.size(); ++si) {
        const SlotDef& s = m_slots[si];
        if (s.mode == ReplaceBody || s.weapon) continue;   // an armour set fills armour only, not weapons/shapeshift
        QComboBox* cb = m_slotCombos[si];
        QString pick;
        for (const Option& o : m_optionsBySlot.value(s.id))
            if (o.set == set && (o.attr.isEmpty() || o.attr == cls.attr)) { pick = o.smd; break; }
        const int at = pick.isEmpty() ? 0 : cb->findData(pick);
        cb->setCurrentIndex(at >= 0 ? at : 0);
    }
    m_syncing = false;
    rebuildAssembly();
    emit status(QStringLiteral("Customize: outfit ‘%1’").arg(set));
}

// ── Outfit presets (persisted to settings as a small JSON array) ─────────────────────────────────
static constexpr auto kPresetsKey = "customize/presets";
static QJsonArray loadPresets() {
    return QJsonDocument::fromJson(QSettings().value(QLatin1String(kPresetsKey)).toString().toUtf8()).array();
}
static void storePresets(const QJsonArray& a) {
    QSettings().setValue(QLatin1String(kPresetsKey), QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact)));
}

void CustomizeTab::reloadPresetList()
{
    QSignalBlocker b(m_presetBox);
    const QString keep = m_presetBox->currentText();
    m_presetBox->clear();
    m_presetBox->addItem(QStringLiteral("— saved outfits —"), QString());
    const QJsonArray a = loadPresets();
    for (const QJsonValue& v : a) { const QString n = v.toObject().value(QStringLiteral("name")).toString(); if (!n.isEmpty()) m_presetBox->addItem(n, n); }
    const int at = m_presetBox->findText(keep);
    m_presetBox->setCurrentIndex(at >= 0 ? at : 0);
    m_delPresetBtn->setEnabled(m_presetBox->count() > 1);
}

void CustomizeTab::savePreset()
{
    if (m_classBox->currentIndex() < 0 || m_classBox->currentIndex() >= m_classes.size()) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Save outfit preset"),
                            QStringLiteral("Name:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    QJsonObject slotMap;
    for (int si = 0; si < m_slots.size(); ++si) {
        const QString smd = m_slotCombos[si]->currentData().toString();
        if (!smd.isEmpty()) slotMap.insert(m_slots[si].id, smd);
    }
    QJsonObject preset;
    preset.insert(QStringLiteral("name"), name);
    preset.insert(QStringLiteral("class"), m_classes[m_classBox->currentIndex()].display);
    preset.insert(QStringLiteral("lock"), m_lockWeapons->isChecked());
    preset.insert(QStringLiteral("slots"), slotMap);

    QJsonArray a = loadPresets();
    for (int i = a.size() - 1; i >= 0; --i) if (a[i].toObject().value(QStringLiteral("name")).toString() == name) a.removeAt(i);
    a.append(preset);
    storePresets(a);
    reloadPresetList();
    { QSignalBlocker b(m_presetBox); const int at = m_presetBox->findData(name); if (at >= 0) m_presetBox->setCurrentIndex(at); }
    emit status(QStringLiteral("Saved outfit ‘%1’").arg(name));
}

void CustomizeTab::deletePreset()
{
    const QString name = m_presetBox->currentData().toString();
    if (name.isEmpty()) return;
    QJsonArray a = loadPresets();
    for (int i = a.size() - 1; i >= 0; --i) if (a[i].toObject().value(QStringLiteral("name")).toString() == name) a.removeAt(i);
    storePresets(a);
    reloadPresetList();
    emit status(QStringLiteral("Deleted outfit ‘%1’").arg(name));
}

void CustomizeTab::applySelectedPreset()
{
    const QString name = m_presetBox->currentData().toString();
    if (name.isEmpty()) return;
    QJsonObject preset;
    for (const QJsonValue& v : loadPresets()) if (v.toObject().value(QStringLiteral("name")).toString() == name) { preset = v.toObject(); break; }
    if (preset.isEmpty()) return;

    // Restore the class (repopulates the slot combos for its attribute), the lock, then each slot.
    const QString clsName = preset.value(QStringLiteral("class")).toString();
    int ci = -1; for (int i = 0; i < m_classes.size(); ++i) if (m_classes[i].display == clsName) { ci = i; break; }
    { QSignalBlocker b(m_lockWeapons); m_lockWeapons->setChecked(preset.value(QStringLiteral("lock")).toBool()); }
    if (ci >= 0 && ci != m_classBox->currentIndex()) m_classBox->setCurrentIndex(ci);   // triggers onClassChanged
    else onClassChanged();                                                              // same class → refresh anyway

    const QJsonObject slotMap = preset.value(QStringLiteral("slots")).toObject();
    m_syncing = true;
    for (int si = 0; si < m_slots.size(); ++si) {
        QComboBox* cb = m_slotCombos[si];
        const QString smd = slotMap.value(m_slots[si].id).toString();
        const int at = smd.isEmpty() ? 0 : cb->findData(smd);
        cb->setCurrentIndex(at >= 0 ? at : 0);
    }
    m_syncing = false;
    rebuildAssembly();
    emit status(QStringLiteral("Loaded outfit ‘%1’").arg(name));
}

void CustomizeTab::resetToClassDefault()
{
    m_syncing = true;
    { QSignalBlocker b(m_outfitBox); m_outfitBox->setCurrentIndex(0); }
    for (QComboBox* cb : m_slotCombos) cb->setCurrentIndex(0);
    m_syncing = false;
    rebuildAssembly();
}

// Randomly dress the character: each slot gets a random one of its available options (index 0 is
// "— none —", so ~1-in-N slots may come up empty, which keeps the results varied rather than always
// fully clad). Shapeshift is left out — it would replace the whole body and hide everything else.
void CustomizeTab::randomize()
{
    m_syncing = true;
    { QSignalBlocker b(m_outfitBox); m_outfitBox->setCurrentIndex(0); }
    for (int si = 0; si < m_slots.size(); ++si) {
        if (m_slots[si].id == QStringLiteral("shapeshift")) { m_slotCombos[si]->setCurrentIndex(0); continue; }
        QComboBox* cb = m_slotCombos[si];
        if (cb->count() <= 1) { cb->setCurrentIndex(0); continue; }
        cb->setCurrentIndex(int(QRandomGenerator::global()->bounded(cb->count())));
    }
    m_syncing = false;
    rebuildAssembly();
    emit status(QStringLiteral("Customize: randomized"));
}

// The meshes currently shown: the class's kept default cosmetics + each user-selected slot piece, with
// the bone each attaches to ("" = drawn in place / skinned to the body rig). Mirrors rebuildAssembly's
// selection logic so an export matches the preview exactly. Empty when a shapeshift form is active.
QVector<CustomizeTab::AssembledPiece> CustomizeTab::currentPieces() const
{
    QVector<AssembledPiece> pieces;
    auto slotFilled = [&](const QString& id) {
        const int i = slotIndexById(id); return i >= 0 && !m_slotCombos[i]->currentData().toString().isEmpty(); };
    const bool bodyFilled = slotFilled("body"), glovesFilled = slotFilled("gloves"), bootsFilled = slotFilled("boots");
    for (const DefaultPiece& d : m_defaults) {
        const QString a = d.aliasSlot;
        if (a.contains("body")   && bodyFilled)   continue;
        if (a.contains("gloves") && glovesFilled) continue;
        if (a.contains("boots")  && bootsFilled)  continue;
        if (m_showHair && !m_showHair->isChecked() && isHairDefault(a, d.smd)) continue;   // Hair toggle
        pieces.push_back({d.smd, d.bone, false});
    }
    for (int si = 0; si < m_slots.size(); ++si) {
        const SlotDef& s = m_slots[si];
        if (s.mode == ReplaceBody) continue;
        const QString smd = m_slotCombos[si]->currentData().toString();
        if (smd.isEmpty()) continue;
        pieces.push_back({smd, s.mode == Bone ? s.bone : QString(), s.weapon});
    }
    return pieces;
}

// Merge the body + pieces into one geometry for export. In-place pieces keep their own per-vertex
// skinning (they are authored to the same rig, so their joint indices are already valid against the
// body skeleton); bone pieces (weapons) bake rigidly to the named bone. Material/part indices are
// offset per piece. This is the exported twin of the viewport assembly.
ModelGeometry CustomizeTab::buildOutfitGeometry(bool includeWeapons, QString* err) const
{
    ModelGeometry merged;
    if (m_bodySmd.isEmpty()) { if (err) *err = QStringLiteral("no body loaded"); return merged; }
    if (!m_store->loadModel(m_bodySmd, merged, err)) return merged;

    for (const AssembledPiece& p : currentPieces()) {
        if (p.weapon && !includeWeapons) continue;
        ModelGeometry ag;
        if (!m_store->loadModel(p.smd, ag, nullptr) || ag.isEmpty()) continue;

        int bi = -1;
        if (!p.bone.isEmpty())
            for (int i = 0; i < m_skel.bones.size(); ++i)
                if (m_skel.bones[i].name.compare(p.bone, Qt::CaseInsensitive) == 0) { bi = i; break; }
        const bool bake = (bi >= 0 && bi <= 255);

        const uint32_t baseVert = uint32_t(merged.vertices.size());
        const int baseMat = merged.materialPaths.size();
        for (const QString& mp : ag.materialPaths) merged.materialPaths.append(mp);
        for (MeshVertex v : ag.vertices) {
            if (bake) {
                const RigMath::Mat4& T = m_skel.bones[bi].bind;
                float pt[3], nn[3], tt[3];
                RigMath::transformPoint(T, v.px, v.py, v.pz, pt);
                RigMath::transformDir(T, v.nx, v.ny, v.nz, nn);
                RigMath::transformDir(T, v.tx, v.ty, v.tz, tt);
                v.px=pt[0]; v.py=pt[1]; v.pz=pt[2]; v.nx=nn[0]; v.ny=nn[1]; v.nz=nn[2]; v.tx=tt[0]; v.ty=tt[1]; v.tz=tt[2];
                v.joints[0]=uint8_t(bi); v.joints[1]=v.joints[2]=v.joints[3]=0;
                v.weights[0]=1.0f; v.weights[1]=v.weights[2]=v.weights[3]=0.0f;
            }   // else: in-place piece — keep its authored positions + skinning (same rig)
            merged.vertices.append(v);
        }
        for (const MeshPart& part : ag.parts) {
            MeshPart np; np.name = part.name;
            np.indexStart = uint32_t(merged.indices.size());
            np.indexCount = part.indexCount;
            np.materialIndex = part.materialIndex >= 0 ? baseMat + part.materialIndex : -1;
            for (uint32_t k = 0; k < part.indexCount; ++k) merged.indices.append(baseVert + ag.indices[part.indexStart + k]);
            merged.parts.append(np);
        }
    }
    merged.skinned = true;
    merged.computeBounds();
    return merged;
}

// ── Inspection panels (ported from the Models tab, adapted to the dressing room) ──────────────────

void CustomizeTab::buildPanels(QWidget* parent)
{
    // Animation bar (under the viewport); hidden until a class with clips is active.
    m_animBar = new QWidget(parent);
    auto* ab = new QHBoxLayout(m_animBar); ab->setContentsMargins(0, 0, 0, 0);
    m_clipBox = new QComboBox(m_animBar);
    m_playBtn = new QToolButton(m_animBar); m_playBtn->setText(QStringLiteral("▶")); m_playBtn->setCheckable(true);
    m_playBtn->setToolTip(QStringLiteral("Play / pause"));
    m_timeline = new QSlider(Qt::Horizontal, m_animBar); m_timeline->setRange(0, 1000);
    m_timeLbl = new QLabel(QStringLiteral("0.00 / 0.00s"), m_animBar);
    ab->addWidget(new QLabel(QStringLiteral("Clip:"), m_animBar));
    ab->addWidget(m_clipBox, 1); ab->addWidget(m_playBtn); ab->addWidget(m_timeline, 2); ab->addWidget(m_timeLbl);
    m_animBar->setVisible(false);

    // Right-side tabbed panels.
    m_panelTabs = new QTabWidget(parent);
    m_panelTabs->setMinimumWidth(300);
    m_panelTabs->setMaximumWidth(430);

    m_partsTable = new QTableWidget(0, 3, m_panelTabs);
    m_partsTable->setHorizontalHeaderLabels({QStringLiteral("Part"), QStringLiteral("Tris"), QStringLiteral("Material")});
    m_partsTable->horizontalHeader()->setStretchLastSection(true);
    m_partsTable->verticalHeader()->setVisible(false);
    m_partsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_partsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // Animations tab: a "Move" selector (this class's own rig, or a player-animation-library move such
    // as sprint / attacks / a skill) above the clip list. Picking a move fills the list with that
    // move's clips; picking a clip retargets it onto the class rig and plays it on the merged figure.
    auto* animPanel = new QWidget(m_panelTabs);
    auto* apl = new QVBoxLayout(animPanel); apl->setContentsMargins(6, 6, 6, 6); apl->setSpacing(4);
    auto* srcRow = new QHBoxLayout(); srcRow->setContentsMargins(0, 0, 0, 0);
    srcRow->addWidget(new QLabel(QStringLiteral("Move:"), animPanel));
    m_animSourceBox = new QComboBox(animPanel);
    m_animSourceBox->setToolTip(QStringLiteral("A player move set (sprint, weapon attacks, skills…) or this class's own rig. "
                                               "The base rig carries only a static idle; the real motion is in the move sets."));
    srcRow->addWidget(m_animSourceBox, 1);
    apl->addLayout(srcRow);
    m_clipList = new QListWidget(animPanel);
    m_clipList->setToolTip(QStringLiteral("Clips of the selected move — click one to play it on the assembled character."));
    apl->addWidget(m_clipList, 1);

    m_attachPanel = new QListWidget(m_panelTabs);
    m_attachPanel->setToolTip(QStringLiteral("The pieces making up the figure — tick to show or hide each one."));

    m_infoPanel = new QLabel(m_panelTabs);
    m_infoPanel->setAlignment(Qt::AlignTop | Qt::AlignLeft); m_infoPanel->setWordWrap(true);
    m_infoPanel->setTextInteractionFlags(Qt::TextSelectableByMouse); m_infoPanel->setMargin(6);
    auto* infoScroll = new QScrollArea(m_panelTabs);
    infoScroll->setWidget(m_infoPanel); infoScroll->setWidgetResizable(true); infoScroll->setFrameShape(QFrame::NoFrame);

    m_panelTabs->addTab(m_partsTable,  QStringLiteral("Parts"));
    m_panelTabs->addTab(animPanel,     QStringLiteral("Animations"));
    m_panelTabs->addTab(m_attachPanel, QStringLiteral("Attachments"));
    m_panelTabs->addTab(infoScroll,    QStringLiteral("Info"));

    // Parts row → highlight that part in the viewport (only meaningful on the static body).
    connect(m_partsTable, &QTableWidget::itemSelectionChanged, this, [this] {
        if (m_animating) return;
        QSet<int> sel;
        for (QTableWidgetItem* it : m_partsTable->selectedItems())
            if (it->column() == 0) sel.insert(it->data(Qt::UserRole).toInt());
        m_view->setSelectedParts(sel);
    });
    // Attachment row tick → show/hide that piece (static assembly only; the animated figure is merged).
    connect(m_attachPanel, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
        if (m_animating) return;
        const int idx = it->data(Qt::UserRole).toInt();
        if (idx >= 0) m_view->setAttachmentVisible(idx, it->checkState() == Qt::Checked);
    });
    // Move selector → repopulate the clip list for that source (this rig, or a library move).
    connect(m_animSourceBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int row) {
        if (row < 0) return;
        onAnimSourceChanged(row);
    });
    // Clip list / combo drive playback: row 0 = bind pose (static assembly), any other = animate merged.
    connect(m_clipList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) return;
        { QSignalBlocker b(m_clipBox); m_clipBox->setCurrentIndex(row); }
        playAnimClip(row);
    });
    connect(m_clipBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int row) {
        if (row < 0) return;
        { QSignalBlocker b(m_clipList); m_clipList->setCurrentRow(row); }
        playAnimClip(row);
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
}

// Rebuild the Info / Parts / Attachments / Animations panels from the current (static) assembly.
void CustomizeTab::refreshPanels()
{
    if (!m_panelTabs) return;

    // Parts: the assembled body's own parts.
    const DecodedMesh& body = decodedFor(m_bodySmd);
    {
        QSignalBlocker b(m_partsTable);
        m_partsTable->setRowCount(body.ok ? body.geo.parts.size() : 0);
        for (int i = 0; body.ok && i < body.geo.parts.size(); ++i) {
            auto* n = new QTableWidgetItem(body.geo.parts[i].name);
            n->setData(Qt::UserRole, i);
            m_partsTable->setItem(i, 0, n);
            m_partsTable->setItem(i, 1, new QTableWidgetItem(QString::number(body.geo.parts[i].indexCount / 3)));
            m_partsTable->setItem(i, 2, new QTableWidgetItem(QFileInfo(body.geo.parts[i].material).fileName()));
        }
    }

    // Attachments: one row per drawn piece, ticked = visible.
    {
        QSignalBlocker b(m_attachPanel);
        m_attachPanel->clear();
        for (int i = 0; i < m_pieceLabels.size(); ++i) {
            auto* it = new QListWidgetItem(m_pieceLabels[i], m_attachPanel);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(Qt::Checked);
            it->setData(Qt::UserRole, i);
        }
        if (m_pieceLabels.isEmpty())
            (new QListWidgetItem(QStringLiteral("(body only — no extra pieces)"), m_attachPanel))->setFlags(Qt::ItemIsEnabled);
        m_panelTabs->setTabText(2, QStringLiteral("Attachments (%1)").arg(m_pieceLabels.size()));
    }

    // Info: a short summary of the figure.
    {
        const QString cls = (m_classBox->currentIndex() >= 0 && m_classBox->currentIndex() < m_classes.size())
                            ? m_classes[m_classBox->currentIndex()].display : QStringLiteral("—");
        long verts = body.ok ? body.geo.vertices.size() : 0;
        int parts = body.ok ? body.geo.parts.size() : 0;
        QStringList lines;
        lines << QStringLiteral("<b>%1</b>").arg(cls);
        lines << QStringLiteral("body: %1").arg(m_bodySmd);
        lines << QStringLiteral("%1 body part(s) · %2 verts").arg(parts).arg(verts);
        lines << QStringLiteral("%1 equipped/default piece(s)").arg(m_pieceLabels.size());
        if (!m_pieceLabels.isEmpty()) { lines << QStringLiteral("<b>Pieces:</b>"); for (const QString& l : m_pieceLabels) lines << QStringLiteral("• %1").arg(l); }
        m_infoPanel->setText(lines.join(QStringLiteral("<br>")));
    }

    // Animations: the clip list and tab label are owned by populateClipList()/onAnimSourceChanged()
    // (the move selector chooses the source), so nothing to do here.
}

// The clip list for the current move source. Row 0 is always "(bind pose)". Source 0 = the class's
// own rig (m_skel, typically just the static idle); source i>0 = m_animCats[i-1], whose clip names
// come from its .ast header (parsed cheaply, no key decode).
void CustomizeTab::populateClipList()
{
    if (!m_clipList || !m_clipBox) return;
    QSignalBlocker b1(m_clipList), b2(m_clipBox);
    m_clipList->clear(); m_clipBox->clear();
    m_clipList->addItem(QStringLiteral("(bind pose)"));
    m_clipBox->addItem(QStringLiteral("(bind pose)"));

    const int src = m_animSourceBox ? m_animSourceBox->currentIndex() : 0;
    const QVector<AstSkeleton::Clip>& clips = (src <= 0) ? m_skel.clips : m_libHeader.clips;
    for (const AstSkeleton::Clip& c : clips) {
        const QString n = c.name.isEmpty() ? QStringLiteral("clip") : c.name;
        m_clipList->addItem(n); m_clipBox->addItem(n);
    }
    const bool haveClips = !clips.isEmpty();
    if (!haveClips) m_clipList->addItem(src <= 0 ? QStringLiteral("(no clips on this rig)")
                                                 : QStringLiteral("(move has no clips)"));
    m_clipList->setEnabled(true);
    m_animBar->setVisible(haveClips || !m_skel.clips.isEmpty());
    m_clipList->setCurrentRow(0);
    m_clipBox->setCurrentIndex(0);
    m_panelTabs->setTabText(1, haveClips ? QStringLiteral("Animations (%1)").arg(clips.size())
                                         : QStringLiteral("Animations"));
}

// Move selector changed: for a library move, parse its .ast header (clip names only) and list them;
// for the class rig, list its own clips. Selecting a source resets to the bind pose.
void CustomizeTab::onAnimSourceChanged(int sourceRow)
{
    if (sourceRow > 0 && sourceRow - 1 < m_animCats.size()) {
        m_libHeader = m_store->playerAnimHeader(m_animCats[sourceRow - 1].path);
        if (!m_libHeader.valid)
            emit status(QStringLiteral("Couldn't read move ‘%1’.").arg(m_animCats[sourceRow - 1].name));
    } else {
        m_libHeader = AstSkeleton::Skeleton{};
    }
    exitAnimation();          // back to the static assembly while a new move is chosen
    populateClipList();
}

// Resolve the chosen clip row against the current source, retarget a library clip onto the class rig
// by bone name, and play it on the merged figure. Row 0 = bind pose (static assembly).
void CustomizeTab::playAnimClip(int clipRow)
{
    if (clipRow <= 0) { exitAnimation(); return; }
    const int src = m_animSourceBox ? m_animSourceBox->currentIndex() : 0;
    const int ci = clipRow - 1;

    if (src <= 0) {                                   // the class's own rig
        if (ci < 0 || ci >= m_skel.clips.size()) return;
        m_playSkel = m_skel;
        enterAnimationWith(m_playSkel, ci);
        return;
    }
    // Library move: decode just this clip, retarget it onto the class rig, play index 0.
    if (src - 1 >= m_animCats.size() || ci < 0 || ci >= m_libHeader.clips.size()) return;
    if (m_skel.bones.isEmpty()) { emit status(QStringLiteral("This class has no rig to animate.")); return; }
    AstSkeleton::Skeleton lib = m_store->playerAnimClip(m_animCats[src - 1].path, ci);
    if (ci >= lib.clips.size() || lib.clips[ci].keys.isEmpty()) {
        emit status(QStringLiteral("That clip has no motion data.")); return;
    }
    AstSkeleton::Clip rc = AstSkeleton::retargetClip(lib, lib.clips[ci], m_skel);
    if (rc.keys.isEmpty()) { emit status(QStringLiteral("That move doesn't match this class's rig.")); return; }
    m_playSkel = m_skel;
    m_playSkel.clips = { rc };
    m_playSkel.clipsDecoded = true;
    enterAnimationWith(m_playSkel, 0);
}

// Show the MERGED, fully-skinned figure and play `skel`'s clip `clipIndex` on it (so worn armour
// deforms with the body, unlike the rigid-attachment static assembly).
void CustomizeTab::enterAnimationWith(const AstSkeleton::Skeleton& skel, int clipIndex)
{
    if (m_bodySmd.isEmpty() || clipIndex < 0 || clipIndex >= skel.clips.size()) return;
    QString e;
    ModelGeometry merged = buildOutfitGeometry(/*includeWeapons*/true, &e);
    if (merged.isEmpty()) { emit status(QStringLiteral("Can't animate: %1").arg(e)); return; }
    m_animating = true;
    m_view->setModel(merged, skel);
    // Head toggle still applies to the merged figure's own head parts.
    if (!m_showHead->isChecked()) {
        QSet<int> headParts;
        for (int i = 0; i < merged.parts.size(); ++i) if (isHeadPartName(merged.parts[i].name)) headParts.insert(i);
        m_view->setHiddenParts(headParts);
    } else m_view->setHiddenParts({});
    const QVector<GlbExporter::ExportMaterial> mats = m_store->resolveMaterials(merged, true);
    QVector<GLModelWidget::MaterialTextures> tex(merged.materialPaths.size());
    for (int i = 0; i < mats.size() && i < tex.size(); ++i) {
        tex[i].baseColor=mats[i].baseColor; tex[i].normal=mats[i].normal; tex[i].metalRough=mats[i].metallicRoughness;
        tex[i].emissive=mats[i].emissive; tex[i].specColor=mats[i].specularColor;
        for (int c=0;c<3;++c) tex[i].subsurface[c]=mats[i].subsurface[c];
        tex[i].translucent=mats[i].transmissionFactor; tex[i].alphaMode=mats[i].alphaMode;
        tex[i].isFur=mats[i].isFur; tex[i].furNoise=mats[i].furNoise; tex[i].furMask=mats[i].furMask; tex[i].furDepth=mats[i].furDepth;
    }
    m_view->setMaterialTextures(tex);
    m_view->clearAttachments();
    m_view->setClip(clipIndex);
    m_view->setPlaying(true);
    QSignalBlocker bp(m_playBtn); m_playBtn->setChecked(true); m_playBtn->setText(QStringLiteral("❚❚"));
}

// Leave animation and restore the interactive attachment assembly (per-piece toggles, fast).
void CustomizeTab::exitAnimation()
{
    const bool wasAnimating = m_animating;
    m_animating = false;
    QSignalBlocker bp(m_playBtn); m_playBtn->setChecked(false); m_playBtn->setText(QStringLiteral("▶"));
    if (wasAnimating) rebuildAssembly();   // rebuild the static figure only if we were showing the merged one
}

// Export the assembled outfit. Modes: 0 = full outfit (one file), 1 = full outfit without weapons,
// 2 = each equipped item as its own file in a chosen folder. Uses the same GlbExporter + Settings
// defaults as the Models tab (format, unit scale, loose textures…).
void CustomizeTab::exportOutfit()
{
    if (!m_store->isOpen() || m_bodySmd.isEmpty()) { emit status(QStringLiteral("Pick a class first.")); return; }

    GlbExporter::Options opt;
    opt.unitScale = float(Config::exportUnitScale());
    opt.includeSkeleton = Config::exportSkeleton();
    opt.includeAnimations = false;                 // the assembled outfit is a static pose
    opt.embedTextures = Config::exportEmbedTextures();
    opt.looseTextures = Config::exportLooseTextures();
    opt.reconstructNormalZ = Config::exportReconstructNormalZ();
    const bool gltf = Config::exportGltf();
    const QString ext = gltf ? QStringLiteral(".gltf") : QStringLiteral(".glb");
    const QString base = Config::lastExportDir().isEmpty() ? QDir::homePath() : Config::lastExportDir();
    const QString cls = (m_classBox->currentIndex() >= 0 && m_classBox->currentIndex() < m_classes.size())
                        ? m_classes[m_classBox->currentIndex()].display.toLower() : QStringLiteral("character");

    if (m_exportMode == 3) {
        // Non-destructive: the EXACT original game files (mesh/.sm/.mat/.dds) of the body + every piece.
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Extract original files to folder"), base);
        if (dir.isEmpty()) return;
        Config::setLastExportDir(dir);
        QStringList models; models << m_bodySmd;
        for (const AssembledPiece& p : currentPieces()) models << p.smd;
        int files = 0, fail = 0; QSet<QString> written;
        for (const QString& mp : models) {
            for (const QString& gp : m_store->collectAssetFiles(mp, true, true)) {
                if (written.contains(gp)) continue;
                written.insert(gp);
                const QByteArray bytes = m_store->readFile(gp, nullptr);
                if (bytes.isEmpty()) { ++fail; continue; }
                const QString outPath = QDir(dir).filePath(gp);
                QDir().mkpath(QFileInfo(outPath).absolutePath());
                QFile f(outPath);
                if (f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size()) ++files; else ++fail;
            }
        }
        emit status(QStringLiteral("Extracted %1 original file%2%3").arg(files)
                    .arg(files == 1 ? QString() : QStringLiteral("s"))
                    .arg(fail ? QStringLiteral(" (%1 failed)").arg(fail) : QString()));
        return;
    }
    if (m_exportMode == 2) {
        // Each item separately: the body + every equipped piece as its own file in a folder.
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Export each item to folder"), base);
        if (dir.isEmpty()) return;
        Config::setLastExportDir(dir);
        int ok = 0, fail = 0;
        auto writeOne = [&](const QString& smd, const QString& label) {
            ModelGeometry g; QString e;
            if (!m_store->loadModel(smd, g, &e)) { ++fail; return; }
            AstSkeleton::Skeleton sk = m_store->loadSkeletonFor(smd, false, g.jointPaletteSize());
            const QVector<GlbExporter::ExportMaterial> mats = m_store->resolveMaterials(g, true);
            QString name = QStringLiteral("%1_%2").arg(cls, label);
            QString out = QDir(dir).filePath(name + ext);
            for (int n = 1; QFileInfo::exists(out); ++n) out = QDir(dir).filePath(QStringLiteral("%1_%2%3").arg(name).arg(n).arg(ext));
            if (GlbExporter::write(g, sk, mats, opt, out, &e)) ++ok; else ++fail;
        };
        writeOne(m_bodySmd, QStringLiteral("body"));
        for (const AssembledPiece& p : currentPieces()) writeOne(p.smd, QFileInfo(p.smd).baseName());
        emit status(QStringLiteral("Exported %1 item%2 to %3%4").arg(ok).arg(ok==1?QString():QStringLiteral("s"))
                    .arg(QDir(dir).dirName()).arg(fail?QStringLiteral(" (%1 failed)").arg(fail):QString()));
        return;
    }

    // Full outfit (with or without weapons) → one merged file.
    const bool withWeapons = (m_exportMode == 0);
    QString e;
    ModelGeometry merged = buildOutfitGeometry(withWeapons, &e);
    if (merged.isEmpty()) { emit status(QStringLiteral("Nothing to export: %1").arg(e)); return; }
    const QString suffix = withWeapons ? QStringLiteral("outfit") : QStringLiteral("outfit_noweapons");
    const QString suggested = QDir(base).filePath(QStringLiteral("%1_%2%3").arg(cls, suffix, ext));
    const QString filter = gltf ? QStringLiteral("glTF (*.gltf)") : QStringLiteral("glTF binary (*.glb)");
    QString out = QFileDialog::getSaveFileName(this, QStringLiteral("Export outfit"), suggested, filter);
    if (out.isEmpty()) return;
    if (!out.toLower().endsWith(ext)) out += ext;
    Config::setLastExportDir(QFileInfo(out).absolutePath());
    const QVector<GlbExporter::ExportMaterial> mats = m_store->resolveMaterials(merged, true);
    if (GlbExporter::write(merged, m_skel, mats, opt, out, &e))
        emit status(QStringLiteral("Exported %1 (%2 parts)").arg(QFileInfo(out).fileName()).arg(merged.parts.size()));
    else
        emit status(QStringLiteral("Export failed: %1").arg(e));
}

// The per-slot export menu (from the row's ⤓ button or a right-click on the slot name). Offers the
// selected item as a converted .glb/.gltf, or its exact original files. Disabled when the slot is empty.
void CustomizeTab::showSlotExportMenu(int slotIndex, const QPoint& globalPos)
{
    if (slotIndex < 0 || slotIndex >= m_slotCombos.size()) return;
    const QString smd = m_slotCombos[slotIndex]->currentData().toString();
    const QString label = slotIndex < m_slots.size() ? m_slots[slotIndex].label : QString();
    QMenu menu(this);
    QAction* conv = menu.addAction(QStringLiteral("Export %1 item (model)…").arg(label));
    QAction* raw  = menu.addAction(QStringLiteral("Export %1 original files (raw)…").arg(label));
    if (smd.isEmpty()) {
        conv->setEnabled(false); raw->setEnabled(false);
        menu.addSeparator();
        menu.addAction(QStringLiteral("(nothing selected in this slot)"))->setEnabled(false);
    }
    connect(conv, &QAction::triggered, this, [this, slotIndex]{ exportSlot(slotIndex, /*raw*/false); });
    connect(raw,  &QAction::triggered, this, [this, slotIndex]{ exportSlot(slotIndex, /*raw*/true); });
    menu.exec(globalPos);
}

// Export just ONE slot's selected item: either a converted model file, or its exact original game files
// (mesh/.sm/.mat/.dds), the same non-destructive extraction the Models tab and outfit export offer.
void CustomizeTab::exportSlot(int slotIndex, bool rawOriginals)
{
    if (slotIndex < 0 || slotIndex >= m_slotCombos.size() || !m_store) return;
    const QString smd = m_slotCombos[slotIndex]->currentData().toString();
    if (smd.isEmpty()) { emit status(QStringLiteral("That slot has no item selected.")); return; }
    const QString label = (slotIndex < m_slots.size() ? m_slots[slotIndex].label : QStringLiteral("item")).toLower().replace(QLatin1Char(' '), QLatin1Char('_'));
    const QString base = Config::lastExportDir().isEmpty() ? QDir::homePath() : Config::lastExportDir();

    if (rawOriginals) {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Extract original files to folder"), base);
        if (dir.isEmpty()) return;
        Config::setLastExportDir(dir);
        int files = 0, fail = 0;
        for (const QString& gp : m_store->collectAssetFiles(smd, /*textures*/true, /*skeleton*/true)) {
            const QByteArray bytes = m_store->readFile(gp, nullptr);
            if (bytes.isEmpty()) { ++fail; continue; }
            const QString outPath = QDir(dir).filePath(gp);
            QDir().mkpath(QFileInfo(outPath).absolutePath());
            QFile f(outPath);
            if (f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size()) ++files; else ++fail;
        }
        emit status(QStringLiteral("Extracted %1 original file%2 for %3%4").arg(files)
                    .arg(files == 1 ? QString() : QStringLiteral("s")).arg(label)
                    .arg(fail ? QStringLiteral(" (%1 failed)").arg(fail) : QString()));
        return;
    }

    // Converted model export (honours the shared export settings, like the outfit export).
    GlbExporter::Options opt;
    opt.unitScale = float(Config::exportUnitScale());
    opt.includeSkeleton = Config::exportSkeleton();
    opt.includeAnimations = false;
    opt.embedTextures = Config::exportEmbedTextures();
    opt.looseTextures = Config::exportLooseTextures();
    opt.reconstructNormalZ = Config::exportReconstructNormalZ();
    const bool gltf = Config::exportGltf();
    const QString ext = gltf ? QStringLiteral(".gltf") : QStringLiteral(".glb");
    ModelGeometry g; QString e;
    if (!m_store->loadModel(smd, g, &e)) { emit status(QStringLiteral("Couldn't load the item: %1").arg(e)); return; }
    AstSkeleton::Skeleton sk = m_store->loadSkeletonFor(smd, false, g.jointPaletteSize());
    const QVector<GlbExporter::ExportMaterial> mats = m_store->resolveMaterials(g, true);
    const QString suggested = QDir(base).filePath(QStringLiteral("%1_%2%3").arg(label, QFileInfo(smd).baseName(), ext));
    const QString filter = gltf ? QStringLiteral("glTF (*.gltf)") : QStringLiteral("glTF binary (*.glb)");
    QString out = QFileDialog::getSaveFileName(this, QStringLiteral("Export %1 item").arg(label), suggested, filter);
    if (out.isEmpty()) return;
    if (!out.toLower().endsWith(ext)) out += ext;
    Config::setLastExportDir(QFileInfo(out).absolutePath());
    if (GlbExporter::write(g, sk, mats, opt, out, &e))
        emit status(QStringLiteral("Exported %1").arg(QFileInfo(out).fileName()));
    else
        emit status(QStringLiteral("Export failed: %1").arg(e));
}

// Push the assembled figure to the shared viewport. A shapeshift form REPLACES the body; otherwise the
// class body is the main mesh, the class's default cosmetics fill any slot the user left empty, and each
// chosen piece composites in place (worn armour) or on its weapon socket (weapons).

// Load + decode a mesh once and keep it: assembling the figure touches the body and every worn piece,
// and a single slot change re-assembles all of them. The cache turns the second and later views of a
// piece into a hash lookup instead of an Oodle decompress + DDS decode + ORM build. Keyed by smd path;
// bounded so a long browsing session can't grow it without limit (a plain perf cache — dropping it
// only costs a re-decode).
const CustomizeTab::DecodedMesh& CustomizeTab::decodedFor(const QString& smd)
{
    auto it = m_meshCache.find(smd);
    if (it != m_meshCache.end()) return it.value();
    if (m_meshCache.size() >= 96) m_meshCache.clear();   // cap decoded-texture memory
    DecodedMesh dm;
    ModelGeometry g;
    if (m_store->loadModel(smd, g, nullptr)) {
        const QVector<GlbExporter::ExportMaterial> mats = m_store->resolveMaterials(g, /*decode*/true);
        dm.tex.resize(g.materialPaths.size());
        for (int i = 0; i < mats.size() && i < dm.tex.size(); ++i) {
            dm.tex[i].baseColor=mats[i].baseColor; dm.tex[i].normal=mats[i].normal; dm.tex[i].metalRough=mats[i].metallicRoughness;
            dm.tex[i].emissive=mats[i].emissive; dm.tex[i].specColor=mats[i].specularColor;
            for (int c=0;c<3;++c) dm.tex[i].subsurface[c]=mats[i].subsurface[c];
            dm.tex[i].translucent=mats[i].transmissionFactor; dm.tex[i].alphaMode=mats[i].alphaMode;
            dm.tex[i].isFur=mats[i].isFur; dm.tex[i].furNoise=mats[i].furNoise; dm.tex[i].furMask=mats[i].furMask; dm.tex[i].furDepth=mats[i].furDepth;
        }
        dm.geo = std::move(g);
        dm.ok = true;
    }
    return *m_meshCache.insert(smd, dm);
}

// An equipped cosmetic's effect meshes live in an /fx/ subtree of its item folder (measured:
// .../<set>/fx/**.smd for MTX pieces, or an /fx sibling of the mesh's own folder). Returns those .smd,
// which draw in place on the same rig as the item.
QStringList CustomizeTab::fxMeshesFor(const QString& equippedSmd)
{
    QStringList out;
    if (equippedSmd.isEmpty() || !m_store) return out;
    const QString p = equippedSmd.toLower();
    QStringList fxRoots;
    const QString dir = p.section(QLatin1Char('/'), 0, -2);
    if (!dir.isEmpty()) fxRoots << dir + QStringLiteral("/fx");
    const int mtx = p.indexOf(QStringLiteral("/microtransactions/"));
    if (mtx >= 0) {
        const int setEnd = p.indexOf(QLatin1Char('/'), mtx + 19);
        if (setEnd > 0) fxRoots << p.left(setEnd) + QStringLiteral("/fx");
    }
    fxRoots.removeDuplicates();
    QSet<QString> seen;
    for (const QString& r : fxRoots)
        for (const QString& m : m_store->filesUnderPrefix(r, QStringLiteral(".smd"))) {
            // Keep the fx meshes themselves (isNoiseMesh would drop all /fx/ paths), but still skip LOD
            // variants and locale dupes so an effect isn't drawn several times over.
            const QString lm = m.toLower();
            if (lm.contains(QStringLiteral("_lod")) || lm.contains(QStringLiteral("/lod"))) continue;
            if (!seen.contains(m)) { seen.insert(m); out << m; }
        }
    return out;
}

void CustomizeTab::rebuildAssembly()
{
    // rebuildAssembly always produces the STATIC, per-piece assembly, so it also leaves animation mode
    // and snaps the clip bar back to bind pose (signals blocked so this doesn't re-enter).
    m_animating = false;
    m_pieceLabels.clear();
    if (m_clipList && m_clipBox) {
        QSignalBlocker b1(m_clipList), b2(m_clipBox);
        if (m_clipList->count() > 0) m_clipList->setCurrentRow(0);
        m_clipBox->setCurrentIndex(0);
    }
    if (m_playBtn) { QSignalBlocker b(m_playBtn); m_playBtn->setChecked(false); m_playBtn->setText(QStringLiteral("▶")); }

    if (m_bodySmd.isEmpty()) { m_view->clearModel(); refreshPanels(); return; }
    QString err;

    // Shapeshift override: whole-body replacement with its own rig.
    const int ssIdx = slotIndexById(QStringLiteral("shapeshift"));
    if (ssIdx >= 0) {
        const QString ss = m_slotCombos[ssIdx]->currentData().toString();
        if (!ss.isEmpty()) {
            const DecodedMesh& dm = decodedFor(ss);
            if (dm.ok) {
                AstSkeleton::Skeleton sk = m_store->loadSkeletonFor(ss, false, dm.geo.jointPaletteSize());
                m_view->setModel(dm.geo, sk);
                m_view->setMaterialTextures(dm.tex);
                m_view->clearAttachments();
                m_view->frameAll();
                refreshPanels();
                return;
            }
        }
    }

    // Body.
    const DecodedMesh& bodyDm = decodedFor(m_bodySmd);
    if (!bodyDm.ok) { m_view->clearModel(); refreshPanels(); return; }
    m_view->setModel(bodyDm.geo, m_skel);
    m_view->setMaterialTextures(bodyDm.tex);
    // Head toggle: hide the base body's head/face parts (skull, jaw, ears…) when the user wants to see
    // a full helmet unobstructed. The parts are the body mesh's own — a per-part hide, not a mesh swap.
    if (!m_showHead->isChecked()) {
        QSet<int> headParts;
        for (int i = 0; i < bodyDm.geo.parts.size(); ++i)
            if (isHeadPartName(bodyDm.geo.parts[i].name)) headParts.insert(i);
        m_view->setHiddenParts(headParts);
    } else {
        m_view->setHiddenParts({});
    }

    // Which slots did the user fill? Their default-outfit counterpart is then hidden.
    auto slotFilled = [&](const QString& id) {
        const int i = slotIndexById(id); return i >= 0 && !m_slotCombos[i]->currentData().toString().isEmpty(); };
    const bool bodyFilled = slotFilled("body"), glovesFilled = slotFilled("gloves"), bootsFilled = slotFilled("boots");

    QVector<GLModelWidget::Attachment> draw;
    QStringList drawnSmds;   // the pieces actually placed (for FX resolution below)
    auto addMesh = [&](const QString& smd, const QString& bone, const QString& label) {
        if (smd.isEmpty()) return;
        const DecodedMesh& dm = decodedFor(smd);
        if (!dm.ok) return;
        GLModelWidget::Attachment att;
        att.geo = dm.geo; att.mats = dm.tex; att.bone = bone; att.label = label; att.visible = true;
        draw.push_back(att);
        drawnSmds << smd.toLower();
        m_pieceLabels << label;
    };

    // Class default cosmetics: always keep hair/beard; keep default body/gloves/boots only where the
    // user has not chosen a replacement for that slot.
    for (const DefaultPiece& d : m_defaults) {
        const QString a = d.aliasSlot;
        if (a.contains("body")   && bodyFilled)   continue;
        if (a.contains("gloves") && glovesFilled) continue;
        if (a.contains("boots")  && bootsFilled)  continue;
        if (!m_showHair->isChecked() && isHairDefault(a, d.smd)) continue;   // Hair toggle
        addMesh(d.smd, d.bone, QStringLiteral("default:%1").arg(a));
    }

    // User selections.
    for (int si = 0; si < m_slots.size(); ++si) {
        const SlotDef& s = m_slots[si];
        if (s.mode == ReplaceBody) continue;
        const QString smd = m_slotCombos[si]->currentData().toString();
        if (smd.isEmpty()) continue;
        addMesh(smd, s.mode == Bone ? s.bone : QString(), s.label);
    }

    // FX toggle: add each drawn item's own /fx/ effect meshes (drawn in place on the same rig). Opt-in,
    // so it costs nothing until the user asks for it.
    if (m_showFx->isChecked()) {
        QSet<QString> fxSeen;
        const QStringList base = drawnSmds;   // snapshot — addMesh appends as we go
        for (const QString& e : base)
            for (const QString& fx : fxMeshesFor(e))
                if (!fxSeen.contains(fx)) { fxSeen.insert(fx); addMesh(fx, QString(), QStringLiteral("fx")); }
    }

    m_view->setAttachments(draw);
    m_view->frameAll();
    refreshPanels();
}
