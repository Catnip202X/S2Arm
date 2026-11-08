"""出包时选的显示默认值（pack/jobs/build.py display_env）单测。

口径：请求的 display 字段 → S2A_DISPLAY_REFRESH / S2A_DISPLAY_OUT（pack/stage_caps.sh 据此写包内 s2a_display.tsv，
运行时 runtime/src/display_cfg.c 只在环境变量未设时用它）。没给 = 不设任何键、产物与原先逐字节相同；
值域与宿主解析一致：refresh = off / display / 24–1000，outSize = native / fit / WxH（单边 1–16384）。
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import build   # noqa: E402
from core import Failure   # noqa: E402


class DisplayOptions(unittest.TestCase):
    def test_absent_means_no_keys(self):
        self.assertEqual(build.display_env(None), {})
        self.assertEqual(build.display_env({}), {})

    def test_refresh_forms(self):
        self.assertEqual(build.display_env({"refresh": "144"}), {"S2A_DISPLAY_REFRESH": "144"})
        self.assertEqual(build.display_env({"refresh": "display"}), {"S2A_DISPLAY_REFRESH": "display"})
        self.assertEqual(build.display_env({"refresh": "off"}), {"S2A_DISPLAY_REFRESH": "off"})
        self.assertEqual(build.display_env({"refresh": "24"}), {"S2A_DISPLAY_REFRESH": "24"})
        self.assertEqual(build.display_env({"refresh": "1000"}), {"S2A_DISPLAY_REFRESH": "1000"})

    def test_refresh_rejects_out_of_range_and_junk(self):
        for value in ("23", "1001", "12", "abc", "144x", "", " 60", 144, 60.0):
            with self.subTest(value=value), self.assertRaises(Failure):
                build.display_env({"refresh": value})

    def test_out_size_forms(self):
        self.assertEqual(build.display_env({"outSize": "2560x1440"}), {"S2A_DISPLAY_OUT": "2560x1440"})
        self.assertEqual(build.display_env({"outSize": "1920x1080"}), {"S2A_DISPLAY_OUT": "1920x1080"})
        self.assertEqual(build.display_env({"outSize": "native"}), {"S2A_DISPLAY_OUT": "native"})
        self.assertEqual(build.display_env({"outSize": "fit"}), {"S2A_DISPLAY_OUT": "fit"})

    def test_out_size_rejects_out_of_range_and_junk(self):
        for value in ("0x1440", "2560x0", "20000x1440", "2560x20000", "2560X1440", "2560", "1440p", "", 2560):
            with self.subTest(value=value), self.assertRaises(Failure):
                build.display_env({"outSize": value})

    def test_both_keys_and_unknown_keys_ignored(self):
        self.assertEqual(build.display_env({"refresh": "165", "outSize": "3840x2160", "future": "x"}),
                         {"S2A_DISPLAY_REFRESH": "165", "S2A_DISPLAY_OUT": "3840x2160"})

    def test_not_a_mapping(self):
        for value in ("144", ["refresh"], 144):
            with self.subTest(value=value), self.assertRaises(Failure):
                build.display_env(value)


if __name__ == "__main__":
    unittest.main()
