import unittest
from inventory import api_exports, plugin_references


class InventoryTest(unittest.TestCase):
    def test_comments_and_strings_are_not_dependencies(self):
        data = plugin_references('''
-- plugin.danger()
--[=[ plugin.more_danger() ]=]
local text = "plugin.fake()"
local long = [==[ plugin.also_fake() ]==]
plugin.define({id="example", api_min=16})
plugin.on("track_started", function() plugin.storage.set("x", "y") end)
if plugin.has_capability("audio.peq") then plugin.get_eq_state() end
''')
        self.assertEqual(data["references"], ["define", "get_eq_state", "has_capability", "on", "storage.set"])
        self.assertEqual(data["api_min_literal"], 16)
        self.assertEqual(data["events"], ["track_started"])
        self.assertEqual(data["capabilities_checked"], ["audio.peq"])

    def test_alias_and_computed_access_are_explicit_unknowns(self):
        data = plugin_references('local p = plugin; p.pause(); plugin[which]()')
        self.assertEqual(len(data["dynamic_access"]), 2)
        self.assertEqual(data["references"], [])

    def test_nested_store_and_raw_file_io(self):
        data = plugin_references('plugin.secrets.get("key"); io.open("/data/device_config.ini", "r")')
        self.assertEqual(data["references"], ["secrets.get"])
        self.assertEqual(data["raw_io_os"], ["io.open"])

    def test_missing_registry_fails_closed(self):
        with self.assertRaises(ValueError):
            api_exports('static const luaL_Reg renamed[] = {};')


if __name__ == "__main__":
    unittest.main()
