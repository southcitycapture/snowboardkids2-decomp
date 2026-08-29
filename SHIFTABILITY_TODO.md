# Shiftability TODO

The project is mostly able to relocate ROM content because the linker advances
`__romPos` and runtime asset references generally use linker-generated
`*_ROM_START` and `*_ROM_END` symbols. The items below prevent calling the
project fully shiftable.

This checklist distinguishes ROM shiftability from VRAM layout constraints.
Fixed overlay load addresses are valid by design, but references within and
between those regions must still be symbolic.

## Definite blockers

- [x] Replace the hardcoded `0x001FD6B0` in
      `src/ui/level_preview_3d.c` with `&KEYL_DISPLAY_LIST_ROM_END`.
  - The value is the current end of `KEYL_DISPLAY_LIST` and start of
    `NEZU_DISPLAY_LIST`.
  - The compiled object contains the literal value without an `R_MIPS_32`
    relocation, so it will not follow a ROM layout change.
  - Verify that the replacement still produces a matching ROM before testing
    a shifted layout.

- [x] Eliminate the game-data addresses hardcoded in
      `linker_scripts/data_field_syms.ld`.
  - Express each sub-symbol as a base symbol plus a field or array offset, or
    expose the containing C object/structure and reference its fields directly.
  - At minimum, the actively referenced `D_8008DDE6_8E9E6` and
    `D_8008F0C6_8FCC6` must become relocatable.
  - Audit the remaining entries even if they currently have no C reference;
    they should not silently become dependencies later.
  - Preserve proper structure types rather than replacing these references
    with pointer arithmetic or `void *` casts.
  - Unreferenced absolute aliases were removed. The two live overlapping
    views are now relative to `playerNumberPositions` and
    `D_8008F0B0_8FCB0`, respectively.

- [x] Replace the absolute cutscene function definitions in
      `linker_scripts/game_syms.ld`:
  - `renderCutsceneEditorText = 0x800BB404`
  - `renderCutsceneSlotMenuItem = 0x800BB2F4`
  - `setupCutsceneCommandLayout = 0x800BB47C`
  - These are unresolved references in the compiled cutscene objects and are
    currently satisfied only by absolute linker assignments.
  - Identify the code that owns these functions, split/decompile it as needed,
    and let the linker resolve normal function symbols. If the addresses refer
    into an intentionally opaque binary, define them relative to that binary's
    load symbol instead of as absolute VRAM values.
  - The former `UNUSED_CODE_BLOCK` is now the `cutscene_editor_helpers` code
    overlay at `cutscene_BSS_END`. Its generated assembly owns all three
    function labels, so internal layout changes move their symbols normally.

## Layout constraints to document or enforce

- [ ] Add linker assertions for the resident main region.
  - `main_VRAM_END` currently meets the `rand` overlay at `0x800AFF30`.
  - Assert that resident code, data, and BSS do not grow into the overlay load
    region.
  - Give the assertion a clear error explaining that the resident image has
    exceeded its RAM budget.

- [ ] Add linker assertions for every fixed-VRAM overlay.
  - Important load addresses include `0x800B00C0` and `0x800BB2B0`.
  - Check each overlay against the next live RAM region or the largest allowed
    overlay size.
  - Sharing a load address between mutually exclusive overlays is expected and
    is not itself a shiftability defect.

- [ ] Document the supported meaning of "shiftable."
  - ROM shiftability: preceding ROM sections may change size and later ROM
    references continue to resolve correctly.
  - Internal VRAM shiftability: symbols within a linked resident image or
    overlay may move while references remain correct.
  - Load-address independence is not currently a goal: overlays and hardware/
    libultra ABI locations legitimately use fixed VRAM addresses.

## Audit remaining raw data

- [ ] Scan linked C and assembly objects for ROM-address literals that have no
      relocation.
  - Compare literal words against all segment starts and ends from
    `snowboardkids2.yaml`.
  - Inspect only objects actually linked into the ROM; extracted reference
    assembly under `asm/data` may contain old numeric values without affecting
    the build.
  - Classify likely matches manually because packed text, fixed-point values,
    display-list commands, and segmented addresses can resemble ROM offsets.

- [ ] Audit binary assets that contain runtime ROM tables.
  - Determine whether any opaque `.bin`/generated asset embeds absolute ROM
    offsets consumed by the game.
  - Convert genuine ROM tables to generated symbolic data or add a packer step
    that patches them from the final linker layout.
  - Do not treat ordinary segmented display-list addresses such as `0x01xxxxxx`
    as cartridge ROM offsets.

- [ ] Keep hardware and libultra ABI constants out of the blocker count.
  - KSEG bases, hardware-register addresses, and OS boot variables such as
    `osTvType` are fixed platform addresses and should remain absolute.
  - Review project-local `0x80xxxxxx` values separately from those platform
    definitions.

## Shiftability verification

- [ ] Add an automated ROM-shift test target.
  - Insert a small aligned padding section early in ROM without changing its
    VRAM footprint.
  - Link a non-matching test ROM and confirm that every later `*_ROM_START/END`
    reference changes by the expected amount.
  - Check known runtime tables in the linked ROM for the shifted values.

- [ ] Add an internal-VRAM-shift test for a safe region or test fixture.
  - Move a symbol within a resident/overlay test region and verify that code
    and data references are relocation-backed.
  - Keep fixed overlay load bases unchanged during this test.

- [ ] Validate each fix in this order:
  1. Run `./tools/build-and-verify.sh` and require the matching ROM checksum.
  2. Run the synthetic ROM-shift test and require all expected relocations.
  3. Run the relevant VRAM-budget assertions.
  4. Smoke-test the shifted ROM in an emulator on asset loading, level
     overlays, and cutscene editor paths affected by the former absolute
     symbols.

## Completion criteria

The decomp can be described as ROM-shiftable when:

- no linked game code/data uses a raw cartridge offset where a linker symbol is
  available;
- all runtime ROM tables are symbolic or generated from the final layout;
- project-local code and data references relocate within their RAM regions;
- fixed overlay boundaries are enforced with clear linker assertions; and
- both the original matching build and a deliberately shifted test build pass.
