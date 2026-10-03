"""The title's licenses/ folder: staged from the component table, checked against the files."""

import hashlib
import importlib.util
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


stage = load("stage_notices", "tools/stage-notices.py")
check = load("check_notices", "tools/check-notices.py")


def digest(data):
    return hashlib.sha256(data).hexdigest()


class Notices(unittest.TestCase):
    """A miniature repository, title and table, so no build is needed."""

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        base = Path(self.directory.name)
        self.repo = base / "repo"
        self.title = base / "title"
        (self.repo / "build/cores/demo").mkdir(parents=True)
        (self.repo / "src").mkdir()
        (self.repo / "LICENSE").write_text("GNU GENERAL PUBLIC LICENSE\n")
        (self.repo / "src/demo-license.txt").write_text("Demo licence, verbatim.\n")
        (self.repo / "tools").mkdir()
        (self.repo / "tools/fetch.sh").write_text('revision="abc123"\n')
        subprocess.run(["git", "init", "-q", str(self.repo)], check=True)
        subprocess.run(["git", "-C", str(self.repo), "add", "-A"], check=True)
        subprocess.run(["git", "-C", str(self.repo), "-c", "user.name=t", "-c", "user.email=t@t",
                        "commit", "-qm", "fixture"], check=True)
        (self.title / "cores").mkdir(parents=True)
        (self.title / "eboot.bin").write_bytes(b"title")
        self.core = b"core bytes"
        (self.title / "cores/demo_libretro.so").write_bytes(self.core)
        self.report(self.core)
        self.table = self.repo / "components.json"
        self.table.write_text(json.dumps({"components": [
            {"id": "port", "name": "Port", "kind": "code", "always": True,
             "artifacts": ["eboot.bin"], "licence": "GPL-3.0-or-later",
             "texts": [{"from": "LICENSE", "to": "GPL-3.0.txt"}],
             "source": {"kind": "git", "path": ".", "remote": "https://example.org/port"}},
            {"id": "upstream", "name": "Upstream", "kind": "code", "always": True,
             "artifacts": ["eboot.bin"], "licence": "GPL-3.0-or-later",
             "texts": [{"from": "LICENSE", "to": "COPYING"}],
             "source": {"kind": "pin", "file": "tools/fetch.sh", "var": "revision",
                        "remote": "https://example.org/upstream"}},
            {"id": "demo", "name": "Demo core", "kind": "code", "noncommercial": True,
             "artifacts": ["cores/demo_libretro.so"], "licence": "LicenseRef-Demo",
             "texts": [{"from": "src/demo-license.txt", "to": "license.txt", "verbatim": True}],
             "source": {"kind": "core", "build": "demo", "remote": "https://example.org/demo"}},
            {"id": "absent", "name": "A core this build does not have", "kind": "code",
             "artifacts": ["cores/absent_libretro.so"], "licence": "MIT", "texts": [],
             "source": {"kind": "fixed", "revision": "1"}},
        ]}))

    def tearDown(self):
        self.directory.cleanup()

    def report(self, data):
        (self.repo / "build/cores/demo/build.json").write_text(json.dumps(
            {"sha256": digest(data), "source_revision": "f00d"}))

    def run_stage(self):
        return stage.stage(self.title, "radv", "v-test", {}, root=self.repo, table=self.table)

    def test_staged_folder_passes_the_check(self):
        staged = self.run_stage()
        self.assertEqual([c["id"] for c in staged], ["port", "upstream", "demo"])
        self.assertEqual(check.check(self.title), [])
        record = json.loads((self.title / "licenses/components.json").read_text())
        demo = next(c for c in record["components"] if c["id"] == "demo")
        self.assertEqual(demo["source"]["revision"], "f00d")
        self.assertEqual(demo["source"]["url"], "https://example.org/demo/tree/f00d")
        self.assertEqual(demo["executables"], {"cores/demo_libretro.so": digest(self.core)})
        upstream = next(c for c in record["components"] if c["id"] == "upstream")
        self.assertEqual(upstream["source"]["revision"], "abc123")
        readme = (self.title / "licenses/README.txt").read_text()
        self.assertIn("Non-commercial terms", readme)
        self.assertIn("Demo core", readme)

    def test_default_title_lookup_ignores_release_zip(self):
        self.run_stage()
        dist = self.repo / "dist"
        dist.mkdir()
        self.title.rename(dist / "PPSA99169")
        (dist / "PPSA99169.zip").write_bytes(b"release archive")
        with patch.object(check, "__file__", str(self.repo / "tools/check-notices.py")):
            self.assertEqual(check.main([]), 0)
            (dist / "PPSA99170").mkdir()
            self.assertEqual(check.main([]), 2)

    def test_a_core_that_is_not_its_build_report_is_refused(self):
        self.report(b"another build")
        with self.assertRaisesRegex(stage.NoticeError, "build.json describes"):
            self.run_stage()
        self.assertFalse((self.title / "licenses").exists(), "a partial licenses/ was left")

    def test_a_missing_licence_text_is_refused(self):
        (self.repo / "src/demo-license.txt").unlink()
        with self.assertRaisesRegex(stage.NoticeError, "not found"):
            self.run_stage()

    def test_an_unlisted_core_fails_the_check(self):
        self.run_stage()
        (self.title / "cores/stray_libretro.so").write_bytes(b"stray")
        problems = check.check(self.title)
        self.assertTrue(any("stray_libretro.so belongs to no listed part" in p for p in problems))

    def test_a_changed_core_or_edited_verbatim_text_fails_the_check(self):
        self.run_stage()
        (self.title / "cores/demo_libretro.so").write_bytes(b"rebuilt")
        (self.title / "licenses/demo/license.txt").write_text("shortened\n")
        problems = check.check(self.title)
        self.assertTrue(any("changed after its notice was staged" in p for p in problems))
        self.assertTrue(any("not the verbatim text" in p for p in problems))

    def test_release_requires_committed_source(self):
        (self.repo / "LICENSE").write_text("edited\n")
        self.run_stage()
        self.assertEqual(check.check(self.title), [])
        problems = check.check(self.title, release=True)
        self.assertEqual(problems, ["port: built from uncommitted source"])

    def test_the_repository_table_names_existing_build_scripts(self):
        table = json.loads((ROOT / "tooling/notices/components.json").read_text())
        ids = [c["id"] for c in table["components"]]
        self.assertEqual(len(ids), len(set(ids)))
        for component in table["components"]:
            self.assertTrue(component["licence"])
            source = component["source"]
            if source["kind"] == "pin":
                self.assertTrue((ROOT / source["file"]).is_file(), source["file"])
            if source["kind"] == "core":
                self.assertTrue(component["artifacts"][0].startswith("cores/"))
            if "LicenseRef" in component["licence"]:
                self.assertTrue(component.get("noncommercial"), component["id"])


if __name__ == "__main__":
    unittest.main()
