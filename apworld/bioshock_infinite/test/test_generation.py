"""Generation with various options. Each class with options also runs Archipelago's base checks:
everything reachable with all items, something reachable with none, and a valid fill."""

from Options import OptionError

from . import BioShockInfiniteTestBase


class TestDefaults(BioShockInfiniteTestBase):
    options = {"goal": "siphon"}  # the default, given so that the base checks run


class TestShopGoal(BioShockInfiniteTestBase):
    options = {"goal": "shop"}


class TestEverything(BioShockInfiniteTestBase):
    options = {
        "items_per_machine": 5,
        "items_per_upgrade_machine": 5,
        "checkpoint_checks": "all",
        "trap_percentage": 100,
        "boys_of_silence_trap": True,
    }


class TestDollarBillsOnly(BioShockInfiniteTestBase):
    options = {
        "items_per_upgrade_machine": 0,
        "voxophone_areas": [],
        "infusion_areas": [],
        "gear_areas": [],
        "checkpoint_checks": "off",
        "randomize_vigors": False,
        "lock_vending_items": False,
        "lock_tears": False,
        "lock_lockpicking": False,
        "area_passes": False,
    }

    def test_no_gate(self) -> None:
        self.assertEqual(self.world.gates, [])


class TestTooFewLocations(BioShockInfiniteTestBase):
    auto_construct = False
    options = {
        "shop_areas": ["Town Center"],
        "items_per_machine": 1,
        "items_per_upgrade_machine": 0,
        "voxophone_areas": [],
        "infusion_areas": [],
        "gear_areas": [],
        "checkpoint_checks": "off",
        "randomize_vigors": False,
    }

    def test_refused(self) -> None:
        with self.assertRaises(OptionError):
            self.world_setup()
