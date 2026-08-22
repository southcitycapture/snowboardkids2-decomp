#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
from pathlib import Path

import yaml


COURSE_COUNT = 16
COURSE_KINDS = {"standard", "boss", "speed_cross", "shot_cross", "x_cross", "training"}
REQUIRED_FIELDS = {"key", "enum", "legacy_id", "kind", "behavior"}


def parse_int(value: object) -> int:
    return int(value, 0) if isinstance(value, str) else int(value)


def c_int(value: object) -> str:
    number = parse_int(value)
    return f"-0x{-number:X}" if number < 0 else f"0x{number:X}"


def load_courses(definitions_dir: Path) -> list[dict]:
    courses = []
    for path in sorted(definitions_dir.glob("*.yaml")):
        with path.open("r", encoding="utf-8") as stream:
            course = yaml.safe_load(stream)
        if not isinstance(course, dict):
            raise ValueError(f"{path}: course definition must be a mapping")
        missing_fields = REQUIRED_FIELDS - course.keys()
        if missing_fields:
            raise ValueError(f"{path}: missing fields: {', '.join(sorted(missing_fields))}")
        course["_path"] = path
        courses.append(course)

    if len(courses) != COURSE_COUNT:
        raise ValueError(f"expected {COURSE_COUNT} stock courses, found {len(courses)}")

    keys = [str(course["key"]) for course in courses]
    ids = [parse_int(course["legacy_id"]) for course in courses]
    if len(set(keys)) != len(keys):
        raise ValueError("course keys must be unique")
    if sorted(ids) != list(range(COURSE_COUNT)):
        raise ValueError(f"legacy_id values must be contiguous from 0 to {COURSE_COUNT - 1}")

    required_sections = {"assets", "environment", "render", "preview", "race"}
    required_assets = {
        "display_lists",
        "model_resources",
        "track_mesh",
        "texture_table",
        "gold_coins",
        "item_boxes",
        "scene_animation",
    }
    scene_slots: dict[int, str] = {}
    for course in courses:
        path = course["_path"]
        missing = required_sections - course.keys()
        if missing:
            raise ValueError(f"{path}: missing sections: {', '.join(sorted(missing))}")
        missing_assets = required_assets - course["assets"].keys()
        if missing_assets:
            raise ValueError(f"{path}: missing assets: {', '.join(sorted(missing_assets))}")
        if course["kind"] not in COURSE_KINDS:
            raise ValueError(f"{path}: unsupported course kind {course['kind']!r}")
        if re.fullmatch(r"[a-z][a-z0-9_]*", str(course["key"])) is None:
            raise ValueError(f"{path}: key must be a lowercase identifier")
        if re.fullmatch(r"[A-Z][A-Z0-9_]*", str(course["enum"])) is None:
            raise ValueError(f"{path}: enum must be an uppercase C identifier")
        for asset_name, asset in course["assets"].items():
            if asset_name == "scene_animation" and asset is None:
                continue
            if not isinstance(asset, dict) or "symbol" not in asset or "decompressed_size" not in asset:
                raise ValueError(f"{path}: asset {asset_name} requires symbol and decompressed_size")
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(asset["symbol"])) is None:
                raise ValueError(f"{path}: asset {asset_name} has an invalid symbol")
        if len(course["environment"]["spawn_position"]) != 3:
            raise ValueError(f"{path}: spawn_position must contain three values")
        if len(course["environment"]["light_colors"]) != 8:
            raise ValueError(f"{path}: light_colors must contain eight values")
        if len(course["environment"]["fog_colors"]) != 8:
            raise ValueError(f"{path}: fog_colors must contain eight values")
        required_render_fields = {"sky_display_lists", "fog_display_lists", "display_list_table"}
        if set(course["render"]) != required_render_fields:
            raise ValueError(f"{path}: render must define sky, fog, and course display lists")
        for field, symbol in course["render"].items():
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(symbol)) is None:
                raise ValueError(f"{path}: render field {field} has an invalid symbol")
        if len(course["race"]["cpu_characters"]) != 4:
            raise ValueError(f"{path}: cpu_characters must contain four values")
        if len(course["race"]["cpu_snowboards"]) != 6:
            raise ValueError(f"{path}: cpu_snowboards must contain six entries")
        if any(len(entry) != 4 for entry in course["race"]["cpu_snowboards"]):
            raise ValueError(f"{path}: each cpu_snowboards entry must contain four values")
        if len(course["race"]["rewards"]) != 3:
            raise ValueError(f"{path}: rewards must contain first, second, and third place values")
        scene = course["assets"].get("scene_animation")
        if scene is not None:
            slot = parse_int(scene["legacy_slot"])
            symbol = str(scene["symbol"])
            if slot in scene_slots and scene_slots[slot] != symbol:
                raise ValueError(f"{path}: scene animation slot {slot} has conflicting assets")
            scene_slots[slot] = symbol

    if sorted(scene_slots) != list(range(len(scene_slots))):
        raise ValueError("scene animation legacy slots must be unique and contiguous")
    return sorted(courses, key=lambda course: parse_int(course["legacy_id"]))


