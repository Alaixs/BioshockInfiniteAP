"""Tests of the BioShock Infinite world, run by Archipelago's test runner: see apworld/run_tests.py."""

from Fill import distribute_items_restrictive
from test.bases import WorldTestBase
from worlds.AutoWorld import call_all


class BioShockInfiniteTestBase(WorldTestBase):
    game = "BioShock Infinite"

    def finish_generation(self) -> None:
        """The rest of a solo generation (Main.main): fill, post_fill, finalize_multiworld."""
        distribute_items_restrictive(self.multiworld)
        call_all(self.multiworld, "post_fill")
        call_all(self.multiworld, "finalize_multiworld")
