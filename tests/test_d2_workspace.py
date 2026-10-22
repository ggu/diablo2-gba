import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import d2_workspace


class WorkspacePreparationTests(unittest.TestCase):
    def test_prepare_orders_assets_and_packers_after_demo_sources_exist(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            excel = root / "extracted/data/GLOBAL/Excel"
            generated = root / "generated"
            generated.mkdir()
            table_paths = [excel / name for name in ("weapons.txt", "armor.txt", "charstats.txt")]
            archive_paths = [root / "source/File00000025.mpq", root / "source/File00000023.mpq"]
            events = []

            def sources_ready():
                excel.mkdir(parents=True)
                for path in table_paths:
                    path.write_text("synthetic table")
                for path in archive_paths:
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(b"synthetic archive")
                events.append("sources")
                return sorted(archive_paths), table_paths

            def run(command, **_kwargs):
                name = Path(command[1]).name
                if name == "d2_import.py":
                    events.append("assets")
                elif name == "d2_items_pack.py":
                    self.assertTrue(all(path.is_file() for path in table_paths))
                    events.append("items")
                elif name == "d2_town_assets.py":
                    self.assertTrue(all(path.is_file() for path in archive_paths))
                    self.assertEqual(command[2:], [arg for path in sorted(archive_paths)
                                                   for arg in ("--mpq", str(path))])
                    events.append("town")
                return None

            with patch.object(d2_workspace, "ensure_checkout", return_value={"cloned": False}),                  patch.object(d2_workspace, "ensure_stormlib", return_value={"built": False}),                  patch.object(d2_workspace, "prepare_demo_sources", side_effect=sources_ready),                  patch.object(d2_workspace, "ASSET_HEADER", generated / "missing-assets.h"),                  patch.object(d2_workspace, "ASSET_MANIFEST", generated / "manifest.json"),                  patch.object(d2_workspace, "EXTRA_MANIFEST", generated / "extras.json"),                  patch.object(d2_workspace, "EXTRA_FILES", ()),                  patch.object(d2_workspace, "_json_file", return_value={}),                  patch.object(d2_workspace, "file_info", return_value={"sha256": "fixture"}),                  patch.object(d2_workspace, "_run", side_effect=run),                  patch.object(d2_workspace, "status", return_value={"prepared": True}):
                result = d2_workspace.prepare()

            self.assertTrue(result["prepared"])
            self.assertEqual(events, ["sources", "assets", "items", "town"])
            self.assertEqual(result["source_archives"], 2)
            self.assertEqual(result["item_tables_extracted"], 3)

    def test_prepare_demo_sources_unpacks_mpqs_and_refreshes_item_tables(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "source"
            excel = root / "extracted/data/GLOBAL/Excel"
            source.mkdir()
            archives = [source / "File00000025.MPQ", source / "File00000023.mpq"]
            for archive in archives:
                archive.write_bytes(b"synthetic MPQ path")

            table_data = {
                "data\\global\\excel\\weapons.txt": b"weapon table",
                "data\\global\\excel\\armor.txt": b"armor table",
                "data\\global\\excel\\charstats.txt": b"class table",
            }
            calls = []

            def extract(_archive, member):
                calls.append(member)
                try:
                    return table_data[member]
                except KeyError as error:
                    raise FileNotFoundError(member) from error

            with patch.object(d2_workspace, "SOURCE_DIR", source), \
                 patch.object(d2_workspace, "ITEM_TABLE_DIR", excel), \
                 patch.object(d2_workspace, "_run") as run, \
                 patch.object(d2_workspace.d2_import, "extract_mpq_member", side_effect=extract):
                got_archives, tables = d2_workspace.prepare_demo_sources()

            self.assertEqual(got_archives, sorted(archives))
            self.assertEqual([path.name for path in tables],
                             ["weapons.txt", "armor.txt", "charstats.txt"])
            self.assertEqual([path.read_bytes() for path in tables], list(table_data.values()))
            command = run.call_args.args[0]
            self.assertEqual(command[1], str(d2_workspace.TOOLS / "d2_fetch_demo.py"))
            self.assertNotIn("--download-only", command)
            self.assertEqual(calls, list(table_data))


if __name__ == "__main__":
    unittest.main()
