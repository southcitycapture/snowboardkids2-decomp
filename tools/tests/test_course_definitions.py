import tempfile
import unittest
from pathlib import Path

import yaml

from tools.generate_course_definitions import generate_matched, generate_recomp, load_courses


ROOT = Path(__file__).resolve().parents[2]
DEFINITIONS = ROOT / "config/courses"


class CourseDefinitionsTest(unittest.TestCase):
    def test_stock_definitions_are_complete_and_ordered(self):
        courses = load_courses(DEFINITIONS)

        self.assertEqual([course["legacy_id"] for course in courses], list(range(16)))
        self.assertEqual(courses[0]["key"], "sunny_mountain")
        self.assertEqual(courses[-1]["key"], "training")

    def test_training_explicitly_reuses_x_cross_assets(self):
        courses = load_courses(DEFINITIONS)
        x_cross = courses[14]
        training = courses[15]

        for field in ("display_lists", "model_resources", "track_mesh", "texture_table", "scene_animation"):
            self.assertEqual(training["assets"][field]["symbol"], x_cross["assets"][field]["symbol"])
        self.assertEqual(training["render"], x_cross["render"])
        self.assertNotEqual(training["assets"]["gold_coins"]["symbol"], x_cross["assets"]["gold_coins"]["symbol"])

    def test_render_records_use_named_fields(self):
        courses = load_courses(DEFINITIONS)

        for course in courses:
            self.assertEqual(
                set(course["render"]),
                {"sky_display_lists", "fog_display_lists", "display_list_table"},
            )
            self.assertNotIn("sky_display_lists", course["environment"])

    def test_environment_uses_lift_and_course_start_fields(self):
        courses = load_courses(DEFINITIONS)

        for course in courses:
            environment = course["environment"]
            self.assertIn("lift_entry_position", environment)
            self.assertIn("course_start_position", environment)
            self.assertIn("lift_entry_yaw_offset", environment)
            self.assertNotIn("shortcut_position", environment)
            self.assertNotIn("spawn_position", environment)
            self.assertNotIn("yaw_offset", environment)

        with tempfile.TemporaryDirectory() as output:
            generate_matched(courses, Path(output))
            generated = (Path(output) / "level_configs.inc").read_text()

        self.assertIn(".liftEntryPosX", generated)
        self.assertIn(".courseStartPos", generated)
        self.assertNotIn("shortcut", generated.lower())

    def test_generation_is_deterministic(self):
        courses = load_courses(DEFINITIONS)
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            for output in (Path(first), Path(second)):
                generate_matched(courses, output)
                generate_recomp(courses, output)
            first_files = {path.name: path.read_text() for path in Path(first).iterdir()}
            second_files = {path.name: path.read_text() for path in Path(second).iterdir()}

        self.assertEqual(first_files, second_files)
        self.assertIn("recomp_course_definitions.inc", first_files)
        self.assertIn("recomp_course_definitions.c", first_files)
        self.assertIn("course_sky_display_lists.inc", first_files)
        self.assertIn("course_fog_display_lists.inc", first_files)
        self.assertIn("course_display_list_tables.inc", first_files)
        self.assertNotIn("course_sky_display_lists_1.inc", first_files)
        self.assertEqual(len(first_files), 25)

    def test_duplicate_legacy_id_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            temporary_path = Path(temporary)
            for source in DEFINITIONS.glob("*.yaml"):
                data = yaml.safe_load(source.read_text())
                if data["legacy_id"] == 15:
                    data["legacy_id"] = 14
                (temporary_path / source.name).write_text(yaml.safe_dump(data, sort_keys=False))

            with self.assertRaisesRegex(ValueError, "legacy_id values"):
                load_courses(temporary_path)

    def test_legacy_lift_environment_keys_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            temporary_path = Path(temporary)
            for source in DEFINITIONS.glob("*.yaml"):
                data = yaml.safe_load(source.read_text())
                if data["legacy_id"] == 0:
                    environment = data["environment"]
                    environment["shortcut_position"] = environment.pop("lift_entry_position")
                (temporary_path / source.name).write_text(yaml.safe_dump(data, sort_keys=False))

            with self.assertRaisesRegex(ValueError, "environment must define"):
                load_courses(temporary_path)


if __name__ == "__main__":
    unittest.main()
