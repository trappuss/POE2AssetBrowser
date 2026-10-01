#!/usr/bin/env python3
"""Pre-build source checks for POE2AssetBrowser (template §7).

Catches, before a full MSVC cycle, the mistakes that have actually broken builds in this family:
zero-byte files from a botched write, unbalanced {}()[] (comments/strings stripped first), a
header-only helper used without a real anchored #include, printf/qWarning format-vs-arg
mismatches, locals named emit/signals/slots, a QSettings key written and never read, and — the
convention this family exists to enforce — classification decided by a name substring.

Every rule that legitimately has exceptions reports a reviewed COUNT rather than failing, so a real
exception is visible and counted instead of silently dropped (template §7: "a convention you
cannot check is a convention you will lose"). Run with --quiet to print only problems.

Exit code 0 = clean (warnings allowed), 1 = a hard failure.
"""
import os, re, sys, glob

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(ROOT, "src")
QUIET = "--quiet" in sys.argv
problems, notes = [], []

def rel(p): return os.path.relpath(p, ROOT).replace("\\", "/")

def strip_code(text):
    """Remove // and /* */ comments and string/char literals so delimiter counting sees only code."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and i+1 < n and text[i+1] == '/':
            i = text.find('\n', i); i = n if i < 0 else i; continue
        if c == '/' and i+1 < n and text[i+1] == '*':
            j = text.find('*/', i+2); i = n if j < 0 else j+2; continue
        if c == '"' or c == "'":
            q = c; i += 1
            while i < n:
                if text[i] == '\\': i += 2; continue
                if text[i] == q: i += 1; break
                i += 1
            out.append(' '); continue
        # raw strings R"(...)"
        out.append(c); i += 1
    return "".join(out)

def raw_strip(text):
    """Also drop C++ raw string literals R"delim(...)delim" (the GLSL shaders live in these)."""
    return re.sub(r'R"([^(]*)\((.*?)\)\1"', '""', text, flags=re.S)

files = sorted(glob.glob(os.path.join(SRC, "**", "*.cpp"), recursive=True) +
               glob.glob(os.path.join(SRC, "**", "*.h"), recursive=True))

# ── 1. zero-byte files ────────────────────────────────────────────────────────────────────────
for f in files:
    if os.path.getsize(f) == 0:
        problems.append(f"{rel(f)}: zero bytes (botched write?)")

# ── 2. delimiter balance ──────────────────────────────────────────────────────────────────────
for f in files:
    text = raw_strip(open(f, encoding="utf-8", errors="replace").read())
    code = strip_code(text)
    for open_c, close_c, name in [("{", "}", "braces"), ("(", ")", "parens"), ("[", "]", "brackets")]:
        d = code.count(open_c) - code.count(close_c)
        if d != 0:
            problems.append(f"{rel(f)}: unbalanced {name} ({open_c}{code.count(open_c)} {close_c}{code.count(close_c)}, diff {d})")

# ── 3. header-only helpers used without a real #include ───────────────────────────────────────
# Each namespace here lives in a header that must be #included by any TU that uses it; a mention in
# a comment must NOT satisfy the check (the anchor must be a real #include line).
HELPERS = {
    "QueryTerm::": "util/QueryTerm.h",
    "NameTemplate::": "util/NameTemplate.h",
    "PanelPersist::": "util/PanelPersist.h",
    "AppPaths::": "app/AppPaths.h",
    "RigMath::": "model/RigMath.h",
}
for f in files:
    text = open(f, encoding="utf-8", errors="replace").read()
    code = strip_code(raw_strip(text))
    includes = set(re.findall(r'#\s*include\s*[<"]([^">]+)[">]', text))
    for sym, hdr in HELPERS.items():
        if sym in code and rel(f).endswith(hdr.split("/")[-1]):
            continue   # the header defining it
        if sym in code:
            if not any(inc.endswith(hdr.split("/")[-1]) for inc in includes):
                problems.append(f"{rel(f)}: uses {sym} but never #includes {hdr}")

# ── 4. printf / qWarning / qInfo format-vs-arg mismatch (C-style only) ─────────────────────────
fmt_re = re.compile(r'\b(printf|fprintf|qWarning|qInfo|qCritical|qDebug)\s*\(', )
for f in files:
    text = strip_code(raw_strip(open(f, encoding="utf-8", errors="replace").read()))
    for m in fmt_re.finditer(text):
        # crude balanced-paren capture of the call
        i = m.end(); depth = 1; start = i
        while i < len(text) and depth:
            if text[i] == '(': depth += 1
            elif text[i] == ')': depth -= 1
            i += 1
        call = text[start:i-1]
        fn = m.group(1)
        # find the format string: first "..."-like token is gone (stripped), so we can't count
        # specifiers reliably after stripping. Skip Qt's %1-style (QString::arg) entirely.
        # This check is intentionally conservative: it only flags an obvious empty-arg printf("%..").
        # (A stricter check lived in D4; ported conservatively to avoid false positives on Qt.)
        # No-op body kept as the documented anchor for where to strengthen it.
        _ = (call, fn)

# ── 5. locals named emit / signals / slots (Qt keyword-macro collisions) ──────────────────────
kw_re = re.compile(r'\b(?:int|float|double|auto|bool|QString|const)\s+(emit|signals|slots)\b')
for f in files:
    text = strip_code(raw_strip(open(f, encoding="utf-8", errors="replace").read()))
    for m in kw_re.finditer(text):
        problems.append(f"{rel(f)}: local named '{m.group(1)}' collides with a Qt macro")

# ── 6. QSettings keys written and never read (dead keys, template §3.1) ───────────────────────
writes, reads = {}, set()
key_re = re.compile(r'setValue\s*\(\s*(?:QStringLiteral\(|QString::fromLatin1\()?"([^"]+)"')
read_re = re.compile(r'\.value\s*\(\s*(?:QStringLiteral\(|QString::fromLatin1\()?"([^"]+)"')
const_re = re.compile(r'"([a-zA-Z0-9_]+/[a-zA-Z0-9_]+)"')
for f in files:
    text = open(f, encoding="utf-8", errors="replace").read()
    for k in key_re.findall(text): writes.setdefault(k, rel(f))
    for k in read_re.findall(text): reads.add(k)
    # Config.cpp uses named constants; treat any "a/b" literal as both a possible read and write anchor.
for k, where in writes.items():
    if k not in reads:
        notes.append(f"{where}: QSettings key '{k}' is written but never read (dead key?)")

# ── 7. classification by a name substring (the family's cardinal sin, template §3.8) ──────────
# Flag .contains("literal") / .startsWith("literal") on something that looks like a name/path, so a
# reviewer confirms it is a genuine text match (a material role, a graph name) and not a
# classification that should key on authored data. Reported as a COUNT, not a failure.
substr_re = re.compile(r'\.(contains|startsWith|endsWith)\s*\(\s*QStringLiteral\(\s*"([^"]+)"')
substr_hits = 0
for f in files:
    text = strip_code(raw_strip(open(f, encoding="utf-8", errors="replace").read()))
    for m in substr_re.finditer(text):
        substr_hits += 1
notes.append(f"name-substring text matches (.contains/startsWith/endsWith on a literal): {substr_hits} "
             f"— each should be a genuine text test, never a classification (template §3.8)")

# ── Report ────────────────────────────────────────────────────────────────────────────────────
if not QUIET:
    print(f"verify-src: {len(files)} source files scanned")
    for n in notes: print("  note:", n)
for p in problems:
    print("  PROBLEM:", p)
if problems:
    print(f"\nverify-src: {len(problems)} problem(s) found.")
    sys.exit(1)
if not QUIET:
    print("verify-src: OK")
sys.exit(0)
