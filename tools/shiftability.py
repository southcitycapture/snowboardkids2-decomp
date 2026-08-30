#!/usr/bin/env python3
"""Audit and test Snowboard Kids 2 ROM/VRAM shiftability."""

from __future__ import annotations

import argparse
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

import yaml


ROOT = Path(__file__).resolve().parents[1]
ROM_SHIFT = 0x10
ROM_SYMBOL_SUFFIXES = ("_ROM_START", "_ROM_END")
RUNTIME_TABLES = (
    "itemAssetTable",
    "gBossHudAssetTable",
    "gCutsceneFadeAssetTable",
    "D_800BA624_1E76D4",
    "gSpriteAssetTable",
    "gSpriteDmaTable",
)


def run(command: list[str], *, capture: bool = False) -> str:
    result = subprocess.run(
        command,
        cwd=ROOT,
        check=True,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
    )
    return result.stdout if capture else ""


def tool(name: str) -> str:
    for prefix in ("mips-linux-gnu-", "mips64-linux-gnu-", "mips64-elf-"):
        candidate = shutil.which(prefix + name)
        if candidate:
            return candidate
    raise RuntimeError(f"unable to find a MIPS {name} tool")


@dataclass
class Section:
    name: str
    type: int
    flags: int
    addr: int
    offset: int
    size: int
    link: int
    info: int
    entsize: int


@dataclass
class Symbol:
    name: str
    value: int
    size: int
    shndx: int


class Elf32:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        header = struct.unpack_from(">16sHHIIIIIHHHHHH", self.data, 0)
        if header[0][:6] != b"\x7fELF\x01\x02":
            raise ValueError(f"{path} is not a big-endian ELF32 file")
        shoff, shentsize, shnum, shstrndx = header[6], header[11], header[12], header[13]
        raw_sections = [
            struct.unpack_from(">IIIIIIIIII", self.data, shoff + i * shentsize)
            for i in range(shnum)
        ]
        shstr = raw_sections[shstrndx]
        names = self.data[shstr[4] : shstr[4] + shstr[5]]
        self.sections: list[Section] = []
        for raw in raw_sections:
            name_end = names.find(b"\0", raw[0])
            name = names[raw[0] : name_end].decode("ascii", errors="replace") if raw[0] else ""
            self.sections.append(
                Section(name, raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7], raw[9])
            )
        self.symbols: dict[str, Symbol] = {}
        for section in self.sections:
            if section.type != 2 or section.entsize == 0:  # SHT_SYMTAB
                continue
            strings_section = self.sections[section.link]
            strings = self.section_data(strings_section)
            for offset in range(section.offset, section.offset + section.size, section.entsize):
                name_offset, value, size, _, _, shndx = struct.unpack_from(">IIIBBH", self.data, offset)
                if name_offset == 0:
                    continue
                end = strings.find(b"\0", name_offset)
                name = strings[name_offset:end].decode("ascii", errors="replace")
                self.symbols[name] = Symbol(name, value, size, shndx)

    def section_data(self, section: Section) -> bytes:
        return self.data[section.offset : section.offset + section.size]

    def symbol_data(self, name: str) -> bytes:
        symbol = self.symbols[name]
        section = self.sections[symbol.shndx]
        start = section.offset + symbol.value - section.addr
        return self.data[start : start + symbol.size]

    def bytes_at_symbol(self, name: str, size: int) -> bytes:
        symbol = self.symbols[name]
        section = self.sections[symbol.shndx]
        start = section.offset + symbol.value - section.addr
        return self.data[start : start + size]


def load_rom_boundaries() -> dict[int, set[str]]:
    config = yaml.safe_load((ROOT / "snowboardkids2.yaml").read_text())
    segments = [
        segment
        for segment in config["segments"]
        if isinstance(segment, dict) and isinstance(segment.get("start"), int)
    ]
    boundaries: dict[int, set[str]] = {}
    for index, segment in enumerate(segments):
        boundaries.setdefault(segment["start"], set()).add(segment["name"] + "_ROM_START")
        if index + 1 < len(segments):
            boundaries.setdefault(segments[index + 1]["start"], set()).add(segment["name"] + "_ROM_END")
    return boundaries


