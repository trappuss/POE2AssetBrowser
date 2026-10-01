#include "model/AssetText.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

QString AssetText::decodeText(const QByteArray& data)
{
    if (data.size() >= 2) {
        const uint8_t b0 = uint8_t(data[0]), b1 = uint8_t(data[1]);
        if (b0 == 0xFF && b1 == 0xFE) return QString::fromUtf16(reinterpret_cast<const char16_t*>(data.constData() + 2), (data.size() - 2) / 2);
        // No BOM but a UTF-16LE ASCII first char looks like "X\0".
        if (b1 == 0 && b0 != 0) return QString::fromUtf16(reinterpret_cast<const char16_t*>(data.constData()), data.size() / 2);
    }
    return QString::fromUtf8(data);
}

AssetText::SkinnedMeshDesc AssetText::parseSm(const QByteArray& data)
{
    SkinnedMeshDesc d;
    const QStringList lines = decodeText(data).split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    for (int i = 0; i < lines.size(); ++i) {
        const QString t = lines[i].trimmed();
        if (t.startsWith(QStringLiteral("version"))) d.version = t.mid(7).trimmed().toInt();
        else if (t.startsWith(QStringLiteral("SkinnedMeshData"))) {
            const int q = t.indexOf(QLatin1Char('"'));
            if (q >= 0) d.smdPath = t.mid(q + 1, t.lastIndexOf(QLatin1Char('"')) - q - 1).toLower();
            d.valid = true;
        } else if (t.startsWith(QStringLiteral("Materials"))) {
            const int n = t.mid(9).trimmed().toInt();
            for (int m = 0; m < n && i + 1 < lines.size(); ++m) {
                const QString ml = lines[++i].trimmed();
                const int q0 = ml.indexOf(QLatin1Char('"')), q1 = ml.lastIndexOf(QLatin1Char('"'));
                if (q0 < 0 || q1 <= q0) { --m; continue; }
                const QString path = ml.mid(q0 + 1, q1 - q0 - 1).toLower();
                const int count = ml.mid(q1 + 1).trimmed().toInt();
                d.materials.append({path, count > 0 ? count : 1});
            }
        } else if (t.startsWith(QStringLiteral("BoundingBox"))) {
            const QStringList v = t.mid(11).trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            for (int k = 0; k < 6 && k < v.size(); ++k) d.bbox[k] = v[k].toFloat();
        }
    }
    return d;
}

