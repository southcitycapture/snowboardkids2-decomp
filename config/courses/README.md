# Course definitions

Each YAML file in this directory is the authoritative definition of one stock
course. Course identity, assets, environment, preview metadata, CPU setup, and
rewards are kept together instead of being maintained as parallel C arrays.

`tools/generate_course_definitions.py` validates these files and emits two
representations under `build/include/generated/course_definitions`:

- Match-preserving initializer fragments included by the original tables.
  Their symbol names, types, order, and linker placement do not change.
- `recomp_course_definitions.c`, a complete recomp-only registry source using
  the public `CourseDefinition` model. A recomp build adds this generated file
  to its sources; the matching build deliberately does not compile it.

The generated files are build products and must not be edited. Run
`./tools/build-and-verify.sh` after changing a definition; an accepted change
must still produce the exact target ROM.

## Field notes

- `legacy_id` controls the element's position in every stock course table and
  must cover 0 through 15 exactly once.
- Asset symbols name their existing ROM start/end symbols. Compressed assets
  also carry their decompressed size.
- `render` names the sky display-list record, fog display-list record, and
  per-course display-list table by role. These are typed `DisplayLists`
  records rather than an ordered list of anonymous symbols.
- `scene_animation.legacy_slot` preserves the separate packed animation table.
  Multiple courses may reference the same slot only when they reference the
  same asset, as X Cross and Training do.
- `overlay` and `scene_animation` may be null.
- `behavior` identifies course-specific code to recomp hooks. Behavior remains
  ordinary matched C and is not generated from YAML.
- Training deliberately reuses X Cross's render records as well as its shared
  course assets.
