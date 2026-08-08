from __future__ import annotations

import struct
from pathlib import Path

from tools.course_assets_common import parse_int
from tools.modelpayload_common import read_palette_s, read_texture_png


HEADER_SIZE = 8
FRAME_ENTRY_SIZE = 0x10
PALETTE_SLOT_SIZE = 0x20

FORMAT_INFO = {
    0: {"format": "ci4", "colors": 16, "palette_slots": 1},
    1: {"format": "ci8", "colors": 256, "palette_slots": 16},
}


def palette_size(colors: int) -> int:
    if colors not in (16, 256):
        raise ValueError(f"unsupported sprite palette size {colors}")
    return colors * 2


def palette_specs_for_frames(frames: list[dict], palette_slot_count: int) -> dict[int, int]:
    if palette_slot_count <= 0:
        raise ValueError("sprite sheet has no palette data")

    formats = {frame["format"] for frame in frames}
    if formats == {"ci4"}:
        specs = {index: 16 for index in range(palette_slot_count)}
    elif formats == {"ci8"}:
        if palette_slot_count % 16 != 0:
            raise ValueError("sprite sheet has a partial CI8 palette")
        specs = {index: 256 for index in range(0, palette_slot_count, 16)}
    elif formats <= {"ci4", "ci8"}:
        specs = {frame["palette_index"]: 16 if frame["format"] == "ci4" else 256 for frame in frames}
    else:
        raise ValueError(f"sprite sheet uses unsupported formats {sorted(formats)}")

    ranges: list[tuple[int, int]] = []
    for index, colors in sorted(specs.items()):
        end_index = index + colors // 16
        if index < 0 or end_index > palette_slot_count:
            raise ValueError(f"sprite sheet has an invalid palette reference {index}")
        if ranges and index < ranges[-1][1]:
            raise ValueError(f"sprite sheet has overlapping palettes at index {index}")
        ranges.append((index, end_index))

    for frame in frames:
        expected_colors = 16 if frame["format"] == "ci4" else 256
        if specs.get(frame["palette_index"]) != expected_colors:
            raise ValueError(f"frame {frame.get('index', '?')} has a conflicting palette format")
    return specs


def parse_frame_entry(data: bytes, index: int) -> dict:
    offset = HEADER_SIZE + index * FRAME_ENTRY_SIZE
    texture_offset, palette_index, width, height, palette_table_index, format_index, pad_0e = struct.unpack_from(
        ">I6H", data, offset
    )
    if palette_table_index != 0 or format_index not in FORMAT_INFO:
        raise ValueError(
            f"frame 0x{index:X} uses unsupported sprite format "
            f"palette_table_index={palette_table_index}, format_index={format_index}"
        )
    if width == 0 or height == 0:
        raise ValueError(f"frame 0x{index:X} has invalid dimensions {width}x{height}")
    format_info = FORMAT_INFO[format_index]
    pixel_count = width * height
    return {
        "texture_offset": texture_offset,
        "palette_index": palette_index,
        "width": width,
        "height": height,
        "palette_table_index": palette_table_index,
        "format_index": format_index,
        "pad_0e": pad_0e,
        "format": format_info["format"],
        "palette_colors": format_info["colors"],
        "palette_slots": format_info["palette_slots"],
        "size": (pixel_count + 1) // 2 if format_index == 0 else pixel_count,
    }


