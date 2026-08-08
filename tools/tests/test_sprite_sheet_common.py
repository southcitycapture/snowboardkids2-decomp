import struct
import unittest

from tools.sprite_sheet_common import palette_specs_for_frames, parse_frame_entry


class ParseFrameEntryTests(unittest.TestCase):
    def frame_data(self, format_index: int, width: int = 7, height: int = 3) -> bytes:
        return b"\0" * 8 + struct.pack(">I6H", 0x100, 0, width, height, 0, format_index, 0)

    def test_ci4_frame_size_rounds_up(self):
        frame = parse_frame_entry(self.frame_data(0), 0)
        self.assertEqual(frame["format"], "ci4")
        self.assertEqual(frame["size"], 11)
        self.assertEqual(frame["palette_colors"], 16)

    def test_ci8_frame_size(self):
        frame = parse_frame_entry(self.frame_data(1), 0)
        self.assertEqual(frame["format"], "ci8")
        self.assertEqual(frame["size"], 21)
        self.assertEqual(frame["palette_colors"], 256)

    def test_rejects_invalid_dimensions(self):
        with self.assertRaisesRegex(ValueError, "invalid dimensions"):
            parse_frame_entry(self.frame_data(0, width=0), 0)

    def test_rejects_unknown_format(self):
        with self.assertRaisesRegex(ValueError, "unsupported sprite format"):
            parse_frame_entry(self.frame_data(2), 0)


class PaletteLayoutTests(unittest.TestCase):
    def test_ci4_exposes_every_palette_slot(self):
        frames = [{"index": 0, "format": "ci4", "palette_index": 1}]
        self.assertEqual(palette_specs_for_frames(frames, 3), {0: 16, 1: 16, 2: 16})

    def test_ci8_uses_sixteen_slot_palettes(self):
        frames = [{"index": 0, "format": "ci8", "palette_index": 16}]
        self.assertEqual(palette_specs_for_frames(frames, 32), {0: 256, 16: 256})

    def test_mixed_layout_uses_referenced_palette_starts(self):
        frames = [
            {"index": 0, "format": "ci4", "palette_index": 0},
            {"index": 1, "format": "ci8", "palette_index": 1},
        ]
        self.assertEqual(palette_specs_for_frames(frames, 17), {0: 16, 1: 256})

    def test_rejects_partial_ci8_palette(self):
        frames = [{"index": 0, "format": "ci8", "palette_index": 0}]
        with self.assertRaisesRegex(ValueError, "partial CI8 palette"):
            palette_specs_for_frames(frames, 15)

    def test_rejects_palette_reference_past_region(self):
        frames = [
            {"index": 0, "format": "ci4", "palette_index": 0},
            {"index": 1, "format": "ci8", "palette_index": 16},
        ]
        with self.assertRaisesRegex(ValueError, "invalid palette reference"):
            palette_specs_for_frames(frames, 17)

    def test_rejects_conflicting_mixed_palette(self):
        frames = [
            {"index": 0, "format": "ci8", "palette_index": 0},
            {"index": 1, "format": "ci4", "palette_index": 1},
        ]
        with self.assertRaisesRegex(ValueError, "overlapping palettes"):
            palette_specs_for_frames(frames, 16)


if __name__ == "__main__":
    unittest.main()
