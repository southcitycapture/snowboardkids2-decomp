# Editable Sprite Sheet Assets

The `sprite_sheet` segment type extracts SNO-compressed `SpriteSheetData` and compatible image-table assets into
indexed CI4 or CI8 PNG files and shared RGBA16 palettes. The generated YAML manifest preserves the runtime frame table,
texture offsets, palette indices, padding, raw gaps, and unused compressed tail needed for byte-identical rebuilding.

All known sprite/image tables use this format. Their manifests and editable sources are under `assets/sprite_sheets/`.
Frames retain stable `frame_XX` names unless their purpose is explicitly identified in code; for example,
`menuUiSprites` gives the board-rating stars descriptive filenames for each mood and resolution.

To rebuild a sheet directly:

```sh
python3 tools/sprite_sheet_pack.py assets/sprite_sheets/<name>.yaml \
    --out /tmp/<name>.sno
```

CI4 palettes contain 16 colors and occupy one 0x20-byte palette slot. CI8 palettes contain 256 colors and occupy 16
slots. Frame palette indices are slot indices, so a CI8 palette can begin at index `0x10`, `0x20`, and so on. Mixed
CI4/CI8 sheets record each referenced palette at its native size; bytes not attributable to a palette are retained as
raw blocks.

PNG dimensions must match the manifest, every pixel color or indexed value must fit the referenced palette, and the
manifest must cover the complete decompressed payload without overlapping ranges. Run the sprite-sheet unit tests and
the matching build after editing a sprite or palette:

```sh
python3 -m unittest tools.tests.test_sprite_sheet_common
./tools/build-and-verify.sh
```
