#!/usr/bin/env python3
"""Compiles the C# samples of the wiki's pages (every ```csharp block of wiki/*.md) against the real package.

From the repository's root:

    python tools/check_wiki_code.py              write the samples as C# files into the test project; say what was found
    python tools/check_wiki_code.py --compile    ... then let Unity compile them and map every error back to its page
    python tools/check_wiki_code.py --log FILE   map the errors of a Unity log made earlier back to the pages
    python tools/check_wiki_code.py --clean      remove the generated folder again

The samples are written to tests/unity-inspectors/Assets/WikiSamples (generated, not tracked: .gitignore names it),
one file for each ```csharp block, each in a namespace of its own. --compile starts the editor in batch mode on
tests/unity-inspectors, which is the same as

    "$UNITY" -batchmode -quit -projectPath "$PWD/tests/unity-inspectors" -logFile out/wiki-code.log

UNITY is the environment variable of that name, or Unity 6000.3.8f1 where the Unity Hub puts it. Never run it
while another Unity has tests/unity-inspectors open. The exit code is 0 when every sample compiled.

How a block is read:

  complete    it declares a class, struct, interface or enum at the left margin: it is compiled as it stands.
  fragment    anything else. It is wrapped into a class deriving MonoBehaviour, with
              "using UnityEngine; using CatSplat.SfxForge; using System.Collections.Generic;" before it.
              Statements go into a method; a fragment that declares methods goes into the class itself.
              What the fragment reads but does not declare is named in a comment line of the fragment:
                  // in: SfxPlayer player, SfxPreset laser
              and becomes fields of the class. "using X;" lines of a fragment move to the top.
  not compiled  the first line is "// not compiled: <why>": counted and listed, never compiled.

Where a sample is compiled (the assembly definition beside it gives it what a reader's project would have):

  Runtime     references CatSplat.SfxForge
  Editor      it names UnityEditor or CatSplat.SfxForge.Editor: also references CatSplat.SfxForge.Editor
  Tests       it names NUnit.Framework: also references the test framework (a PlayMode assembly: an EditMode
              run compiles it and runs nothing of it)
  Showcase    it names SfxShowcase.: also references copies of the showcase's Scripts/Core and Scripts/Audio

A sample sees the complete Runtime samples that stand before it on the same page (a component, then its editor).
manifest.json in the generated folder maps every generated file and line back to its page, block and line.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WIKI = ROOT / "wiki"
PROJECT = ROOT / "tests" / "unity-inspectors"
OUT = PROJECT / "Assets" / "WikiSamples"
OUT_REL = "Assets/WikiSamples"
SHOWCASE = ROOT / "unity" / "Demo" / "Assets" / "Showcase" / "Scripts"
DEFAULT_UNITY = "C:/Program Files/Unity/Hub/Editor/6000.3.8f1/Editor/Unity.exe"
DEFAULT_LOG = ROOT / "out" / "wiki-code.log"

FENCE_OPEN = re.compile(r"^(\s*)```csharp\s*$")
TYPE_DECL = re.compile(
    r"^(?:(?:public|internal|private|static|sealed|abstract|partial|readonly|unsafe)\s+)*"
    r"(?:class|struct|interface|enum)\s+\w+"
)
USING_LINE = re.compile(r"^using\s+(?:static\s+)?[\w.]+(?:\s*=\s*[\w.<>]+)?\s*;\s*(?://.*)?$")
METHOD_DECL = re.compile(
    r"^(?:(?:public|private|protected|internal|static|override|virtual|async)\s+)*"
    r"(?!(?:return|new|await|yield|throw|else|using|var)\b)[\w<>\[\],.]+\s+\w+\s*\([^;]*\)\s*(?:=>.*|\{?\s*)$"
)
IN_LINE = re.compile(r"^\s*//\s*in:\s*(.+?)\s*$")
NOT_COMPILED = re.compile(r"^\s*//\s*not compiled:\s*(.+?)\s*$")
# Warnings that only say "this sample does not use what it declares": expected of samples.
QUIET = "#pragma warning disable 0168, 0169, 0219, 0414, 0649, 8321"
FRAGMENT_USINGS = ["System.Collections.Generic", "UnityEngine", "CatSplat.SfxForge"]

ASMDEFS = {
    "Runtime": {
        "name": "WikiSamples",
        "references": ["CatSplat.SfxForge"],
        "autoReferenced": False,
    },
    "Editor": {
        "name": "WikiSamples.Editor",
        "references": ["CatSplat.SfxForge", "CatSplat.SfxForge.Editor", "WikiSamples"],
        "includePlatforms": ["Editor"],
        "autoReferenced": False,
    },
    "Tests": {
        "name": "WikiSamples.Tests",
        "references": ["CatSplat.SfxForge", "WikiSamples", "UnityEngine.TestRunner"],
        "overrideReferences": True,
        "precompiledReferences": ["nunit.framework.dll"],
        "defineConstraints": ["UNITY_INCLUDE_TESTS"],
        "autoReferenced": False,
    },
    "Showcase": {
        "name": "WikiSamples.Showcase",
        "references": ["CatSplat.SfxForge", "WikiSamples", "SfxShowcase.Core", "SfxShowcase.Audio"],
        "autoReferenced": False,
    },
}


class Block:
    def __init__(self, page, index, line, lines):
        self.page = page  # "Unity-One-Shots.md"
        self.index = index  # 1-based, among the page's csharp blocks
        self.line = line  # the page's line number of the block's first code line
        self.lines = lines
        self.reason = None
        self.kind = self.classify()
        self.group = self.where()
        stem = page[:-3].replace("-", "_")
        self.namespace = f"WikiSamples.{stem}.B{index:02d}"
        self.file = f"{stem}_B{index:02d}.cs"

    def classify(self):
        for text in self.lines:
            if text.strip():
                found = NOT_COMPILED.match(text)
                if found:
                    self.reason = found.group(1)
                    return "not compiled"
                break
        if any(TYPE_DECL.match(text) for text in self.lines):
            return "complete"
        return "fragment"

    def where(self):
        code = "\n".join(self.lines)
        if "NUnit.Framework" in code:
            return "Tests"
        if "SfxShowcase." in code:
            return "Showcase"
        if re.search(r"\bUnityEditor\b|CatSplat\.SfxForge\.Editor\b", code):
            return "Editor"
        return "Runtime"

    @property
    def label(self):
        return f"wiki/{self.page} block {self.index} (line {self.line})"


def read_blocks():
    blocks = []
    for path in sorted(WIKI.glob("*.md")):
        lines = path.read_text(encoding="utf-8").split("\n")
        index = 0
        at = 0
        while at < len(lines):
            opened = FENCE_OPEN.match(lines[at])
            if not opened:
                at += 1
                continue
            indent = opened.group(1)
            first = at + 1
            at = first
            while at < len(lines) and lines[at].strip() != "```":
                at += 1
            body = [t[len(indent):] if t.startswith(indent) else t.lstrip() for t in lines[first:at]]
            index += 1
            blocks.append(Block(path.name, index, first + 1, body))
            at += 1
    return blocks


def split_fields(text):
    """'SfxPlayer player, Dictionary<int, AudioClip> clips' -> the declarations, split at the outer commas."""
    parts, depth, start = [], 0, 0
    for i, c in enumerate(text):
        if c in "<([":
            depth += 1
        elif c in ">)]":
            depth -= 1
        elif c == "," and depth == 0:
            parts.append(text[start:i])
            start = i + 1
    parts.append(text[start:])
    return [p.strip() for p in parts if p.strip()]


def generate(block, earlier):
    """The generated file's text, and the generated line number of the block's first line."""
    head = [QUIET, f"// generated by tools/check_wiki_code.py from {block.label}: do not edit",
            f"namespace {block.namespace}", "{"]
    head += [f"using {ns};" for ns in earlier]
    body = list(block.lines)
    if block.kind == "complete":
        return "\n".join(head + body + ["}"]) + "\n", len(head) + 1

    usings = list(FRAGMENT_USINGS)
    if block.group == "Editor":
        usings.append("UnityEditor")
    fields = []
    for i, text in enumerate(body):
        if USING_LINE.match(text):
            usings.append(text.strip())
            body[i] = ""
            continue
        named = IN_LINE.match(text)
        if named:
            fields += split_fields(named.group(1))
    seen = []
    for u in usings:
        line = u if u.startswith("using") else f"using {u};"
        if line not in seen:
            seen.append(line)
    head += seen
    head += ["public class Fragment : MonoBehaviour", "{"]
    head += [f"public {f};" for f in fields]
    members = any(METHOD_DECL.match(text) for text in body)
    if members:
        tail = ["}", "}"]
    else:
        head += ["void Run()", "{"]
        tail = ["}", "}", "}"]
    return "\n".join(head + body + tail) + "\n", len(head) + 1


def write_if_changed(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text(encoding="utf-8") == text:
        return
    path.write_text(text, encoding="utf-8", newline="\n")


def copy_showcase(wanted):
    """Copies of the showcase's two assemblies the samples talk to: the code and the .asmdef, no .meta."""
    for part in ("Core", "Audio"):
        source = SHOWCASE / part
        if not source.is_dir():
            sys.exit(f"check_wiki_code: {source.relative_to(ROOT).as_posix()} is missing")
        for item in sorted(source.iterdir()):
            if item.suffix in (".cs", ".asmdef"):
                target = OUT / "ShowcaseCopy" / part / item.name
                write_if_changed(target, item.read_text(encoding="utf-8"))
                wanted.add(target)