def linked_project_objects() -> list[Path]:
    script = (ROOT / "snowboardkids2.ld").read_text()
    paths = set(re.findall(r"build/(?:src|asm|lib/libkmc)/[^\s;()]+\.o", script))
    return sorted(ROOT / path for path in paths if (ROOT / path).is_file())


def audit_linked_objects(boundaries: dict[int, set[str]]) -> None:
    candidates: list[str] = []
    scanned_words = 0
    for path in linked_project_objects():
        elf = Elf32(path)
        relocated: dict[int, set[int]] = {}
        for section in elf.sections:
            if section.type not in (4, 9):  # SHT_RELA/SHT_REL
                continue
            entry_size = section.entsize or (12 if section.type == 4 else 8)
            offsets = relocated.setdefault(section.info, set())
            for entry_offset in range(section.offset, section.offset + section.size, entry_size):
                offsets.add(struct.unpack_from(">I", elf.data, entry_offset)[0])
        for index, section in enumerate(elf.sections):
            if section.type != 1 or not (section.flags & 0x2):  # allocated SHT_PROGBITS
                continue
            data = elf.section_data(section)
            for offset in range(0, len(data) - 3, 4):
                scanned_words += 1
                value = struct.unpack_from(">I", data, offset)[0]
                if value in boundaries and value >= 0x1050 and offset not in relocated.get(index, set()):
                    names = ", ".join(sorted(boundaries[value]))
                    candidates.append(f"{path.relative_to(ROOT)}:{section.name}+0x{offset:X}: 0x{value:08X} ({names})")
    if candidates:
        raise RuntimeError("unrelocated ROM-boundary literals in linked objects:\n  " + "\n  ".join(candidates))
    print(f"linked-object audit: {len(linked_project_objects())} objects, {scanned_words} words, no candidates")


def audit_vram_literals() -> None:
    elf_path = ROOT / "build/snowboardkids2.elf"
    if not elf_path.is_file():
        raise RuntimeError("build/snowboardkids2.elf is missing; run make first")
    project_symbols: dict[int, set[str]] = {}
    for name, value in load_nm_symbols(elf_path).items():
        if 0x80000400 <= value < 0x80400000:
            project_symbols.setdefault(value, set()).add(name)
    allowlist_data = yaml.safe_load((ROOT / "config/shiftability_vram_constants.yaml").read_text())
    allowlist = {
        (entry["path"], entry["section"], int(entry["offset"]), int(entry["value"]))
        for entry in allowlist_data
    }
    seen_allowlist: set[tuple[str, str, int, int]] = set()
    candidates: list[str] = []
    for path in linked_project_objects():
        elf = Elf32(path)
        relocated: dict[int, set[int]] = {}
        for section in elf.sections:
            if section.type not in (4, 9):
                continue
            entry_size = section.entsize or (12 if section.type == 4 else 8)
            offsets = relocated.setdefault(section.info, set())
            for entry_offset in range(section.offset, section.offset + section.size, entry_size):
                offsets.add(struct.unpack_from(">I", elf.data, entry_offset)[0])
        relative = str(path.relative_to(ROOT))
        for index, section in enumerate(elf.sections):
            if section.type != 1 or not (section.flags & 0x2):
                continue
            data = elf.section_data(section)
            for offset in range(0, len(data) - 3, 4):
                value = struct.unpack_from(">I", data, offset)[0]
                if value not in project_symbols or offset in relocated.get(index, set()):
                    continue
                key = (relative, section.name, offset, value)
                if key in allowlist:
                    seen_allowlist.add(key)
                else:
                    names = ", ".join(sorted(project_symbols[value])[:5])
                    candidates.append(f"{relative}:{section.name}+0x{offset:X}: 0x{value:08X} ({names})")
    stale = allowlist - seen_allowlist
    if candidates or stale:
        details = []
        if candidates:
            details.append("unrelocated project-VRAM literals:\n  " + "\n  ".join(candidates))
        if stale:
            details.append(
                "stale platform-constant entries:\n  "
                + "\n  ".join(f"{p}:{s}+0x{o:X}: 0x{v:08X}" for p, s, o, v in sorted(stale))
            )
        raise RuntimeError("VRAM-literal audit failed:\n" + "\n".join(details))
    print(f"VRAM-literal audit: {len(seen_allowlist)} fixed platform constants classified; no project-local candidates")


