#!/usr/bin/env python3
"""Takes the debug information out of the built Vibe, so that it can be given away.

    python tools/strip_wasm_debug.py build/sfx-forge.wasm [build/sfx-forge.zip]

The SDK's build leaves DWARF sections (.debug_info, .debug_line, ...) in the .wasm, and they hold the full paths
of the folders it was built in. This removes every custom section whose name starts with ".debug_" (and
"sourceMappingURL" / "external_debug_info", if there) from the .wasm, in place, and writes the zip again with
that .wasm and its other members as they were. Code, data, exports and the "name" section are not touched, so
the instrument is the same. Running it twice changes nothing. The zip is written with a fixed date, so the same
members always give the same bytes.

It also has the check the package generator uses: paths_in(bytes) lists what looks like a machine path.
"""
import os
import re
import sys
import zipfile

DROPPED = ("sourceMappingURL", "external_debug_info")
ZIP_DATE = (1980, 1, 1, 0, 0, 0)


def leb(data, at):
    value = 0
    shift = 0
    while True:
        byte = data[at]
        at += 1
        value |= (byte & 0x7F) << shift
        shift += 7
        if not byte & 0x80:
            return value, at


def sections(data):
    """(id, name or None, start, end) of every section; end is one past its last byte."""
    if data[:8] != b"\0asm\x01\0\0\0":
        raise ValueError("not a WebAssembly module (version 1)")
    at = 8
    found = []
    while at < len(data):
        start = at
        kind = data[at]
        size, body = leb(data, at + 1)
        end = body + size
        if end > len(data):
            raise ValueError("a section runs past the end of the file")
        name = None
        if kind == 0:
            length, text = leb(data, body)
            name = data[text : text + length].decode("utf-8", "replace")
        found.append((kind, name, start, end))
        at = end
    return found


def is_debug(name):
    return name is not None and (name.startswith(".debug_") or name in DROPPED)


def debug_sections(data):
    return [(name, end - start) for kind, name, start, end in sections(data) if is_debug(name)]


def strip(data):
    kept = [data[:8]]
    dropped = []
    for kind, name, start, end in sections(data):
        if is_debug(name):
            dropped.append((name, end - start))
        else:
            kept.append(data[start:end])
    return b"".join(kept), dropped


def paths_in(data):
    """What looks like an absolute path of a machine: a drive letter, or a home folder."""
    found = set()
    for match in re.finditer(
        rb"(?<![A-Za-z0-9])[A-Za-z]:[\/][A-Za-z0-9_. \/-]{3,60}|/(?:home|Users)/[A-Za-z0-9_. /-]{2,60}", data
    ):
        found.add(match.group(0).decode("latin-1"))
    return sorted(found)


def main():
    arguments = sys.argv[1:]
    if not 1 <= len(arguments) <= 2:
        sys.exit(__doc__)
    wasm_path = arguments[0]
    with open(wasm_path, "rb") as f:
        original = f.read()
    stripped, dropped = strip(original)
    if dropped:
        with open(wasm_path, "wb") as f:
            f.write(stripped)
    print(
        "strip_wasm_debug: %s: %d -> %d bytes, %d debug sections removed%s"
        % (
            os.path.basename(wasm_path),
            len(original),
            len(stripped),
            len(dropped),
            "" if dropped else " (there were none)",
        )
    )
    left = paths_in(stripped)
    if left:
        sys.exit(
            "strip_wasm_debug: ERROR: the stripped .wasm still holds what looks like a machine path: "
            + ", ".join(left[:5])
        )

    if len(arguments) == 2:
        zip_path = arguments[1]
        member = os.path.basename(wasm_path)
        with zipfile.ZipFile(zip_path) as z:
            members = [(info.filename, z.read(info.filename)) for info in z.infolist()]
        if member not in [name for name, _ in members]:
            sys.exit("strip_wasm_debug: ERROR: %s is not in %s" % (member, zip_path))
        temporary = zip_path + ".tmp"
        with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for name, content in members:
                info = zipfile.ZipInfo(name, ZIP_DATE)
                info.compress_type = zipfile.ZIP_DEFLATED
                info.create_system = 0
                info.external_attr = 0
                z.writestr(info, stripped if name == member else content, compresslevel=9)
        os.replace(temporary, zip_path)
        print(
            "strip_wasm_debug: %s written again with %d members, %d bytes"
            % (os.path.basename(zip_path), len(members), os.path.getsize(zip_path))
        )


if __name__ == "__main__":
    main()