def write_samples(blocks):
    wanted = set()
    manifest = {"generated by": "tools/check_wiki_code.py", "files": {}, "not compiled": []}
    groups = set()
    runtime_before = {}  # page -> namespaces of the complete Runtime samples so far
    for block in blocks:
        if block.kind == "not compiled":
            manifest["not compiled"].append(
                {"page": f"wiki/{block.page}", "block": block.index, "line": block.line, "why": block.reason}
            )
            continue
        earlier = runtime_before.setdefault(block.page, [])
        text, first = generate(block, earlier)
        target = OUT / block.group / block.file
        write_if_changed(target, text)
        wanted.add(target)
        groups.add(block.group)
        manifest["files"][f"{OUT_REL}/{block.group}/{block.file}"] = {
            "page": f"wiki/{block.page}",
            "block": block.index,
            "kind": block.kind,
            "page line of the first code line": block.line,
            "generated line of the first code line": first,
            "lines": len(block.lines),
        }
        if block.kind == "complete" and block.group == "Runtime":
            runtime_before[block.page] = earlier + [block.namespace]
    groups.add("Runtime")  # the others reference it, so it must exist
    for group in sorted(groups):
        target = OUT / group / (ASMDEFS[group]["name"] + ".asmdef")
        write_if_changed(target, json.dumps(ASMDEFS[group], indent=2) + "\n")
        wanted.add(target)
    if not any(p.suffix == ".cs" for p in wanted if p.parent == OUT / "Runtime"):
        target = OUT / "Runtime" / "Empty.cs"
        write_if_changed(target, "// generated: an assembly needs a file\nnamespace WikiSamples { static class Empty { } }\n")
        wanted.add(target)
    if "Showcase" in groups:
        copy_showcase(wanted)
    target = OUT / "manifest.json"
    write_if_changed(target, json.dumps(manifest, indent=2) + "\n")
    wanted.add(target)
    # what an earlier run wrote and this one did not: away with it (and with its .meta)
    for path in sorted(OUT.rglob("*"), reverse=True):
        if path.is_dir():
            if not any(path.iterdir()):
                path.rmdir()
                meta = path.with_name(path.name + ".meta")
                if meta.exists():
                    meta.unlink()
        elif path.suffix == ".meta":
            if not path.with_suffix("").exists():
                path.unlink()
        elif path not in wanted:
            path.unlink()
    return manifest, sorted(groups)


