import unittest
from pathlib import Path
from unittest.mock import patch

from tools import d2_town_assets


class TownAssetTests(unittest.TestCase):
    def test_source_report_probes_canonical_map_and_akara_names(self):
        seen = []
        def fake_probe(_archives, member):
            seen.append(member)
            if member in (d2_town_assets.TOWN_MAPS[0], d2_town_assets.TOWN_TILES[0],
                          d2_town_assets.PALETTE, d2_town_assets.AKARA_COF["neutral"][0]) or \
                    member.startswith(r"data\global\items\inv"):
                return Path("user.mpq"), b"asset"
            return None, None
        with patch.object(d2_town_assets, "probe_member", side_effect=fake_probe):
            report = d2_town_assets.source_report([Path("user.mpq")])
        self.assertTrue(report["ready"])
        self.assertTrue(report["town_map_available"])
        self.assertTrue(report["akara_neutral_available"])
        self.assertFalse(report["akara_talk_available"])
        self.assertTrue(report["partial_ready"])
        self.assertEqual(report["item_icon_count"], len(d2_town_assets.ITEM_ICONS))
        self.assertIn(r"data\global\tiles\ACT1\TOWN\townN1.ds1", seen)
        self.assertIn(r"data\global\monsters\PS\COF\PSNUHTH.cof", seen)
        self.assertIn(r"data\global\monsters\PS\COF\PSS1HTH.cof", seen)

    def test_palette_mapping_keeps_transparency_and_uses_world_bank(self):
        source = [0] * 256
        shared = [i for i in range(224)]
        mapping = d2_town_assets.map_palette(source, shared)
        self.assertEqual(mapping[0], 0)
        self.assertTrue(all(32 <= value <= 255 for value in mapping[1:]))

    def test_ground_icon_uses_renderer_nearest_neighbor_mapping(self):
        icon = {"width": 4, "height": 3, "pixels": bytes(range(12))}
        ground = d2_town_assets.ground_icon(icon)
        self.assertEqual((ground["width"], ground["height"], ground["x"], ground["y"]),
                         (9, 15, -4, -12))
        expected = bytes((y * 3 // 15) * 4 + (x * 4 // 9)
                         for y in range(15) for x in range(9))
        self.assertEqual(ground["pixels"], expected)


if __name__ == "__main__":
    unittest.main()