def build_sprite_sheet(manifest_path: Path, manifest: dict) -> bytes:
    size = parse_int(manifest["decompressed_size"])
    frames = manifest["frames"]
    palettes = manifest["palettes"]
    palette_base = HEADER_SIZE + len(frames) * FRAME_ENTRY_SIZE
    out = bytearray(size)
    written = bytearray(size)

    def write_range(start: int, data: bytes, description: str, allow_identical: bool = False) -> None:
        end = start + len(data)
        if start < 0 or end > size:
            raise ValueError(f"{manifest_path}: {description} range 0x{start:X}-0x{end:X} exceeds payload size")
        if any(written[start:end]):
            if allow_identical and out[start:end] == data:
                return
            raise ValueError(f"{manifest_path}: {description} overlaps an earlier range")
        out[start:end] = data
        written[start:end] = b"\x01" * len(data)

    header = parse_int(manifest.get("texture_base", 0)).to_bytes(4, "big") + len(frames).to_bytes(4, "big")
    write_range(0, header, "header")

    palette_values: dict[str, list[int]] = {}
    palette_indices: dict[int, str] = {}
    palette_names: set[str] = set()
    palette_ranges: list[tuple[int, int]] = []
    for palette in palettes:
        index = parse_int(palette["index"])
        if index in palette_indices:
            raise ValueError(f"{manifest_path}: duplicate palette index {index}")
        name = str(palette["name"])
        if name in palette_names:
            raise ValueError(f"{manifest_path}: duplicate palette name {name!r}")
        palette_names.add(name)
        values = read_palette_s(manifest_path.parent / palette["path"])
        colors = parse_int(palette["colors"])
        if colors not in (16, 256):
            raise ValueError(f"{palette['path']} declares unsupported palette size {colors}")
        if len(values) != colors:
            raise ValueError(f"{palette['path']} contains {len(values)} colors, expected {colors}")
        expected_offset = palette_base + index * PALETTE_SLOT_SIZE
        offset = parse_int(palette["offset"])
        if offset != expected_offset:
            raise ValueError(
                f"{palette['path']} has offset 0x{offset:X}, expected contiguous palette offset 0x{expected_offset:X}"
            )
        raw = b"".join(value.to_bytes(2, "big") for value in values)
        palette_end = offset + len(raw)
        for other_start, other_end in palette_ranges:
            if offset < other_end and other_start < palette_end:
                raise ValueError(f"{palette['path']} overlaps another palette")
        palette_ranges.append((offset, palette_end))
        write_range(offset, raw, f"palette {index}")
        palette_values[name] = values
        palette_indices[index] = name

    for index, frame in enumerate(frames):
        manifest_index = parse_int(frame.get("index", index))
        if manifest_index != index:
            raise ValueError(f"{manifest_path}: frame {index} declares index {manifest_index}")
        palette_index = parse_int(frame["palette_index"])
        if palette_index not in palette_indices:
            raise ValueError(f"{frame['name']} references missing palette index {palette_index}")
        width = parse_int(frame["width"])
        height = parse_int(frame["height"])
        if width <= 0 or height <= 0:
            raise ValueError(f"{frame['name']} has invalid dimensions {width}x{height}")
        if frame.get("format") not in ("ci4", "ci8"):
            raise ValueError(f"{frame['name']} uses unsupported texture format {frame.get('format')!r}")
        palette_table_index = parse_int(frame.get("palette_table_index", 0))
        format_index = parse_int(frame.get("format_index", 0))
        if palette_table_index != 0 or format_index not in FORMAT_INFO:
            raise ValueError(
                f"{frame['name']} uses unsupported sprite format "
                f"palette_table_index={palette_table_index}, format_index={format_index}"
            )
        format_info = FORMAT_INFO[format_index]
        if frame["format"] != format_info["format"]:
            raise ValueError(
                f"{frame['name']} format {frame['format']!r} does not match format_index {format_index}"
            )
        palette_name = palette_indices[palette_index]
        palette = next(item for item in palettes if item["name"] == palette_name)
        palette_colors = parse_int(palette["colors"])
        if palette_colors != format_info["colors"]:
            raise ValueError(
                f"{frame['name']} requires {format_info['colors']} colors, but palette {palette_index} "
                f"contains {palette_colors}"
            )
        texture_offset = parse_int(frame["texture_offset"])
        texture = read_texture_png(manifest_path.parent / frame["path"], frame, palette_values[palette_name])
        expected_size = parse_int(frame["size"])
        calculated_size = (width * height + 1) // 2 if frame["format"] == "ci4" else width * height
        if expected_size != calculated_size or len(texture) != expected_size:
            raise ValueError(
                f"{frame['path']} rebuilt to 0x{len(texture):X} bytes; "
                f"manifest/calculated sizes are 0x{expected_size:X}/0x{calculated_size:X}"
            )
        entry = struct.pack(
            ">I6H",
            texture_offset,
            palette_index,
            width,
            height,
            palette_table_index,
            format_index,
            parse_int(frame.get("pad_0e", 0)),
        )
        write_range(HEADER_SIZE + index * FRAME_ENTRY_SIZE, entry, f"frame entry {index}")
        write_range(texture_offset, texture, f"frame texture {index}", allow_identical=True)

    for block in manifest.get("raw_blocks", []):
        start = parse_int(block["offset"])
        expected_size = parse_int(block["size"])
        data = (manifest_path.parent / block["path"]).read_bytes()
        if len(data) != expected_size:
            raise ValueError(f"{block['path']} is 0x{len(data):X} bytes, expected 0x{expected_size:X}")
        write_range(start, data, f"raw block {block['name']}")

    if not all(written):
        first = written.index(0)
        raise ValueError(f"{manifest_path}: manifest does not cover decompressed offset 0x{first:X}")
    return bytes(out)