def summary(blocks):
    count = {"complete": 0, "fragment": 0, "not compiled": 0}
    for block in blocks:
        count[block.kind] += 1
    pages = len({b.page for b in blocks})
    print(f"wiki code: {len(blocks)} csharp blocks on {pages} pages: {count['complete']} complete, "
          f"{count['fragment']} fragments, {count['not compiled']} not compiled")
    for block in blocks:
        if block.kind == "not compiled":
            print(f"  not compiled: {block.label}: {block.reason}")


MESSAGE = re.compile(
    r"^(?P<file>Assets[\\/]WikiSamples[\\/][^(]+)\((?P<line>\d+),(?P<col>\d+)\): (?P<kind>error|warning) (?P<text>.*)$"
)
OTHER_ERROR = re.compile(r"^(?P<where>\S.*\(\d+,\d+\)): error (?P<text>CS\d+.*)$")


def read_log(log, manifest):
    """Prints every error and warning of the samples with its page and line. Returns the number of errors."""
    if not log.exists():
        print(f"wiki code: no log at {log}")
        return 1
    errors, warnings, seen = 0, 0, set()
    for raw in log.read_text(encoding="utf-8", errors="replace").splitlines():
        text = raw.strip()
        if text in seen:
            continue
        found = MESSAGE.match(text)
        if not found:
            other = OTHER_ERROR.match(text)
            if other:
                seen.add(text)
                errors += 1
                print(f"  ERROR (not in a sample) {text}")
            continue
        seen.add(text)
        file = found.group("file").replace("\\", "/")
        entry = manifest["files"].get(file)
        if entry is None:
            where = file + " (the showcase's copy or a file of an earlier run)"
        else:
            line = int(found.group("line")) - entry["generated line of the first code line"]
            if 0 <= line < entry["lines"]:
                where = f"{entry['page']}:{entry['page line of the first code line'] + line} (block {entry['block']})"
            else:
                where = f"{entry['page']} block {entry['block']}, in the wrapper the tool wrote around it ({file})"
        if found.group("kind") == "error":
            errors += 1
            print(f"  ERROR   {where}: {found.group('text')}")
        elif entry is not None:
            warnings += 1
            print(f"  warning {where}: {found.group('text')}")
    print(f"wiki code: {errors} errors, {warnings} warnings in the samples ({log.relative_to(ROOT).as_posix()})")
    return errors