def write_fragment(out_dir: Path, name: str, lines: list[str]) -> None:
    path = out_dir / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def asset_initializer(asset: dict, compressed: bool = True) -> str:
    symbol = asset["symbol"]
    if compressed:
        return (
            f"    {{ (void *)&{symbol}_ROM_START, (void *)&{symbol}_ROM_END, "
            f"{c_int(asset['decompressed_size'])} }},"
        )
    return f"    {{ (void *)&{symbol}_ROM_START, (void *)&{symbol}_ROM_END }},"


def render_level_config(course: dict) -> list[str]:
    env = course["environment"]
    spawn = ", ".join(c_int(value) for value in env["spawn_position"])
    light = ", ".join(c_int(value) for value in env["light_colors"])
    fog = ", ".join(c_int(value) for value in env["fog_colors"])
    return [
        "    {",
        f"        .shortcutPosX = {c_int(env['shortcut_position'][0])},",
        f"        .shortcutPosZ = {c_int(env['shortcut_position'][1])},",
        f"        .yawOffset = {c_int(env['yaw_offset'])},",
        "        .padding = 0,",
        f"        .spawnPos = {{ {spawn} }},",
        f"        .lightColors = {{ {light} }},",
        f"        .fogColors = {{ {fog} }},",
        f"        .musicTrack = {c_int(env['music_track'])},",
        "        .padding2 = { 0 },",
        "    },",
    ]


def render_overlay(course: dict) -> list[str]:
    overlay = course.get("overlay")
    if overlay is None:
        return ["    { 0 },"]
    symbol = overlay["symbol"]
    data_end = overlay.get("data_end", "DATA_END")
    return [
        "    {",
        f"        .romStart = (void *)&{symbol}_ROM_START,",
        f"        .romEnd = (void *)&{symbol}_ROM_END,",
        f"        .ramStart = &{symbol}_VRAM,",
        f"        .icacheStart = &{symbol}_TEXT_START,",
        f"        .icacheEnd = &{symbol}_TEXT_END,",
        f"        .dcacheStart = &{symbol}_DATA_START,",
        f"        .dcacheEnd = &{symbol}_{data_end},",
        f"        .bssStart = &{symbol}_BSS_START,",
        f"        .bssEnd = &{symbol}_BSS_END,",
        "    },",
    ]


def enum_name(course: dict) -> str:
    return str(course["enum"])


