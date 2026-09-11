#!/usr/bin/env python3
"""Overlay dispatch: make one N64 address mean several native functions.

Snowboard Kids 2 loads its levels as overlays. Every level's code is linked at
the *same* VRAM address (0x800BB2B0), and so are the .race / .credits /
.cutscene / character- and player-select overlays (0x800B00C0). Resident code
calls into an overlay by address, so `scheduleTask(&renderFlyingEnemy, ...)` in
src/race/race_hud.c really means "the function at 0x800BB2B0", which is a
different function in each level -- the decomp just had to name that address
after one of them.

A native port links every overlay at once, so the address is gone and the name
is all that is left: the call would always reach Linda's Castle's function. So:

  * every name a group's addresses carry becomes a thunk (generated assembly)
    that jumps through a slot,
  * each overlay's real definitions are renamed with `__sbk_ov_<segment>`
    (mirror_src.py prepends the #defines listed in the renames file),
  * loading an overlay fills that group's slots with the loaded segment's own
    function pointers (port/src/ultra/os_overlay.c, called from the port's hook
    in dmaLoadAndInvalidate).

Every reference then resolves to whatever is loaded, from inside the overlay
and from the resident image alike, and function-pointer comparisons still work
because the thunk address is what everyone stores.

    gen_overlays.py snowboardkids2.map --ld snowboardkids2.ld \
        --thunks overlay_thunks.s --table overlay_table.c --renames renames.txt
"""
import argparse
import re
from collections import defaultdict

OBJ_RE = re.compile(r"^ \.(\w+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S+\.o)\s*$")
SYM_RE = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+([A-Za-z_][A-Za-z0-9_]*)\s*$")
ASSIGN_RE = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+([A-Za-z_][A-Za-z0-9_]*) = ")
SEG_RE = re.compile(r"^    (\w+)_ROM_START = __romPos;(.*?)^    \1_ROM_END = __romPos;", re.S | re.M)
OBJ_IN_SEG_RE = re.compile(r"(build/src/[^ )]+\.o)\(")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("map")
    ap.add_argument("--ld", required=True)
    ap.add_argument("--thunks", required=True)
    ap.add_argument("--table", required=True)
    ap.add_argument("--renames", required=True)
    ap.add_argument("--prefix", default="_")
    args = ap.parse_args()

    assigns = {}
    entries = []  # (addr, name, section, obj)
    cur = None
    with open(args.map) as f:
        for line in f:
            m = ASSIGN_RE.match(line)
            if m:
                assigns[m.group(2)] = int(m.group(1), 16)
                continue
            m = OBJ_RE.match(line)
            if m:
                cur = (m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4))
                continue
            m = SYM_RE.match(line)
            if m and cur is not None:
                addr = int(m.group(1), 16)
                sec, sec_addr, sec_size, obj = cur
                if sec_addr <= addr < sec_addr + max(sec_size, 1):
                    entries.append((addr, m.group(2), sec, obj))

    with open(args.ld) as f:
        ld = f.read()
    seg_objs = {}
    for name, body in SEG_RE.findall(ld):
        objs = sorted(set(OBJ_IN_SEG_RE.findall(body)))
        if objs:
            seg_objs[name] = objs

    # Group the segments that share a VRAM start; a group of one is resident.
    groups = defaultdict(list)
    for seg, objs in seg_objs.items():
        vram = assigns.get(seg + "_VRAM")
        end = assigns.get(seg + "_VRAM_END")
        if vram is None or end is None:
            continue
        groups[vram].append((seg, end, objs))
    groups = {v: segs for v, segs in groups.items() if len(segs) > 1}

    obj_syms = defaultdict(list)
    for addr, name, sec, obj in entries:
        if sec == "text":
            obj_syms[obj].append((addr, name))

    slot = 0
    thunks = []          # (name, slot index)
    table_segs = []      # (segment, rom_start, group_lo, group_hi, [(slot, name) ...])
    renames = defaultdict(list)  # source path -> [names]
    total_names = 0
    for vram in sorted(groups):
        group_lo = slot
        addr_slot = {}
        for seg, end, objs in sorted(groups[vram]):
            for obj in objs:
                for addr, name in obj_syms.get(obj, []):
                    if addr not in addr_slot:
                        addr_slot[addr] = None
        for addr in sorted(addr_slot):
            addr_slot[addr] = slot
            slot += 1
        for seg, end, objs in sorted(groups[vram]):
            suffix = "__sbk_ov_" + seg
            rows = []
            for obj in objs:
                src = obj.replace("build/", "", 1)[:-2] + ".c"
                for addr, name in sorted(obj_syms.get(obj, [])):
                    rows.append((addr_slot[addr], name + suffix))
                    thunks.append((name, addr_slot[addr]))
                    renames[src].append((name, name + suffix))
                    total_names += 1
            rom = assigns.get(seg + "_ROM_START")
            if rom is None:
                continue
            table_segs.append([seg, rom, group_lo, slot, sorted(rows)])

    p = args.prefix
    with open(args.thunks, "w") as out:
        out.write("; generated by port/tools/gen_overlays.py: %d overlay thunks\n" % len(thunks))
        out.write("\t.text\n")
        for name, k in thunks:
            out.write("\t.globl %s%s\n%s%s:\n" % (p, name, p, name))
            out.write("\tlis r12,ha16(%ssbk_ov_slot+%d)\n" % (p, k * 4))
            out.write("\tlwz r12,lo16(%ssbk_ov_slot+%d)(r12)\n" % (p, k * 4))
            out.write("\tmtctr r12\n\tbctr\n")

    with open(args.table, "w") as out:
        out.write("/* generated by port/tools/gen_overlays.py: %d overlays, %d slots */\n" % (len(table_segs), slot))
        out.write("#include <stdint.h>\n#include \"ultra/sbk_overlay.h\"\n\n")
        out.write("sbk_ov_fn sbk_ov_slot[%d];\nconst int sbk_ov_slot_count = %d;\n\n" % (max(slot, 1), slot))
        seen = set()
        for seg, rom, glo, ghi, rows in table_segs:
            for k, name in rows:
                if name not in seen:
                    seen.add(name)
                    out.write("extern void %s();\n" % name)
        out.write("\n")
        for seg, rom, glo, ghi, rows in table_segs:
            out.write("static const struct sbk_ov_entry ov_%s[] = {\n" % seg)
            for k, name in rows:
                out.write("    { %d, (sbk_ov_fn)%s },\n" % (k, name))
            out.write("};\n")
        out.write("\nconst struct sbk_ov_segment sbk_ov_segments[] = {\n")
        for seg, rom, glo, ghi, rows in table_segs:
            out.write("    { \"%s\", 0x%08Xu, ov_%s, %d, %d, %d },\n" % (seg, rom, seg, len(rows), glo, ghi))
        out.write("    { 0, 0, 0, 0, 0, 0 }\n};\n")

    with open(args.renames, "w") as out:
        for src in sorted(renames):
            for name, new in renames[src]:
                out.write("%s %s %s\n" % (src, name, new))

    print("overlays: %d groups, %d segments, %d slots, %d thunks -> %s"
          % (len(groups), len(table_segs), slot, len(thunks), args.thunks))


if __name__ == "__main__":
    main()
