#!/usr/bin/env python3
"""Copy one game source file into the port's build mirror, applying the few
IDO/N64-isms the port cannot absorb elsewhere:

  * `#pragma weak A = B` aliases (IDO-only; they crash GCC on Mach-O) become
    harmless unknown pragmas; port/src/port_aliases.c provides real wrappers.
  * Definitions of pinned globals (see gen_pins.py) are renamed to
    `NAME__sbk_unpinned`: the absolute symbol placing NAME inside the emulated
    RDRAM is the real object, the renamed definition is the "twin" whose initial
    value is copied there at startup.

    mirror_src.py --pins pins.txt src/x.c build/gen/src/x.c
"""
import argparse
import os
import re

# Column-0 lines that cannot be a global definition.
SKIP_PREFIX = ("extern", "static", "typedef", "#", "/", "*", " ", "\t", "}", "{", "return", "case", "default", "if", "else", "for", "while", "do", "switch", "goto")
IDENT_RE = re.compile(r"[A-Za-z_]\w*")


def find_definition(stripped, pinned):
    """If this column-0 line defines a pinned global, return (name, span) of
    the declarator name; handles `T name[N] = {`, `T name;` and
    `T (*name[N])(args) = {`. Function definitions/prototypes never match
    because function names are not pinned."""
    if stripped.startswith(SKIP_PREFIX):
        return None
    eq = stripped.find("=")
    semi = stripped.find(";")
    stop = min(x for x in (eq, semi) if x >= 0) if (eq >= 0 or semi >= 0) else -1
    if stop < 0:
        return None
    head = stripped[:stop]
    if "{" in head:
        return None
    for m in IDENT_RE.finditer(head):
        if m.group(0) in pinned:
            return m.group(0), m.span()
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--pins", help="pins.txt from gen_pins.py")
    ap.add_argument("--suffix", default="__sbk_unpinned")
    args = ap.parse_args()

    pinned = set()
    if args.pins:
        with open(args.pins) as f:
            for line in f:
                parts = line.split()
                if parts:
                    pinned.add(parts[0])

    out = []
    renamed = []
    with open(args.src) as f:
        for line in f:
            stripped = line.rstrip("\n")
            if stripped.startswith("#pragma weak "):
                line = "#pragma sbk_weak " + stripped[len("#pragma weak "):] + "\n"
            elif pinned and stripped:
                found = find_definition(stripped, pinned)
                if found:
                    name, (a, b) = found
                    eq = stripped.find("=")
                    decl = stripped[:eq].rstrip() if eq >= 0 else stripped[:stripped.find(";")]
                    # keep the original name declared for later uses in this file.
                    # The declared bounds are kept: game code takes sizeof() of
                    # pinned arrays (thread stacks), which an open [] would break.
                    decl_open = decl
                    line = ("extern %s;\n" % decl_open
                            + stripped[:a] + name + args.suffix + stripped[b:] + "\n")
                    renamed.append(name)
            out.append(line)
    # The N64 ELF's symbol sizes are unreliable for IDO data (textconv'd string
    # tables report only their initialised prefix); publish the real size of
    # every twin so the startup copy (gen_pins.py) can use it.
    for name in dict.fromkeys(renamed):  # a tentative declaration and its definition both match
        out.append("const unsigned long %s__sbk_size = sizeof(%s%s);\n" % (name, name, args.suffix))
    # Mach-O has no ".bss" section: the N64 build forces zero-initialised globals
    # there with __attribute__((section(".bss"))) and the BSS / BSS_ALIGN macros,
    # and Darwin's assembler rejects `.section .bss`. The attribute buys the port
    # nothing -- every one of these globals is pinned into the emulated RDRAM --
    # so it is dropped, keeping any alignment it was paired with.
    # sizeof()/ARRAY_SIZE() of a pinned global must measure the twin: the pinned
    # symbol is an absolute address whose extern declaration cannot carry a size
    # when the definition left the first dimension open (`T name[] = {...}`).
    text = "".join(out)
    for name in set(renamed):
        text = re.sub(r'\b(sizeof|ARRAY_SIZE)\s*\(\s*%s\s*\)' % re.escape(name),
                      r'\1(%s%s)' % (name, args.suffix), text)
    out = [text]

    debssed = []
    for line in "".join(out).splitlines(True):
        if not line.lstrip().startswith("#"):   # leave the macros' own definitions alone
            line = re.sub(r'__attribute__\s*\(\(\s*section\("\.bss"\)\s*,\s*aligned\((\w+)\)\s*\)\)', r'__attribute__((aligned(\1)))', line)
            line = re.sub(r'__attribute__\s*\(\(\s*aligned\((\w+)\)\s*,\s*section\("\.bss"\)\s*\)\)', r'__attribute__((aligned(\1)))', line)
            line = re.sub(r'__attribute__\s*\(\(\s*section\("\.bss"\)\s*\)\)', '', line)
            line = re.sub(r'\bBSS_ALIGN\s*\(\s*\w+\s*\)', '', line)
            line = re.sub(r'\bBSS\b(?!_)', '', line)
        debssed.append(line)
    out = debssed

    # Port patches: exact-text substitutions listed in port/patches.txt as
    #   relative/path.c\told text\tnew text
    # applied to the mirrored copy only. Each must match exactly once.
    text = "".join(out)
    patches = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "patches.txt")
    # key on the destination inside the gen tree (the source may be a textconv temp file)
    dst_abs = os.path.abspath(args.dst).replace(os.sep, "/")
    rel = dst_abs.split("/gen/", 1)[1] if "/gen/" in dst_abs else os.path.relpath(os.path.abspath(args.src), os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    if os.path.exists(patches):
        with open(patches) as pf:
            for line in pf:
                if not line.strip() or line.startswith("#"):
                    continue
                path, old, new = line.rstrip("\n").split("\t")
                if path != rel:
                    continue
                if text.count(old) != 1:
                    raise SystemExit("patches.txt: %s: expected exactly one match for %r, found %d" % (path, old, text.count(old)))
                text = text.replace(old, new)
    with open(args.dst, "w") as f:
        f.write(text)


if __name__ == "__main__":
    main()
