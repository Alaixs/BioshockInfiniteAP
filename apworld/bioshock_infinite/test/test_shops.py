"""Shop rows show their item before it is bought: none of our own traps, and our own Silver Eagles
are moved out of our shops whenever the logic allows it. No progression in shops by default, and
prices that follow the story."""

from ..data import SHOP_AREAS, SILVER_EAGLES, TRAPS
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


class TestNoProgressionInShops(BioShockInfiniteTestBase):
    """By default, a row the player can't afford (or a shop left behind) must never block anyone."""

    options = {"items_per_machine": 5, "items_per_upgrade_machine": 2}
    run_default_tests = False

    def test_no_progression_in_shops(self) -> None:
        self.finish_generation()
        for location in self.world.shop_locations:
            self.assertFalse(location.item.advancement, f"{location.item.name} in {location.name}")
        self.assertTrue(self.multiworld.can_beat_game(self.multiworld.get_all_state(False)))


class TestProgressionInShopsAllowed(BioShockInfiniteTestBase):
    options = {"progression_in_shops": True}
    run_default_tests = False

    def test_rule_lets_progression_in(self) -> None:
        progression = self.world.create_item("Monument Island Pass")
        self.assertTrue(progression.advancement)
        self.assertTrue(all(loc.item_rule(progression) for loc in self.world.shop_locations))


class TestPricesByArea(BioShockInfiniteTestBase):
    """Each shop area draws from a 40% window of the price range that moves up with the story."""

    options = {"minimum_price": 10, "maximum_price": 110}
    run_default_tests = False

    def test_prices_follow_the_story(self) -> None:
        data = self.world.fill_slot_data()
        area_of = {location.address: location.parent_region.name for location in self.world.shop_locations}
        for loc, price in data["prices"].items():
            position = SHOP_AREAS.index(area_of[int(loc)]) / (len(SHOP_AREAS) - 1)
            start = 10 + round(100 * 0.6 * position)
            self.assertTrue(start <= price <= start + 40, f"{area_of[int(loc)]}: {price}")
        first = [p for loc, p in data["prices"].items() if area_of[int(loc)] == SHOP_AREAS[0]]
        last = [p for loc, p in data["prices"].items() if area_of[int(loc)] == SHOP_AREAS[-1]]
        self.assertLess(max(first), min(last))


class TestFlatPrices(BioShockInfiniteTestBase):
    options = {"minimum_price": 10, "maximum_price": 110, "price_scaling": "flat"}
    run_default_tests = False

    def test_prices_anywhere_in_the_range(self) -> None:
        prices = self.world.fill_slot_data()["prices"].values()
        self.assertTrue(all(10 <= p <= 110 for p in prices))
        self.assertGreater(max(prices) - min(prices), 50)