AssetText::Material AssetText::parseMat(const QByteArray& data)
{
    Material m;
    const QString text = decodeText(data);
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) return m;
    const QJsonObject o = doc.object();
    m.version = o.value(QStringLiteral("version")).toInt();
    m.valid = true;
    // Collect texture roles and scalar/array value params (keyed by role name → first value).
    QHash<QString, QJsonValue> valueParams;
    for (const QJsonValue& giv : o.value(QStringLiteral("graphinstances")).toArray()) {
        const QJsonObject gi = giv.toObject();
        const QString parent = gi.value(QStringLiteral("parent")).toString();
        if (!parent.isEmpty()) m.graphs.append(parent);
        for (const QJsonValue& cpv : gi.value(QStringLiteral("custom_parameters")).toArray()) {
            const QJsonObject cp = cpv.toObject();
            const QString role = cp.value(QStringLiteral("name")).toString();
            for (const QJsonValue& pv : cp.value(QStringLiteral("parameters")).toArray()) {
                const QJsonObject pr = pv.toObject();
                if (pr.contains(QStringLiteral("path"))) {
                    MaterialTexture mt;
                    mt.role = role;
                    mt.path = pr.value(QStringLiteral("path")).toString().toLower();
                    mt.srgb = pr.value(QStringLiteral("srgb")).toBool();
                    m.textures.append(mt);
                } else if (pr.contains(QStringLiteral("value")) && !valueParams.contains(role)) {
                    valueParams.insert(role, pr.value(QStringLiteral("value")));
                }
            }
        }
    }
    // Also the top-level "textures" array (filename + format), for the Textures/Explain views.
    for (const QJsonValue& tv : o.value(QStringLiteral("textures")).toArray()) {
        const QJsonObject to = tv.toObject();
        const QString fn = to.value(QStringLiteral("filename")).toString().toLower();
        if (!fn.isEmpty() && !std::any_of(m.textures.begin(), m.textures.end(), [&](const MaterialTexture& x){ return x.path == fn; })) {
            MaterialTexture mt; mt.role = QStringLiteral("(declared)"); mt.path = fn; m.textures.append(mt);
        }
    }

    // ── Derive shader family / workflow from the first Materials/*.fxgraph ──
    for (const QString& g : m.graphs) {
        if (!g.contains(QStringLiteral("/Materials/"), Qt::CaseInsensitive)) continue;
        m.family = g.section(QLatin1Char('/'), -1).section(QLatin1Char('.'), 0, 0);  // stem
        const QString gl = g.toLower();
        if      (gl.contains(QStringLiteral("dielectricspecgloss")))            m.workflow = Workflow::DielectricSpecGloss;
        else if (gl.contains(QStringLiteral("specgloss")) || gl.contains(QStringLiteral("specmask"))) m.workflow = Workflow::SpecGlossSpecMask;
        else if (gl.contains(QStringLiteral("metalrough")))                     m.workflow = Workflow::MetalRough;
        break;
    }
    // BasicColourNormalSpec lives under /Graphs/General/ (not /Materials/), so the family loop above
    // skips it. Detect it separately: its packed normal+spec texture must be unpacked differently.
    for (const QString& g : m.graphs) {
        if (g.contains(QStringLiteral("BasicColourNormalSpec"), Qt::CaseInsensitive)) {
            m.normalSpecPacked = true;
            if (m.family.isEmpty()) m.family = QStringLiteral("BasicColourNormalSpec");
            break;
        }
    }
    // ── Effect graphs ──
    for (const QString& g : m.graphs) {
        const QString gl = g.toLower();
        if (gl.contains(QStringLiteral("alphatest"))) m.alphaTest = true;
        if (gl.contains(QStringLiteral("/sss")) || gl.contains(QStringLiteral("sss3"))) m.hasSSS = true;
        if (gl.contains(QStringLiteral("translucency")))  m.hasTranslucency = true;
        if (gl.contains(QStringLiteral("fur")))           m.hasFur = true;
    }
    // ── Composite / alpha mode (authored Force* graph; measured on real materials: ForceAdditive,
    // ForceAlphaBlend, ForceNoZWriteAlphaBlend, ForceAlphaTest*). Additive > blend > mask; a material
    // carries at most one Force* composite graph in practice, the ordering is defined for safety. ──
    m.alphaMode = AlphaMode::Opaque;
    for (const QString& g : m.graphs) {
        const QString gl = g.toLower();
        if (gl.contains(QStringLiteral("additive")))   { m.alphaMode = AlphaMode::Additive; break; }
        if (gl.contains(QStringLiteral("alphablend"))) { m.alphaMode = AlphaMode::Blend;    break; }
    }
    if (m.alphaMode == AlphaMode::Opaque && m.alphaTest) m.alphaMode = AlphaMode::Mask;
    // ── Resolve role paths ──
    m.albedo        = m.findTexture({QStringLiteral("Albedo"), QStringLiteral("base_color"), QStringLiteral("colour"), QStringLiteral("color")});
    m.normalGlossAO = m.findTexture({QStringLiteral("NormalGlossAO"), QStringLiteral("Normal")});
    m.metalMask     = m.findTexture({QStringLiteral("SpecularMask"), QStringLiteral("Metal")});
    // SpecularColour_TEX — an explicit RGB specular-colour map (spec-gloss workflow). Matched by the
    // specific "SpecularColour"/"SpecColour" role, so it is never confused with SpecularMask (metal)
    // or with the albedo's "colour" key (albedo is resolved first, above).
    m.specColorTex  = m.findTexture({QStringLiteral("SpecularColour"), QStringLiteral("SpecularColor"),
                                     QStringLiteral("SpecColour"), QStringLiteral("SpecColor")});
    m.emissiveTex   = m.findTexture({QStringLiteral("glow"), QStringLiteral("emissive"), QStringLiteral("Emit")});
    m.sssTex        = m.findTexture({QStringLiteral("SSS_TEX"), QStringLiteral("SSS")});
    m.translucencyTex = m.findTexture({QStringLiteral("Translucency")});
    if (m.hasFur) {   // FurV2 shell-fur inputs: the strand noise + the fur-length mask (DepthMap_TEX).
        m.furNoiseTex = m.findTexture({QStringLiteral("Noise")});
        m.furMaskTex  = m.findTexture({QStringLiteral("DepthMap")});   // only DepthMap_TEX carries a path
    }
    m.specMaskInAlbedoAlpha = m.albedo.isEmpty() ? false : [&]{
        for (const MaterialTexture& t : m.textures) if (t.path == m.albedo) return t.role.contains(QStringLiteral("SpecMask"), Qt::CaseInsensitive);
        return false; }();

    // ── Scalar params ──
    for (auto it = valueParams.constBegin(); it != valueParams.constEnd(); ++it) {
        const QString role = it.key();
        if (role.contains(QStringLiteral("OcclusionPower"), Qt::CaseInsensitive)) {
            if (it.value().isDouble()) m.occlusionPower = float(it.value().toDouble());
            else if (it.value().isArray() && !it.value().toArray().isEmpty()) m.occlusionPower = float(it.value().toArray().at(0).toDouble());
        }
        if (role.contains(QStringLiteral("specular_exponent"), Qt::CaseInsensitive)) {   // Blinn-Phong gloss
            if (it.value().isDouble()) m.specularExponent = float(it.value().toDouble());
            else if (it.value().isArray() && !it.value().toArray().isEmpty()) m.specularExponent = float(it.value().toArray().at(0).toDouble());
        }
        if (role.contains(QStringLiteral("Roughness"), Qt::CaseInsensitive)) {   // Use(s)Roughness flag
            const QJsonValue v = it.value();
            m.usesRoughness = v.isBool() ? v.toBool() : (v.isDouble() ? v.toDouble() != 0.0 : true);
        }
        // FurV2 "01. Depth" — fur length. Excludes DepthMap_MULT / VertexDepthMap_MULT (those carry "Map").
        if (m.hasFur && role.contains(QStringLiteral("Depth"), Qt::CaseInsensitive) && !role.contains(QStringLiteral("Map"), Qt::CaseInsensitive)) {
            if (it.value().isDouble()) m.furDepth = float(it.value().toDouble());
            else if (it.value().isArray() && !it.value().toArray().isEmpty()) m.furDepth = float(it.value().toArray().at(0).toDouble());
        }
    }
    // SSS depth tints (03/04/05_SSS_R/G/B_Tint → [0]=R depth, [1]=G, [2]=B).
    auto readTint = [&](const char* key, int idx) {
        for (auto it = valueParams.constBegin(); it != valueParams.constEnd(); ++it)
            if (it.key().contains(QLatin1String(key), Qt::CaseInsensitive) && it.value().isArray()) {
                const QJsonArray a = it.value().toArray();
                for (int c = 0; c < 3 && c < a.size(); ++c) m.sssTint[idx][c] = float(a.at(c).toDouble());
                return;
            }
    };
    if (m.hasSSS) { readTint("SSS_R_Tint", 0); readTint("SSS_G_Tint", 1); readTint("SSS_B_Tint", 2); }
    return m;
}

