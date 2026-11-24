#!/usr/bin/env python3
"""Check the wiki in wiki/ before it is published.

    python tools/check_wiki.py            check; exit code 1 when anything fails
    python tools/check_wiki.py --sync     first copy again every picture of wiki/img that has
                                          the same name as one in docs/img and differs from it

Run it from the repository's root. Standard library only. One line per problem:

    FAIL  wiki/Page.md:12: what is wrong
    WARN  wiki/Page.md: what is worth a look (does not fail the check)

What it checks is listed in wiki/Editing-This-Wiki.md, "The checker".
"""
import filecmp
import os
import re
import shutil
import subprocess
import sys
import urllib.parse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WIKI = os.path.join(ROOT, "wiki")
IMG = os.path.join(WIKI, "img")
DOCS_IMG = os.path.join(ROOT, "docs", "img")

# Pages that are not content pages: they need no "## Next" and "## Reference".
HUB = {"Home", "Start-Here", "Glossary", "FAQ", "Videos", "Editing-This-Wiki", "_Sidebar", "_Footer"}
# Files of the folder that are not wiki pages at all.
NOT_A_PAGE = {"README"}
# Pages that need not be listed in the sidebar.
NOT_IN_SIDEBAR = {"_Sidebar", "_Footer"}

LONG_PAGE = 400

DIAGRAM_TYPES = (
    "flowchart", "graph", "sequenceDiagram", "classDiagram", "stateDiagram-v2", "stateDiagram",
    "erDiagram", "gantt", "timeline", "pie", "journey", "mindmap", "gitGraph", "quadrantChart",
)

LINK = re.compile(r'(!?)\[((?:[^\[\]]|\[[^\[\]]*\])*)\]\(\s*<?([^)\s>]*)>?(?:\s+"[^"]*")?\s*\)')
FENCE = re.compile(r"^\s*(```+|~~~+)(.*)$")
HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
MACHINE_PATH = re.compile(r"(?<![A-Za-z])[A-Za-z]:[/\\](?![/\\])|/Users/|/home/")
PLACEHOLDER = re.compile(r"\b(TODO|TBD|FIXME|XXX|lorem ipsum)\b|\bcoming soon\b", re.I)
REF_DEFINITION = re.compile(r"^\s{0,3}\[[^\]^]+\]:\s+\S")
HTML_LINK = re.compile(r"<(a|img)\b[^>]*\b(href|src)\s*=", re.I)
EMAIL = re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,}")

problems = []
warnings = []


def rel(path):
    return os.path.relpath(path, ROOT).replace("\\", "/")


def fail(path, line, text):
    where = rel(path) + (":%d" % line if line else "")
    problems.append("FAIL  %s: %s" % (where, text))


def warn(path, text):
    warnings.append("WARN  %s: %s" % (rel(path), text))


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def anchor_of(heading):
    """The anchor GitHub gives a heading."""
    text = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", heading)       # a link: its text
    text = re.sub(r"<[^>]+>", "", text)                             # HTML tags
    text = text.replace("`", "").replace("*", "")
    text = text.strip().lower()
    text = re.sub(r"[^\w\- ]", "", text, flags=re.UNICODE)
    return text.replace(" ", "-")


class Doc:
    """A Markdown file: its lines outside code fences, its headings' anchors, its mermaid blocks."""

    def __init__(self, path):
        self.path = path
        self.lines = read(path).split("\n")
        self.prose = []          # (line number, text) of the lines outside code fences
        self.mermaid = []        # (line number of the fence, [lines])
        self.headings = []       # (line number, level, text)
        self.anchors = set()
        fence = None
        block = None
        for number, line in enumerate(self.lines, 1):
            m = FENCE.match(line)
            if fence is None and m:
                fence = m.group(1)[0] * 3
                if m.group(2).strip().lower() == "mermaid":
                    block = (number, [])
                continue
            if fence is not None:
                if m and m.group(1).startswith(fence) and not m.group(2).strip():
                    fence = None
                    if block:
                        self.mermaid.append(block)
                        block = None
                elif block:
                    block[1].append(line)
                continue
            self.prose.append((number, line))
            h = HEADING.match(line)
            if h:
                self.headings.append((number, len(h.group(1)), h.group(2)))
        if fence is not None:
            fail(path, len(self.lines), "a code fence is opened and never closed")
        seen = {}
        for _, _, text in self.headings:
            a = anchor_of(text)
            n = seen.get(a, 0)
            seen[a] = n + 1
            self.anchors.add(a if n == 0 else "%s-%d" % (a, n))

    def links(self):
        for number, text in self.prose:
            for m in LINK.finditer(text):
                if text[:m.start()].count("`") % 2:                 # inside inline code: an example, not a link
                    continue
                yield number, bool(m.group(1)), m.group(3)