def generate_matched(courses: list[dict], out_dir: Path) -> None:
    write_fragment(
        out_dir,
        "course_ids.inc",
        [f"    {enum_name(course)} = {course['legacy_id']}," for course in courses],
    )

    level_lines: list[str] = []
    overlay_lines: list[str] = []
    for course in courses:
        level_lines.extend(render_level_config(course))
        overlay_lines.extend(render_overlay(course))
    write_fragment(out_dir, "level_configs.inc", level_lines)
    write_fragment(out_dir, "overlays.inc", overlay_lines)

    asset_fields = {
        "track_mesh_assets.inc": ("track_mesh", True),
        "texture_table_assets.inc": ("texture_table", True),
        "display_list_assets.inc": ("display_lists", False),
        "model_resource_assets.inc": ("model_resources", True),
        "gold_coin_assets.inc": ("gold_coins", True),
        "item_box_assets.inc": ("item_boxes", True),
    }
    for filename, (field, compressed) in asset_fields.items():
        write_fragment(
            out_dir,
            filename,
            [asset_initializer(course["assets"][field], compressed) for course in courses],
        )

    scenes_by_slot = {}
    for course in courses:
        scene = course["assets"].get("scene_animation")
        if scene is not None:
            scenes_by_slot[parse_int(scene["legacy_slot"])] = scene
    scenes = [scenes_by_slot[slot] for slot in sorted(scenes_by_slot)]
    write_fragment(out_dir, "scene_animation_assets.inc", [asset_initializer(scene) for scene in scenes])

    write_fragment(
        out_dir,
        "course_sky_display_lists.inc",
        [f"    {course['render']['sky_display_lists']}," for course in courses],
    )
    write_fragment(
        out_dir,
        "course_fog_display_lists.inc",
        [f"    {course['render']['fog_display_lists']}," for course in courses],
    )
    write_fragment(
        out_dir,
        "course_display_list_tables.inc",
        [
            f"    (LevelDisplayLists *){course['render']['display_list_table']},"
            for course in courses
        ],
    )

    scalar_tables = {
        "level_worlds.inc": lambda course: course["preview"]["world"],
        "preview_durations.inc": lambda course: course["preview"]["duration"],
        "preview_start_waypoints.inc": lambda course: course["preview"]["start_waypoint"],
        "cpu_board_models.inc": lambda course: course["race"]["cpu_board_model"],
        "expert_cpu_snowboards.inc": lambda course: course["race"]["expert_snowboard"],
        "first_place_rewards.inc": lambda course: course["race"]["rewards"][0],
        "second_place_rewards.inc": lambda course: course["race"]["rewards"][1],
        "third_place_rewards.inc": lambda course: course["race"]["rewards"][2],
    }
    for filename, getter in scalar_tables.items():
        write_fragment(out_dir, filename, [f"    {c_int(getter(course))}," for course in courses])

    write_fragment(
        out_dir,
        "cpu_characters.inc",
        [
            "    { " + ", ".join(c_int(value) for value in course["race"]["cpu_characters"]) + " },"
            for course in courses
        ],
    )
    snowboard_lines = []
    for course in courses:
        entries = ["{ " + ", ".join(c_int(value) for value in entry) + " }" for entry in course["race"]["cpu_snowboards"]]
        snowboard_lines.append("    { " + ", ".join(entries) + " },")
    write_fragment(out_dir, "cpu_snowboards.inc", snowboard_lines)


def recomp_asset(asset: dict | None, compression: str) -> str:
    if asset is None:
        return "{ NULL, NULL, 0, COURSE_ASSET_NONE }"
    symbol = asset["symbol"]
    size = c_int(asset.get("decompressed_size", 0))
    return f"{{ &{symbol}_ROM_START, &{symbol}_ROM_END, {size}, {compression} }}"


