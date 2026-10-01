#pragma once
#include <QString>
#include <QStringList>
#include <QLatin1Char>

// ── The ONE definition of what a search term matches ────────────────────────────────────────────
// (Ported verbatim from D4AssetBrowser per template §4 — the one-matcher rule. Every place that
// filters the asset list, the Textures tab and bulk extraction parses through here, so a syntax
// addition lands everywhere at once instead of three hand-rolled parsers drifting apart.)
//
// Syntax handled HERE: '|' inside a term is OR — "helmet|gloves" matches a name containing either.
// Combined with the outer space-AND at the call site: "armour boots|gloves" = armour AND (boots OR
// gloves). A '-' term with alternatives excludes when ANY alternative matches (-a|b == NOT(a OR b)).
namespace QueryTerm {

inline bool matches(const QString& hay, const QString& term)
{
    if (!term.contains(QLatin1Char('|')))
        return hay.contains(term, Qt::CaseInsensitive);
    const QStringList alts = term.split(QLatin1Char('|'), Qt::SkipEmptyParts);
    if (alts.isEmpty()) return true;   // a term that is ONLY pipes constrains nothing
    for (const QString& a : alts)
        if (hay.contains(a, Qt::CaseInsensitive)) return true;
    return false;
}

// Parse a full query line into (positive, negative, metaOnly) terms and test a candidate. This is
// the shared entry the list model and bulk filter both call so their parse can never diverge.
struct Query {
    QStringList andTerms;     // each must match (may contain | alternatives)
    QStringList notTerms;     // none may match
    QStringList metaTerms;    // '#' prefixed: match against metadata (tags/id), not the file name
    QStringList idTerms;      // pure-digit terms, matched against the id
    bool isEmpty() const { return andTerms.isEmpty() && notTerms.isEmpty() && metaTerms.isEmpty() && idTerms.isEmpty(); }
};

// A "0x…" token whose remainder is all hex digits is an ID term in hex (the MurmurHash64 path id
// the game and GGPK/bundle tools print in hex). Requiring the 0x prefix keeps hex-looking words
// (face, dead, cafe…) as ordinary name searches. The stored token is the lowercase hex body, matched
// against the idText's hex form by the caller.
inline bool isHexId(const QString& t, QString* body)
{
    if (t.size() < 3 || !(t.startsWith(QStringLiteral("0x")) || t.startsWith(QStringLiteral("0X")))) return false;
    for (int i = 2; i < t.size(); ++i) {
        const QChar c = t[i];
        const bool hex = (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
                      || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))
                      || (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
        if (!hex) return false;
    }
    if (body) *body = t.mid(2).toLower();
    return true;
}

inline Query parse(const QString& line)
{
    Query q;
    for (const QString& raw : line.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        QString t = raw;
        if (t.startsWith(QLatin1Char('-')) && t.size() > 1) { q.notTerms.append(t.mid(1)); continue; }
        if (t.startsWith(QLatin1Char('#')) && t.size() > 1) { q.metaTerms.append(t.mid(1)); continue; }
        QString hex;
        if (isHexId(t, &hex)) { q.idTerms.append(hex); continue; }   // find by id, hex form
        bool digits = !t.isEmpty();
        for (QChar c : t) if (!c.isDigit()) { digits = false; break; }
        if (digits) { q.idTerms.append(t); continue; }               // find by id, decimal form
        q.andTerms.append(t);
    }
    return q;
}

// `name` = the file path/name; `meta` = a metadata haystack (id text, tags…). Returns whether the
// candidate passes the whole query.
inline bool test(const Query& q, const QString& name, const QString& meta, const QString& idText)
{
    for (const QString& t : q.andTerms) if (!matches(name, t) && !matches(meta, t)) return false;
    for (const QString& t : q.notTerms) if (matches(name, t) || matches(meta, t)) return false;
    for (const QString& t : q.metaTerms) if (!matches(meta, t)) return false;
    for (const QString& t : q.idTerms) if (!idText.contains(t)) return false;
    return true;
}

// ── Self-test — "" on success, or the first failing case. Called once at startup (main.cpp). ─────
inline QString selfTest()
{
    struct Case { const char* hay; const char* term; bool want; };
    static const Case kCases[] = {
        {"helmet_str03",        "helmet",              true },
        {"helmet_str03",        "gloves",              false},
        {"helmet_str03",        "HELMET",              true },   // case-insensitive
        {"helmet_str03",        "gloves|helmet",       true },
        {"helmet_str03",        "gloves|boots",        false},
        {"tatteredrobe_mc",     "robe|test999",        true },
        {"body_base00",         "body_base00|body_base01", true },
        {"body_base01_trs",     "body_base00|arm_base00",  false},
        {"anything",            "|",                   true },
        {"anything",            "zzz|",                false},
    };
    for (const auto& c : kCases) {
        const QString hay = QString::fromLatin1(c.hay), term = QString::fromLatin1(c.term);
        if (matches(hay, term) != c.want)
            return QStringLiteral("QueryTerm: \"%1\" vs \"%2\" expected %3")
                       .arg(hay, term, c.want ? QStringLiteral("match") : QStringLiteral("no match"));
    }
    // Find-by-id: decimal and 0x-hex tokens become id terms; a hex-looking WORD stays a name term.
    // idText carries both the decimal and the zero-padded 16-hex form of the path hash.
    {
        const quint64 id = 0x0a1b2c3d4e5f6070ULL;
        const QString idText = QStringLiteral("%1 %2").arg(id).arg(id, 16, 16, QLatin1Char('0'));
        const QString path = QStringLiteral("art/models/items/armours/foo.smd");
        if (!test(parse(QStringLiteral("0x0a1b2c3d4e5f6070")), path, path, idText)) return QStringLiteral("QueryTerm: full hex id should match");
        if (!test(parse(QStringLiteral("0x4e5f6070")), path, path, idText)) return QStringLiteral("QueryTerm: hex id suffix should match");
        if (test(parse(QStringLiteral("0xdeadbeef")), path, path, idText)) return QStringLiteral("QueryTerm: wrong hex id must not match");
        if (!test(parse(QString::number(id)), path, path, idText)) return QStringLiteral("QueryTerm: decimal id should match");
        // "face" is valid hex but has no 0x prefix → a name term, matched against the name only.
        if (!test(parse(QStringLiteral("armours")), path, path, idText)) return QStringLiteral("QueryTerm: name term regression");
        if (test(parse(QStringLiteral("face")), path, path, idText)) return QStringLiteral("QueryTerm: bare hex-word must not match this name");
    }
    return QString();
}

}  // namespace QueryTerm