_docs = {}


def doc(path):
    path = os.path.normpath(path)
    if path not in _docs:
        _docs[path] = Doc(path)
    return _docs[path]


def has_anchor(path, anchor):
    """Anchors are compared as written: GitHub's are lower case, and a browser does not fold case."""
    anchor = urllib.parse.unquote(anchor)
    if not path.lower().endswith(".md"):
        m = re.fullmatch(r"L(\d+)(?:-L(\d+))?", anchor)            # a line of a source file
        if not m:
            return False
        try:
            count = len(read(path).split("\n"))
        except (UnicodeDecodeError, OSError):
            return False
        first, last = int(m.group(1)), int(m.group(2) or m.group(1))
        return 1 <= first <= last <= count
    return anchor in doc(path).anchors


def exact_case(full):
    """True when every part of the path below the repository's root is spelled as it is on the disk.
    Windows opens docs/manual.md for docs/MANUAL.md; GitHub does not."""
    here = ROOT
    for part in os.path.relpath(full, ROOT).split(os.sep):
        if part not in os.listdir(here):
            return False
        here = os.path.join(here, part)
    return True


outside = {}        # repository path -> the first (file, line, target) that links to it


def check_ignored():
    """A link to a file git ignores works here and leads nowhere on GitHub."""
    if not outside:
        return
    try:
        run = subprocess.run(["git", "-C", ROOT, "check-ignore", "--stdin", "-z"],
                             input="\0".join(sorted(outside)).encode("utf-8"), capture_output=True)
    except OSError:
        warnings.append("WARN  git did not run: links to files that git ignores were not looked for")
        return
    if run.returncode not in (0, 1):
        warnings.append("WARN  git check-ignore failed: links to files that git ignores were not looked for")
        return
    for line in run.stdout.decode("utf-8", "replace").split("\0"):
        if line in outside:
            path, number, target = outside[line]
            fail(path, number, "link '%s': git ignores %s, so it is not on GitHub" % (target, line))


def check_links(d, name, counts, used_images):
    """Every link and picture of one file."""
    page_links = set()
    for number, text in d.prose:
        code = re.sub(r"`[^`]*`", "", text)
        if REF_DEFINITION.match(code):
            fail(d.path, number, "a reference-style link: this wiki writes [text](target), which is what is checked")
        if HTML_LINK.search(code):
            fail(d.path, number, "an HTML link or picture: this wiki writes [text](target) and ![text](img/name.png)")
        if name not in NOT_A_PAGE and name != "Editing-This-Wiki" and PLACEHOLDER.search(code):
            fail(d.path, number, "placeholder text ('%s')" % PLACEHOLDER.search(code).group(0))
    for number, is_image, target in d.links():
        if not target:
            fail(d.path, number, "a link with no target")
            continue
        if re.match(r"[a-z][a-z0-9+.-]*:", target, re.I):
            if target.lower().startswith(("http://", "https://")):
                counts["external"] += 1
            else:
                fail(d.path, number, "link '%s': only http and https links leave the repository" % target)
            continue
        path, _, anchor = target.partition("#")
        path = urllib.parse.unquote(path)
        if is_image or path.startswith("img/"):
            counts["images"] += 1
            if not path.startswith("img/") or "/" in path[4:]:
                fail(d.path, number, "picture '%s': pictures are img/<name>" % target)
            elif not os.path.isfile(os.path.join(IMG, path[4:])) or path[4:] not in os.listdir(IMG):
                fail(d.path, number, "picture '%s' is not in wiki/img" % target)
            else:
                used_images.add(path[4:])
            continue
        counts["links"] += 1
        if path == "":
            if anchor not in d.anchors:
                fail(d.path, number, "link '#%s': no such heading on this page" % anchor)
            continue
        if path.startswith("../") or name in NOT_A_PAGE:
            full = os.path.normpath(os.path.join(WIKI, path))
            if not full.startswith(ROOT + os.sep):
                fail(d.path, number, "link '%s' leaves the repository" % target)
            elif not os.path.exists(full):
                fail(d.path, number, "link '%s': no such file" % target)
            elif not exact_case(full):
                fail(d.path, number, "link '%s': the file's name is spelled with other capitals" % target)
            elif name not in NOT_A_PAGE and os.path.dirname(full) == WIKI and full.endswith(".md") \
                    and os.path.basename(full)[:-3] not in NOT_A_PAGE:
                fail(d.path, number, "link '%s': a wiki page is linked by its name alone, not through ../" % target)
            elif os.path.dirname(full) == IMG:
                fail(d.path, number, "link '%s': a picture of the wiki is img/<name>, not through ../" % target)
            elif anchor and os.path.isfile(full) and not has_anchor(full, anchor):
                fail(d.path, number, "link '%s': no heading gives the anchor '#%s' in %s"
                     % (target, anchor, rel(full)))
            if os.path.exists(full):
                outside.setdefault(rel(full), (d.path, number, target))
            continue
        if "/" in path or path.endswith(".md"):
            fail(d.path, number, "link '%s': a wiki page is linked by its name alone, "
                                 "without a path and without .md" % target)
            continue
        full = os.path.join(WIKI, path + ".md")
        if path in NOT_A_PAGE or not os.path.isfile(full) or path + ".md" not in os.listdir(WIKI):
            fail(d.path, number, "link '%s': there is no page wiki/%s.md" % (target, path))
            continue
        page_links.add(path)
        if anchor and not has_anchor(full, anchor):
            fail(d.path, number, "link '%s': no heading gives the anchor '#%s' on that page" % (target, anchor))
    return page_links