def generate_recomp(courses: list[dict], out_dir: Path) -> None:
    lines = ["/* Generated CourseDefinition entries. Include inside an array initializer. */"]
    kinds = {
        "standard": "COURSE_KIND_STANDARD",
        "boss": "COURSE_KIND_BOSS",
        "speed_cross": "COURSE_KIND_SPEED_CROSS",
        "shot_cross": "COURSE_KIND_SHOT_CROSS",
        "x_cross": "COURSE_KIND_X_CROSS",
        "training": "COURSE_KIND_TRAINING",
    }
    for course in courses:
        env = course["environment"]
        race = course["race"]
        assets = course["assets"]
        cpu_characters = ", ".join(c_int(value) for value in race["cpu_characters"])
        rewards = ", ".join(c_int(value) for value in race["rewards"])
        cpu_snowboards = ", ".join(
            "{ " + ", ".join(c_int(value) for value in entry) + " }" for entry in race["cpu_snowboards"]
        )
        render = course["render"]
        spawn = ", ".join(c_int(value) for value in env["spawn_position"])
        light = ", ".join(c_int(value) for value in env["light_colors"])
        fog = ", ".join(c_int(value) for value in env["fog_colors"])
        lines.extend(
            [
                "    {",
                "        COURSE_DEFINITION_ABI_VERSION, sizeof(CourseDefinition),",
                f"        \"{course['key']}\", {course['legacy_id']}, {kinds[course['kind']]},",
                f"        \"{course['behavior']}\",",
                "        {",
                f"            {recomp_asset(assets['display_lists'], 'COURSE_ASSET_UNCOMPRESSED')},",
                f"            {recomp_asset(assets['model_resources'], 'COURSE_ASSET_SNO')},",
                f"            {recomp_asset(assets['track_mesh'], 'COURSE_ASSET_SNO')},",
                f"            {recomp_asset(assets['texture_table'], 'COURSE_ASSET_SNO')},",
                f"            {recomp_asset(assets['gold_coins'], 'COURSE_ASSET_SNO')},",
                f"            {recomp_asset(assets['item_boxes'], 'COURSE_ASSET_SNO')},",
                f"            {recomp_asset(assets.get('scene_animation'), 'COURSE_ASSET_SNO')},",
                "        },",
                "        {",
                f"            .shortcutPosX = {c_int(env['shortcut_position'][0])},",
                f"            .shortcutPosZ = {c_int(env['shortcut_position'][1])},",
                f"            .yawOffset = {c_int(env['yaw_offset'])},",
                "            .padding = 0,",
                f"            .spawnPos = {{ {spawn} }},",
                f"            .lightColors = {{ {light} }},",
                f"            .fogColors = {{ {fog} }},",
                f"            .musicTrack = {c_int(env['music_track'])},",
                "            .padding2 = { 0 },",
                "        },",
                "        {",
                f"            {render['sky_display_lists']},",
                f"            {render['fog_display_lists']},",
                f"            (const LevelDisplayLists *){render['display_list_table']},",
                "        },",
                f"        {{ {c_int(course['preview']['world'])}, {c_int(course['preview']['duration'])}, {c_int(course['preview']['start_waypoint'])} }},",
                f"        {{ {{ {cpu_characters} }}, {c_int(race['cpu_board_model'])}, {c_int(race['expert_snowboard'])}, {{ {rewards} }}, {{ {cpu_snowboards} }} }},",
                f"        \"{course.get('overlay', {}).get('symbol', '') if course.get('overlay') else ''}\",",
                "        0, { 0 },",
                "    },",
            ]
        )
    write_fragment(out_dir, "recomp_course_definitions.inc", lines)

    assets = sorted(
        {
            asset["symbol"]
            for course in courses
            for asset in course["assets"].values()
            if asset is not None
        }
    )
    overlays = sorted(
        course["overlay"]["symbol"] for course in courses if course.get("overlay") is not None
    )
    source_lines = [
        "/* Auto-generated recomp-only course registry. */",
        '#include "assets.h"',
        '#include "race/course_definition.h"',
        "",
    ]
    source_lines.extend(f"USE_ASSET({symbol});" for symbol in assets)
    source_lines.append("")
    source_lines.extend(f"USE_OVERLAY({symbol});" for symbol in overlays)
    source_lines.extend(
        [
            "",
            "const CourseDefinition gBuiltinCourseDefinitions[] = {",
            '#include "generated/course_definitions/recomp_course_definitions.inc"',
            "};",
            "",
            f"const s32 gBuiltinCourseDefinitionCount = {len(courses)};",
        ]
    )
    write_fragment(out_dir, "recomp_course_definitions.c", source_lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate match-preserving course data projections.")
    parser.add_argument("--definitions", type=Path, default=Path("config/courses"))
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    courses = load_courses(args.definitions)
    generate_matched(courses, args.out)
    generate_recomp(courses, args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
