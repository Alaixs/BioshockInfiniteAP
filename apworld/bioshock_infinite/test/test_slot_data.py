"""slot_data is all the client in the DLL knows about the seed."""

from ..data import LOCKPICKING, TEARS, item_name_to_id
from . import BioShockInfiniteTestBase


class TestSlotData(BioShockInfiniteTestBase):
    options = {"items_per_machine": 3, "minimum_price": 100, "maximum_price": 20}  # swapped on purpose
    run_default_tests = False

    def test_contents(self) -> None:
        self.finish_generation()
        data = self.world.fill_slot_data()
        ours = {loc.address for loc in self.multiworld.get_locations(self.player) if loc.address is not None}
        for path, ids in data["machines"].items():
            self.assertLessEqual(set(ids), ours, path)
        for loc, price in data["prices"].items():
            self.assertTrue(20 <= price <= 100, f"{loc}: {price}")
            self.assertIn(int(loc), ours)
        for table in ("voxophones", "infusions", "checkpoints", "gear"):
            self.assertLessEqual(set(data[table].values()), ours, table)
        self.assertEqual(data["goal"], "siphon")
        self.assertEqual(data["tears_item"], item_name_to_id[TEARS])
        self.assertEqual(data["lockpick_item"], item_name_to_id[LOCKPICKING])
        self.assertEqual(sorted(data["passes"].values()), ["finkton", "monumentisland"])
        self.assertEqual(len(data["vigors"]), 6)
        self.assertEqual(sorted(set().union(*data["areas"].values())), sorted(ours))


class TestSlotDataWithoutLocks(BioShockInfiniteTestBase):
    options = {"lock_tears": False, "lock_lockpicking": False, "area_passes": False, "randomize_vigors": False}
    run_default_tests = False

    def test_no_lock_keys(self) -> None:
        self.finish_generation()
        data = self.world.fill_slot_data()
        for key in ("tears_item", "lockpick_item", "passes"):
            self.assertNotIn(key, data)
        self.assertEqual(data["vigors"], {})