def strip_quotes(line):
    return re.sub(r'"[^"]*"', lambda q: "Q" * len(q.group(0)), line)


def check_mermaid(d):
    """Cheap syntax checks. They do not render the diagram."""
    for start, lines in d.mermaid:
        body = [(start + 1 + i, t) for i, t in enumerate(lines)
                if t.strip() and not t.strip().startswith("%%")]
        if not body:
            fail(d.path, start, "an empty mermaid block")
            continue
        first = body[0][1].strip()
        kind = first.split()[0].rstrip(";")
        if kind not in DIAGRAM_TYPES:
            fail(d.path, body[0][0], "mermaid: '%s' is not a diagram type this wiki uses" % kind)
            continue
        flow = kind in ("flowchart", "graph")
        opener = {"flowchart": r"subgraph\b", "graph": r"subgraph\b",
                  "sequenceDiagram": r"(loop|alt|opt|par|critical|break|rect|box)\b"}.get(kind)
        if opener:
            depth = 0
            for number, text in body[1:]:
                word = strip_quotes(text).strip()
                if re.match(opener, word):
                    depth += 1
                elif re.fullmatch(r"end;?", word):
                    depth -= 1
                    if depth < 0:
                        fail(d.path, number, "mermaid: an 'end' that closes nothing")
                        break
            if depth > 0:
                fail(d.path, start, "mermaid: %d block(s) (subgraph, loop, alt ...) without their 'end'" % depth)
        stack = []
        for number, text in body[1:]:
            if text.count('"') % 2:
                fail(d.path, number, "mermaid: an odd number of double quotes")
                continue
            bare = strip_quotes(text)
            tags = re.sub(r"<<[^<>]*>>", "", text)
            if re.search(r"</?[A-Za-z][^<>]*>", re.sub(r"<br\s*/?>", "", tags)):
                fail(d.path, number, "mermaid: HTML other than <br/>")
            if flow and re.search(r'"\s*(\d+[.)]|[-*+#])\s', text):
                fail(d.path, number, "mermaid: a label that begins like a Markdown list or heading "
                                     "('1. ', '- ', '# '): GitHub draws 'Unsupported markdown'")
            if flow:
                for m in re.finditer(r"\[([^\[\]]*)\]", bare):
                    inner = m.group(1)
                    if ("(" in inner or ")" in inner) and not re.fullmatch(r"\([^()]*\)", inner):
                        fail(d.path, number, "mermaid: parentheses in a [] label that is not quoted")
                for m in re.finditer(r"\|([^|]*)\|", bare):
                    if "(" in m.group(1) or ")" in m.group(1):
                        fail(d.path, number, "mermaid: parentheses in an edge label that is not quoted")
                if re.search(r"(-->|---|-\.->|==>)\s*(\|[^|]*\|\s*)?(end|class|style)\s*$", bare) or \
                   re.match(r"\s*(end|class|style)\s*(\[|\(|\{|-->|---|-\.->|==>)", bare):
                    fail(d.path, number, "mermaid: 'end', 'class' or 'style' used as the name of a node")
            if kind == "sequenceDiagram":
                m = re.match(r"\s*(participant|actor)\s+(\S+)", bare)
                if m and not re.fullmatch(r"[A-Za-z]+", m.group(2)):
                    fail(d.path, number, "mermaid: the participant id '%s' is not letters only" % m.group(2))
                # after the colon of a message or a note the text is free
                bare = re.sub(r":.*$", "", bare)
            elif kind.startswith("stateDiagram") or kind == "classDiagram":
                if not re.search(r"\{\s*$", bare):
                    bare = re.sub(r":.*$", "", bare)
            for ch in bare:
                if ch in "([{":
                    stack.append((ch, number))
                elif ch in ")]}":
                    if not stack or "([{".index(stack[-1][0]) != ")]}".index(ch):
                        fail(d.path, number, "mermaid: a '%s' that closes nothing" % ch)
                        stack = None
                        break
                    stack.pop()
            if stack is None:
                break
        if stack:
            fail(d.path, stack[-1][1], "mermaid: a '%s' that is never closed" % stack[-1][0])


