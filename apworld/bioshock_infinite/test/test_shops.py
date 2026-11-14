"""Shop rows show their item before it is bought: none of our own traps, and our own Silver Eagles
are moved out of our shops whenever the logic allows it."""

from ..data import SILVER_EAGLES, TRAPS
from . import BioShockInfiniteTestBase


class TestOwnFillerOutOfShops(BioShockInfiniteTestBase):
    options = {"trap_percentage": 50, "items_per_machine": 3}
    run_default_tests = False

    def test_no_own_trap_in_own_shop(self) -> None:
        self.finish_generation()
        for location in self.world.shop_locations:
            item = location.item
            self.assertFalse(item.player == self.player and item.name in TRAPS, f"{item.name} in {location.name}")

    def test_own_silver_eagles_swapped_out(self) -> None:
        self.finish_generation()
        in_shops = [loc for loc in self.world.shop_locations if loc.item.name in SILVER_EAGLES]
        swappable = [
            loc
            for loc in self.multiworld.get_locations(self.player)
            if loc not in self.world.shop_locations
            and loc.address is not None
            and not loc.item.advancement
            and loc.item.name not in SILVER_EAGLES
            and loc.item.name not in TRAPS
        ]
        # A row keeps our Silver Eagles only when no other location had an item to swap with.
        self.assertTrue(not in_shops or not swappable, f"{len(in_shops)} rows sell our own Silver Eagles")
        self.assertTrue(self.multiworld.can_beat_game(self.multiworld.get_all_state(False)))
