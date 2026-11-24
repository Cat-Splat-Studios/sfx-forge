#!/usr/bin/env python3
"""Make the folder that is pushed to the GitHub wiki, from wiki/.

    python tools/publish_wiki.py --repo-url https://github.com/OWNER/REPO [--branch main] [--out out/wiki-publish]

Run it from the repository's root. Standard library only. It

  1. runs tools/check_wiki.py and stops when that fails;
  2. copies wiki/*.md (not wiki/README.md, which is about the folder) and wiki/img into the output folder;
  3. rewrites every link that leaves the wiki ("../docs/MANUAL.md#2-the-device") into an absolute URL of the
     repository: a file to <repo-url>/blob/<branch>/<path>#anchor, a folder to <repo-url>/tree/<branch>/<path>,
     a picture, a video or another binary file to <repo-url>/raw/<branch>/<path>.
     Links between wiki pages and to img/ stay as they are;
  4. prints the git commands that publish the folder.

It runs no git command and nothing that touches the network: pushing is the owner's step.
A .git folder inside the output folder is kept, so the folder may be a clone of the wiki's own repository.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import urllib.parse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WIKI = os.path.join(ROOT, "wiki")

SKIP = {"README.md"}
RAW = (".png", ".jpg", ".jpeg", ".gif", ".svg", ".webp", ".mp4", ".webm", ".mov", ".wav", ".zip", ".wasm",
       ".dll", ".syx", ".bytes", ".pdf")

LINK = re.compile(r'(!?\[(?:[^\[\]]|\[[^\[\]]*\])*\]\(\s*)(\.\./[^)\s]*)((?:\s+"[^"]*")?\s*\))')
FENCE = re.compile(r"^\s*(```+|~~~+)")


def rewrite(text, base, branch, counts):
    """The page's text with its ../ links made absolute. Code fences and inline code are left alone."""

    def absolute(match):
        if match.string[: match.start()].count("`") % 2:           # inside inline code: an example, not a link
            return match.group(0)
        target = match.group(2)
        path, hash_, anchor = target.partition("#")
        path = urllib.parse.unquote(path)
        full = os.path.normpath(os.path.join(WIKI, path))
        inside = os.path.relpath(full, ROOT).replace("\\", "/")
        if inside.startswith(".."):
            raise SystemExit("publish_wiki: the link '%s' leaves the repository" % target)
        if os.path.isdir(full):
            kind = "tree"
        elif inside.lower().endswith(RAW):
            kind = "raw"
        else:
            kind = "blob"
        counts[kind] += 1
        url = "%s/%s/%s/%s" % (base, kind, urllib.parse.quote(branch, safe="/"), urllib.parse.quote(inside, safe="/"))
        return match.group(1) + url + hash_ + anchor + match.group(3)

    out = []
    fence = None
    for line in text.split("\n"):
        m = FENCE.match(line)
        if fence is None and m:
            fence = m.group(1)[0] * 3
        elif fence is not None:
            if m and m.group(1).startswith(fence) and not line.strip().strip("`~"):
                fence = None
        else:
            line = LINK.sub(absolute, line)
        out.append(line)
    return "\n".join(out)


def main():
    parser = argparse.ArgumentParser(description="Make the folder that is pushed to the GitHub wiki.")
    parser.add_argument("--repo-url", required=True,
                        help="the repository's address, like https://github.com/OWNER/REPO")
    parser.add_argument("--branch", default="main", help="the branch the links point into (default: main)")
    parser.add_argument("--out", default=os.path.join("out", "wiki-publish"),
                        help="the folder to write (default: out/wiki-publish)")
    args = parser.parse_args()

    base = args.repo_url.strip().rstrip("/")
    if base.endswith(".git"):
        base = base[:-4]
    if base.endswith(".wiki"):
        base = base[:-5]
    if not re.fullmatch(r"https://[^/\s]+/[^/\s]+/[^/\s]+", base):
        print("publish_wiki: --repo-url must look like https://github.com/OWNER/REPO")
        return 1

    check = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "check_wiki.py")], cwd=ROOT)
    if check.returncode != 0:
        print("publish_wiki: nothing written, because check_wiki.py failed")
        return 1

    out = os.path.abspath(args.out)
    if os.path.commonpath([out, WIKI]) == WIKI or out == ROOT:
        print("publish_wiki: --out must not be the repository's root or inside wiki/")
        return 1
    os.makedirs(out, exist_ok=True)
    for name in os.listdir(out):                     # empty it, but keep a clone's .git
        if name == ".git":
            continue
        full = os.path.join(out, name)
        if os.path.isdir(full) and not os.path.islink(full):
            shutil.rmtree(full)
        else:
            os.remove(full)

    counts = {"blob": 0, "tree": 0, "raw": 0}
    pages = 0
    for name in sorted(os.listdir(WIKI)):
        if not name.endswith(".md") or name in SKIP:
            continue
        with open(os.path.join(WIKI, name), encoding="utf-8") as f:
            text = f.read()
        with open(os.path.join(out, name), "w", encoding="utf-8", newline="\n") as f:
            f.write(rewrite(text, base, args.branch, counts))
        pages += 1
    pictures = 0
    if os.path.isdir(os.path.join(WIKI, "img")):
        shutil.copytree(os.path.join(WIKI, "img"), os.path.join(out, "img"))
        pictures = len(os.listdir(os.path.join(out, "img")))

    shown = os.path.relpath(out, ROOT).replace("\\", "/")
    wiki_git = base + ".wiki.git"
    print()
    print("publish_wiki: wrote %s: %d pages, %d pictures" % (shown, pages, pictures))
    print("  links made absolute: %d to files (blob), %d to folders (tree), %d to pictures and videos (raw)"
          % (counts["blob"], counts["tree"], counts["raw"]))
    print("  they point into %s, branch %s: push that branch first, or they lead nowhere" % (base, args.branch))
    print()
    if not os.path.isdir(os.path.join(out, ".git")):
        print("The folder is not a clone of the wiki yet. Once, before the three commands:")
        print("  1. on GitHub, open the repository's Wiki tab and save a first page: that creates the wiki's repository")
        print('  2. delete "%s", then: git clone %s "%s"' % (shown, wiki_git, shown))
        print("  3. run this script again: it fills the clone and keeps its .git folder")
        print()
    print("To publish, run these three commands yourself:")
    print('  git -C "%s" add -A' % shown)
    print('  git -C "%s" commit -m "The wiki, from wiki/ of the repository"' % shown)
    print('  git -C "%s" push' % shown)
    return 0


if __name__ == "__main__":
    sys.exit(main())