QString AssetText::Material::findTexture(const QStringList& roleKeys) const
{
    for (const MaterialTexture& t : textures)
        for (const QString& k : roleKeys)
            if (t.role.contains(k, Qt::CaseInsensitive)) return t.path;
    return QString();
}

AssetText::AnimatedObject AssetText::parseAo(const QByteArray& data)
{
    AnimatedObject a;
    const QString text = decodeText(data);
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    auto quoted = [](const QString& s) -> QString {
        const int q0 = s.indexOf(QLatin1Char('"')), q1 = s.lastIndexOf(QLatin1Char('"'));
        return (q0 >= 0 && q1 > q0) ? s.mid(q0 + 1, q1 - q0 - 1) : QString();
    };
    for (const QString& raw : lines) {
        const QString t = raw.trimmed();
        if (t.startsWith(QStringLiteral("version"))) a.version = t.mid(7).trimmed().toInt();
        else if (t.startsWith(QStringLiteral("extends"))) { a.extends = quoted(t); a.valid = true; }
        else if (t.startsWith(QStringLiteral("skin"))) a.smPath = quoted(t).toLower();
        else if (t.startsWith(QStringLiteral("skeleton"))) a.skeletonAst = quoted(t).toLower();
        else if (t.startsWith(QStringLiteral("metadata"))) a.animationAmd = quoted(t).toLower();
        else if (t.startsWith(QStringLiteral("attached_object"))) {
            const QString v = quoted(t);
            const int sp = v.indexOf(QLatin1Char(' '));
            if (sp > 0) a.attachments.append({v.left(sp), v.mid(sp + 1).toLower()});
        }
    }
    return a;
}

QString AssetText::selfTest()
{
    const QByteArray sm = QStringLiteral("version 6\r\nSkinnedMeshData \"Art/X/Foo.smd\"\r\nMaterials 1\r\n\t\"Art/X/Foo.mat\" 3\r\nBoundingBox -1 -2 -3 4 5 6\r\n").toUtf8();
    const SkinnedMeshDesc d = parseSm(sm);
    if (!d.valid || d.smdPath != QStringLiteral("art/x/foo.smd") || d.materials.size() != 1 || d.materials[0].count != 3)
        return QStringLiteral("AssetText: .sm parse wrong (path=%1 mats=%2)").arg(d.smdPath).arg(d.materials.size());
    const QByteArray mat = QStringLiteral("{\"version\":4,\"graphinstances\":[{\"parent\":\"Metadata/Materials/MetalRoughBN.fxgraph\",\"custom_parameters\":[{\"name\":\"AlbedoTransparency_TEX\",\"parameters\":[{\"path\":\"Art/X/c.dds\",\"srgb\":true}]}]}]}").toUtf8();
    const Material m = parseMat(mat);
    if (!m.valid || m.baseColor() != QStringLiteral("art/x/c.dds"))
        return QStringLiteral("AssetText: .mat baseColor wrong (%1)").arg(m.baseColor());
    return QString();
}
