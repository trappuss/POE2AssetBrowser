#!/usr/bin/env python3
"""Documentation link checker (template §8): walk every [text](target) in README.md, docs/ and
wiki/, and resolve both the page and the #anchor against the real headings. GitHub's anchor rule
lowercases, replaces each space with a hyphen, drops punctuation, and does NOT collapse runs of
hyphens — so `## Help ▸ Find` is `help--find` with two hyphens; this encodes that once.

Exit 1 if any link is broken. Run before publishing.
"""
import os, re, sys, glob

ROOT = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(ROOT)
WIKI = os.path.join(ROOT, "wiki")
DOCS = os.path.join(ROOT, "docs")

def anchors_of(path):
    out = set()
    for line in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r'#{1,6}\s+(.*)', line)
        if not m: continue
        h = m.group(1).strip()
        a = h.lower()
        a = re.sub(r'[^\w\s-]', '', a)     # drop punctuation (keep word chars, spaces, hyphens)
        a = a.replace(' ', '-')
        out.add(a)
    return out

link_re = re.compile(r'\[[^\]]+\]\(([^)]+)\)')
errors = []
files = [os.path.join(ROOT, "README.md")] + glob.glob(os.path.join(WIKI, "*.md")) + glob.glob(os.path.join(DOCS, "*.md"))

for f in files:
    base = os.path.dirname(f)
    text = open(f, encoding="utf-8", errors="replace").read()
    for m in link_re.finditer(text):
        target = m.group(1).strip()
        if target.startswith(("http://", "https://", "mailto:")):
            continue
        page, _, anchor = target.partition('#')
        # Resolve the page.
        if page == "":
            resolved = f          # same-page anchor
        elif page.endswith(".md"):
            resolved = os.path.normpath(os.path.join(base, page))
        else:
            # A wiki-style bare page name (e.g. "Install" or "wiki/Install").
            name = os.path.basename(page)
            cand = os.path.join(WIKI, name + ".md")
            resolved = cand if os.path.exists(cand) else os.path.normpath(os.path.join(base, page))
        if page and not os.path.exists(resolved):
            # a link into wiki/ from README written as (wiki/Install.md) resolves; a bare (Install) too
            errors.append(f"{os.path.relpath(f, ROOT)}: broken page link -> {target}")
            continue
        if anchor:
            if not os.path.exists(resolved):
                errors.append(f"{os.path.relpath(f, ROOT)}: anchor target missing -> {target}")
                continue
            if anchor not in anchors_of(resolved):
                errors.append(f"{os.path.relpath(f, ROOT)}: no heading '#{anchor}' in {os.path.relpath(resolved, ROOT)}")

print(f"check-links: {len(files)} docs scanned")
for e in errors: print("  BROKEN:", e)
if errors:
    print(f"\ncheck-links: {len(errors)} broken link(s).")
    sys.exit(1)
print("check-links: OK")
