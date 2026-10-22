import hashlib
import io
import json
import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.d2_fetch_demo import (_download_verified, record_download, validate_embedded_mpq)


class D2FetchTests(unittest.TestCase):
    def test_verified_download_replaces_destination_atomically(self):
        data = b"verified demo fixture"
        md5 = hashlib.md5(data).hexdigest()
        sha256 = hashlib.sha256(data).hexdigest()
        with tempfile.TemporaryDirectory() as tmp:
            destination = Path(tmp) / "demo.exe"
            destination.write_bytes(b"old")
            _download_verified(io.BytesIO(data), destination, size=len(data),
                              md5=md5, sha256=sha256)
            self.assertEqual(destination.read_bytes(), data)
            self.assertEqual(list(Path(tmp).glob("*.partial")), [])

    def test_bad_download_keeps_old_file_and_removes_partial(self):
        data = b"wrong bytes"
        with tempfile.TemporaryDirectory() as tmp:
            destination = Path(tmp) / "demo.exe"
            destination.write_bytes(b"old")
            with self.assertRaisesRegex(ValueError, "MD5 mismatch"):
                _download_verified(io.BytesIO(data), destination, size=len(data),
                                  md5="0" * 32, sha256=hashlib.sha256(data).hexdigest())
            self.assertEqual(destination.read_bytes(), b"old")
            self.assertEqual(list(Path(tmp).iterdir()), [destination])

    def test_embedded_mpq_header_checks_bounds(self):
        with tempfile.TemporaryDirectory() as tmp:
            installer = Path(tmp) / "demo.exe"
            archive_size = 40
            header = b"MPQ\x1a" + struct.pack("<IIHH", 32, archive_size, 0, 3) + bytes(16)
            installer.write_bytes(bytes(0x4F000) + header + bytes(archive_size - len(header)))
            self.assertEqual(validate_embedded_mpq(installer), (0x4F000, archive_size))
            installer.write_bytes(installer.read_bytes() + b"trailing")
            with self.assertRaisesRegex(ValueError, "EOF"):
                validate_embedded_mpq(installer)

    def test_download_manifest_preserves_other_assets_and_updates_demo(self):
        data = b"verified demo fixture"
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            demo = folder / "diabloiidemo.exe"
            manifest_path = folder / "download-manifest.json"
            demo.write_bytes(data)
            manifest_path.write_text(json.dumps({"assets": [
                {"filename": "bootstrapper.exe", "sha256": "keep-me"},
                {"filename": demo.name, "sha256": "stale"},
            ]}), encoding="utf-8")
            with patch("tools.d2_fetch_demo.DEMO_SIZE", len(data)), \
                 patch("tools.d2_fetch_demo.DEMO_MD5", hashlib.md5(data).hexdigest()), \
                 patch("tools.d2_fetch_demo.DEMO_SHA256", hashlib.sha256(data).hexdigest()):
                record_download(demo, manifest_path)
            saved = json.loads(manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(len(saved["assets"]), 2)
            self.assertEqual(saved["assets"][0]["sha256"], "keep-me")
            self.assertEqual(saved["assets"][1]["url"], "https://archive.org/download/DiabloIiDemo/DiabloIIDemo.exe")
            self.assertEqual(saved["assets"][1]["size_bytes"], len(data))
            self.assertEqual(saved["assets"][1]["sha256"], hashlib.sha256(data).hexdigest())


if __name__ == "__main__":
    unittest.main()