def audit_binary_assets(boundaries: dict[int, set[str]]) -> None:
    allowlist_data = yaml.safe_load((ROOT / "config/shiftability_binary_false_positives.yaml").read_text())
    allowlist = {
        (entry["path"], int(entry["offset"]), int(entry["value"]))
        for entry in allowlist_data
    }
    seen_allowlist: set[tuple[str, int, int]] = set()
    unexpected: list[str] = []
    match_count = 0
    for path in sorted((ROOT / "assets").rglob("*.bin")):
        relative = str(path.relative_to(ROOT))
        data = path.read_bytes()
        for offset in range(0, len(data) - 3, 4):
            value = struct.unpack_from(">I", data, offset)[0]
            if value not in boundaries or value < 0x1050:
                continue
            match_count += 1
            key = (relative, offset, value)
            if key in allowlist:
                seen_allowlist.add(key)
            else:
                unexpected.append(f"{relative}+0x{offset:X}: 0x{value:08X}")
    stale = allowlist - seen_allowlist
    if unexpected or stale:
        details = []
        if unexpected:
            details.append("unclassified matches:\n  " + "\n  ".join(unexpected))
        if stale:
            details.append(
                "stale allowlist entries:\n  "
                + "\n  ".join(f"{p}+0x{o:X}: 0x{v:08X}" for p, o, v in sorted(stale))
            )
        raise RuntimeError("binary-asset ROM audit failed:\n" + "\n".join(details))
    print(f"binary-asset audit: {match_count} coincidental matches, all classified; no runtime ROM tables")


def load_nm_symbols(elf_path: Path) -> dict[str, int]:
    output = run([tool("nm"), "-n", str(elf_path)], capture=True)
    symbols = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 3 and re.fullmatch(r"[0-9a-fA-F]+", parts[0]):
            symbols[parts[2]] = int(parts[0], 16)
    return symbols


def verify_runtime_words(base: Elf32, shifted: Elf32, base_rom_symbols: dict[int, int]) -> int:
    checked = 0
    for table_name in RUNTIME_TABLES:
        table_checked = 0
        base_data = base.symbol_data(table_name)
        shifted_data = shifted.symbol_data(table_name)
        for offset in range(0, min(len(base_data), len(shifted_data)) - 3, 4):
            base_word = struct.unpack_from(">I", base_data, offset)[0]
            if base_word not in base_rom_symbols:
                continue
            shifted_word = struct.unpack_from(">I", shifted_data, offset)[0]
            expected = base_rom_symbols[base_word]
            # A non-pointer field can coincidentally equal a ROM boundary. The
            # object audit independently rejects unrelocated boundary words;
            # here we count fields that demonstrably followed the shifted
            # linker symbol.
            if shifted_word == expected:
                table_checked += 1
        if table_checked == 0:
            raise RuntimeError(f"no relocated ROM words found in runtime table {table_name}")
        checked += table_checked
    return checked