def sync_pictures():
    done = 0
    for name in sorted(os.listdir(IMG)):
        ours, theirs = os.path.join(IMG, name), os.path.join(DOCS_IMG, name)
        if os.path.isfile(theirs) and not filecmp.cmp(ours, theirs, shallow=False):
            shutil.copyfile(theirs, ours)
            print("synced  wiki/img/%s from docs/img" % name)
            done += 1
    print("--sync: %d picture(s) copied again" % done)


def main(argv):
    if not os.path.isdir(WIKI):
        print("FAIL  there is no wiki/ folder; run this from the repository's root")
        return 1
    if "--sync" in argv and os.path.isdir(IMG):
        sync_pictures()

    names = sorted(f[:-3] for f in os.listdir(WIKI) if f.endswith(".md"))
    pages = [n for n in names if n not in NOT_A_PAGE]
    counts = {"links": 0, "images": 0, "external": 0}
    used_images = set()
    diagrams = 0
    sidebar_links = set()

    for name in names:
        path = os.path.join(WIKI, name + ".md")
        d = doc(path)
        linked = check_links(d, name, counts, used_images)
        if name == "_Sidebar":
            sidebar_links = linked
        check_mermaid(d)
        diagrams += len(d.mermaid)
        if name in NOT_A_PAGE:
            continue
        if not re.fullmatch(r"_?[A-Za-z0-9]+(-[A-Za-z0-9]+)*", name):
            fail(path, 0, "a page's file name is words joined by hyphens")
        if name not in HUB:
            if not d.lines or not d.lines[0].startswith("# "):
                fail(path, 1, "a page starts with its title: '# Page Name'")
            twos = [text for _, level, text in d.headings if level == 2]
            for wanted in ("Next", "Reference"):
                if wanted not in twos:
                    fail(path, 0, "the page has no '## %s'" % wanted)
            if twos[-2:] != ["Next", "Reference"] and "Next" in twos and "Reference" in twos:
                fail(path, 0, "'## Next' and '## Reference' are the last two sections, in that order")
        if len(d.lines) > LONG_PAGE:
            warn(path, "%d lines; over %d a page is better split" % (len(d.lines), LONG_PAGE))

    # the sidebar
    if "_Sidebar" not in names:
        fail(os.path.join(WIKI, "_Sidebar.md"), 0, "there is no sidebar")
    else:
        for name in pages:
            if name not in NOT_IN_SIDEBAR and name not in sidebar_links:
                fail(os.path.join(WIKI, name + ".md"), 0, "the page is not in wiki/_Sidebar.md")

    check_ignored()

    # the pictures
    if os.path.isdir(IMG):
        for name in sorted(os.listdir(IMG)):
            full = os.path.join(IMG, name)
            if name not in used_images:
                fail(full, 0, "no page shows this picture")
            twin = os.path.join(DOCS_IMG, name)
            if os.path.isfile(twin) and not filecmp.cmp(full, twin, shallow=False):
                fail(full, 0, "a stale copy: it differs from docs/img/%s "
                              "(python tools/check_wiki.py --sync copies it again)" % name)

    # nothing of this machine, nobody's address
    for folder, _, files in os.walk(WIKI):
        for f in sorted(files):
            if not f.lower().endswith((".md", ".py", ".mjs", ".js", ".json", ".txt", ".sh", ".yml", ".yaml")):
                continue
            full = os.path.join(folder, f)
            try:
                text = read(full)
            except UnicodeDecodeError:
                fail(full, 0, "the file is not UTF-8")
                continue
            for number, line in enumerate(text.split("\n"), 1):
                if MACHINE_PATH.search(line):
                    fail(full, number, "an absolute path of a machine")
                if EMAIL.search(line):
                    fail(full, number, "an e-mail address")

    for line in warnings:
        print(line)
    for line in problems:
        print(line)
    print("wiki: %d pages, %d links (%d more leave for the web), %d pictures shown (%d files in wiki/img), "
          "%d diagrams" % (len(pages), counts["links"], counts["external"], counts["images"],
                           len(os.listdir(IMG)) if os.path.isdir(IMG) else 0, diagrams))
    if problems:
        print("check_wiki FAILED: %d problem(s), %d warning(s)" % (len(problems), len(warnings)))
        return 1
    print("check_wiki PASSED, %d warning(s)" % len(warnings))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