def missing_assemblies(groups):
    """The names of the sample assemblies Unity has not built. (Their dates say nothing: Unity keeps an
    assembly as it is when the code it would be built from has not changed.)"""
    built = PROJECT / "Library" / "ScriptAssemblies"
    return [ASMDEFS[g]["name"] for g in groups if not (built / (ASMDEFS[g]["name"] + ".dll")).exists()]


def compile_with_unity(log):
    unity = os.environ.get("UNITY", DEFAULT_UNITY)
    if not Path(unity).exists():
        sys.exit(f"check_wiki_code: no Unity at {unity}: set the environment variable UNITY")
    if (PROJECT / "Temp" / "UnityLockfile").exists():
        print("wiki code: tests/unity-inspectors/Temp/UnityLockfile exists: is the project open in another Unity?")
    log.parent.mkdir(parents=True, exist_ok=True)
    if log.exists():
        log.unlink()
    print("wiki code: Unity compiles the samples (a minute or two) ...")
    done = subprocess.run([unity, "-batchmode", "-quit", "-projectPath", str(PROJECT), "-logFile", str(log)])
    return done.returncode


def main():
    parser = argparse.ArgumentParser(description="Compiles the C# samples of wiki/*.md against the package.")
    parser.add_argument("--compile", action="store_true", help="let Unity compile the samples, then read its log")
    parser.add_argument("--log", metavar="FILE", help="read this Unity log instead of starting Unity")
    parser.add_argument("--clean", action="store_true", help="remove the generated folder and stop")
    args = parser.parse_args()

    if args.clean:
        shutil.rmtree(OUT, ignore_errors=True)
        meta = OUT.with_name(OUT.name + ".meta")
        if meta.exists():
            meta.unlink()
        print(f"wiki code: removed tests/unity-inspectors/{OUT_REL}")
        return 0

    blocks = read_blocks()
    manifest, groups = write_samples(blocks)
    summary(blocks)
    print(f"wiki code: {len(manifest['files'])} files written to tests/unity-inspectors/{OUT_REL}")
    if args.log:
        return 1 if read_log(Path(args.log).resolve(), manifest) else 0
    if not args.compile:
        return 0

    code = compile_with_unity(DEFAULT_LOG)
    errors = read_log(DEFAULT_LOG, manifest)
    missing = missing_assemblies(groups)
    failed = "Scripts have compiler errors" in DEFAULT_LOG.read_text(encoding="utf-8", errors="replace")
    if errors == 0 and code == 0 and not missing and not failed:
        print("wiki code: every sample compiled")
        return 0
    if missing:
        print("wiki code: Unity did not build " + ", ".join(missing) + ": read the log")
    if code != 0 or failed:
        print(f"wiki code: Unity ended with exit code {code}" + (": scripts have compiler errors" if failed else ""))
    return 1


if __name__ == "__main__":
    sys.exit(main())