def test_rom_shift(output_path: Path) -> None:
    base_path = ROOT / "build/snowboardkids2.elf"
    if not base_path.is_file():
        raise RuntimeError("build/snowboardkids2.elf is missing; run make first")
    with tempfile.TemporaryDirectory(prefix="sbk2-rom-shift-") as temporary:
        temp = Path(temporary)
        linker_text = (ROOT / "snowboardkids2.ld").read_text()
        marker = "    main_ROM_START = __romPos;"
        replacement = f"    __romPos += 0x{ROM_SHIFT:X};\n{marker}"
        if linker_text.count(marker) != 1:
            raise RuntimeError("could not identify the early-ROM shift insertion point")
        shifted_script = temp / "snowboardkids2-shifted.ld"
        shifted_script.write_text(linker_text.replace(marker, replacement))
        shifted_elf = temp / "snowboardkids2-shifted.elf"
        command = [
            tool("ld"), "-T", str(shifted_script), "-Map", str(temp / "snowboardkids2-shifted.map"),
            "--no-check-sections", "-u", "osPfsIsPlug", "-Lbuild/lib", "-lmus", "-lgultra_rom",
            "-T", "linker_scripts/hardware_regs.ld", "-T", "linker_scripts/libultra_syms.ld",
            "-T", "linker_scripts/data_field_syms.ld", "-T", "linker_scripts/memory_layout_assertions.ld",
            "-o", str(shifted_elf),
        ]
        run(command)
        shifted_rom = temp / "snowboardkids2-shifted.z64"
        run([tool("objcopy"), "-O", "binary", str(shifted_elf), str(shifted_rom)])
        run([sys.executable, "tools/n64crc.py", str(shifted_rom)])

        base_symbols = load_nm_symbols(base_path)
        shifted_symbols = load_nm_symbols(shifted_elf)
        main_start = base_symbols["main_ROM_START"]
        checked_symbols = 0
        address_map: dict[int, int] = {}
        for name, base_value in base_symbols.items():
            if not name.endswith(ROM_SYMBOL_SUFFIXES) or name not in shifted_symbols:
                continue
            # entry_ROM_END remains before the inserted gap even though it has
            # the same baseline value as main_ROM_START.
            moves = name == "main_ROM_START" or base_value > main_start
            expected = base_value + (ROM_SHIFT if moves else 0)
            if shifted_symbols[name] != expected:
                raise RuntimeError(
                    f"{name} shifted to 0x{shifted_symbols[name]:X}; expected 0x{expected:X}"
                )
            if moves:
                address_map[base_value] = expected
            checked_symbols += 1
        checked_words = verify_runtime_words(Elf32(base_path), Elf32(shifted_elf), address_map)
        if shifted_rom.stat().st_size != (ROOT / "build/snowboardkids2.z64").stat().st_size + ROM_SHIFT:
            raise RuntimeError("shifted ROM size did not grow by the inserted padding")
        output_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(shifted_rom, output_path)
        print(
            f"ROM-shift test: +0x{ROM_SHIFT:X}, {checked_symbols} boundary symbols and "
            f"{checked_words} runtime table words relocated; wrote {output_path}"
        )


def test_vram_shift() -> None:
    with tempfile.TemporaryDirectory(prefix="sbk2-vram-shift-") as temporary:
        temp = Path(temporary)
        obj = temp / "fixture.o"
        run([tool("as"), "-G", "0", "-mips3", "-mabi=32", "-o", str(obj), "tools/shiftability/relocation_fixture.s"])

        def link(path: Path, data_address: int) -> Elf32:
            run([
                tool("ld"), "-e", "shiftabilityFixtureLoad", "-Ttext", "0x80100000",
                "--section-start=.rodata=0x80100100",
                f"--section-start=.data=0x{data_address:08X}", str(obj), "-o", str(path),
            ])
            return Elf32(path)

        base = link(temp / "fixture-base.elf", 0x80100200)
        shifted = link(temp / "fixture-shifted.elf", 0x80100220)
        if shifted.symbols["shiftabilityFixtureData"].value - base.symbols["shiftabilityFixtureData"].value != 0x20:
            raise RuntimeError("fixture data symbol did not move by 0x20")
        base_pointer = struct.unpack(">I", base.bytes_at_symbol("shiftabilityFixturePointer", 4))[0]
        shifted_pointer = struct.unpack(">I", shifted.bytes_at_symbol("shiftabilityFixturePointer", 4))[0]
        if shifted_pointer - base_pointer != 0x20:
            raise RuntimeError("R_MIPS_32 fixture pointer did not follow the shifted data symbol")
        if base.bytes_at_symbol("shiftabilityFixtureLoad", 16) == shifted.bytes_at_symbol("shiftabilityFixtureLoad", 16):
            raise RuntimeError("HI16/LO16 fixture code did not follow the shifted data symbol")
        print("internal-VRAM-shift test: R_MIPS_32 and HI16/LO16 references followed a +0x20 data move")


def audit() -> None:
    boundaries = load_rom_boundaries()
    audit_linked_objects(boundaries)
    audit_vram_literals()
    audit_binary_assets(boundaries)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("audit", "rom-shift", "vram-shift", "all"))
    parser.add_argument(
        "--shifted-rom",
        type=Path,
        default=ROOT / "build/snowboardkids2-shifted.z64",
        help="output path for the deliberately shifted ROM",
    )
    args = parser.parse_args()
    try:
        if args.command in ("audit", "all"):
            audit()
        if args.command in ("rom-shift", "all"):
            test_rom_shift(args.shifted_rom.resolve())
        if args.command in ("vram-shift", "all"):
            test_vram_shift()
    except (KeyError, OSError, RuntimeError, subprocess.CalledProcessError, ValueError) as error:
        print(f"shiftability check failed: {error}", file=sys.stderr)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
