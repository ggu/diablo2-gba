import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.d2_import import (cof_info, dc6_frames, downsample_indexed, emit_header,
                             floor_buffer, resolve_mpq, stormlib_path,
                             rgb555_palette, write_png)


class D2ImportTests(unittest.TestCase):
    def test_dc6_rle_decode_preserves_index_and_origin(self):
        # One 2x1 frame: one opaque value followed by one transparent pixel.
        rle = bytes((1, 9, 0x81, 0x80))
        frame = struct.pack("<8I", 0, 2, 1, 0xFFFFFFFF, 3, 0, 0, len(rle)) + rle + b"\xee\xee\xee"
        data = struct.pack("<6I", 6, 0, 0, 0, 1, 1) + struct.pack("<I", 28) + frame
        dirs, per_dir, frames = dc6_frames(data)
        self.assertEqual((dirs, per_dir), (1, 1))
        self.assertEqual(frames[0]["pixels"], b"\x09\x00")
        self.assertEqual((frames[0]["x"], frames[0]["y"]), (-1, 3))

    def test_cof_layer_and_priority_decode(self):
        header = bytes((1, 2, 1)) + bytes(21) + bytes((7,))
        body = bytes(3) + bytes((0, 1, 1, 0, 0)) + b"hth\0" + bytes((0, 1)) + bytes((0, 1))
        cof = cof_info(header + body)
        self.assertEqual(cof["layers"][0]["weapon_class"], "hth")
        self.assertEqual(cof["events"], [0, 1])
        self.assertEqual(cof["priority"], [[0], [1]])

    def test_downsample_palette_and_png(self):
        self.assertEqual(downsample_indexed(bytes(range(16)), 4, 4, 2), (2, 2, bytes((5, 7, 13, 15))))
        raw_palette = bytes((i & 255) for i in range(768))
        self.assertEqual(len(rgb555_palette(raw_palette)), 256)
        red_palette = bytes((0, 0, 255)) + bytes(765)
        self.assertEqual(rgb555_palette(red_palette)[0], 0x001F)
        blue_palette = bytes((255, 0, 0)) + bytes(765)
        self.assertEqual(rgb555_palette(blue_palette)[0], 0x7C00)
        palette = [(i, i, i, 0 if i == 0 else 255) for i in range(256)]
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "sprite.png"
            write_png(out, 2, 1, b"\x00\x01", palette)
            self.assertTrue(out.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"))

    def test_header_matches_renderer_contract(self):
        frame = {"width": 1, "height": 1, "x": 0, "y": 0, "pixels": b"\x01"}
        animation = {"frames": 1, "images": [frame] * 8}
        actors = [[animation] * 5 for _ in range(4)]
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "d2_den_assets.h"
            emit_header(output, actors, [bytes(800)], [0] * 224)
            header = output.read_text()
        self.assertIn("D2SpriteFrame", header)
        self.assertIn("d2_den_actors[4][5]", header)
        self.assertIn("d2_den_floor_count = 1u", header)
        self.assertIn("d2_den_palette[224]", header)

    def test_stormlib_prefers_pinned_local_build_across_platform_names(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            library = root / ".local/d2/build/stormlib/libstorm.so"
            library.parent.mkdir(parents=True)
            library.write_bytes(b"fixture")
            with patch("tools.d2_import.ROOT", root), patch(
                    "ctypes.util.find_library", return_value="system-storm"):
                self.assertEqual(stormlib_path(), str(library))

    def test_embedded_mpq_detection_and_floor_fit(self):
        with tempfile.TemporaryDirectory() as tmp:
            installer = Path(tmp) / "installer.exe"
            prefix = b"MZ" + bytes(30)
            header = b"MPQ\x1a" + struct.pack("<IIHH", 32, 40, 0, 3) + bytes(16)
            installer.write_bytes(prefix + header + bytes(8))
            extracted = resolve_mpq(installer)
            self.assertEqual(extracted.read_bytes(), header + bytes(8))
        tile = {"width": 4, "height": 4, "pixels": bytes(range(16))}
        floor = floor_buffer(tile)
        self.assertEqual(len(floor), 800)
        self.assertEqual(floor[0], 10)


if __name__ == "__main__":
    unittest.main()
