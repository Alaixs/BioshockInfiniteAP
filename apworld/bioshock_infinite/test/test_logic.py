"""The story is linear: an area only needs its gate items and those of the areas before it."""

from BaseClasses import CollectionState

from ..data import AREAS, LOCKPICKING, TEARS
from . import BioShockInfiniteTestBase

MONUMENT_PASS, FINKTON_PASS = "Monument Island Pass", "Finkton Pass"
ALL_GATES = {MONUMENT_PASS, TEARS, LOCKPICKING, FINKTON_PASS}
EXPECTED_GATES = {
    "Town Center": set(),
    "Monument Island": {MONUMENT_PASS},
    "Battleship Bay": {MONUMENT_PASS, TEARS, LOCKPICKING},
    "Finkton": ALL_GATES,
    "Emporia": ALL_GATES,
    "Hand of the Prophet": ALL_GATES,
}


class TestGates(BioShockInfiniteTestBase):
    def locations_in(self, area: str) -> list:
        return [loc for loc in self.multiworld.get_locations(self.player) if loc.parent_region.name == area]

    def test_gates_of_each_area(self) -> None:
        self.assertEqual(list(EXPECTED_GATES), AREAS)
        for area, gates in EXPECTED_GATES.items():
            self.assertEqual(set(self.world.gates_of(area)), gates, area)

    def test_each_area_needs_each_of_its_gates(self) -> None:
        for area in AREAS:
            locations = self.locations_in(area)
            self.assertTrue(locations, f"no location in {area} with the default options")
            for gate in EXPECTED_GATES[area]:
                state = CollectionState(self.multiworld)
                self.collect_all_but(gate, state)
                self.assertFalse(locations[0].can_reach(state), f"{area} reachable without {gate}")
            state = CollectionState(self.multiworld)
            self.collect_all_but([], state)
            self.assertTrue(all(loc.can_reach(state) for loc in locations), f"{area} unreachable with everything")

    def test_town_center_open_from_the_start(self) -> None:
        state = CollectionState(self.multiworld)
        self.assertTrue(all(loc.can_reach(state) for loc in self.locations_in("Town Center")))

    def test_gate_items_are_progression(self) -> None:
        for name in ALL_GATES:
            self.assertTrue(self.world.create_item(name).advancement, name)


class TestLateAreasOnly(BioShockInfiniteTestBase):
    """No location of ours before the gated areas: the gate items are given from the start."""

    options = {
        "shop_areas": ["Emporia"],
        "items_per_machine": 5,
        "items_per_upgrade_machine": 0,
        "voxophone_areas": ["Emporia", "Hand of the Prophet"],
        "infusion_areas": ["Emporia", "Hand of the Prophet"],
        "gear_areas": ["Emporia", "Hand of the Prophet"],
        "checkpoint_checks": "off",
        "randomize_vigors": False,
    }

    def test_gates_given_at_start(self) -> None:
        start = {item.name for item in self.multiworld.precollected_items[self.player]}
        self.assertLessEqual(ALL_GATES, start)
        self.assertFalse(ALL_GATES & {item.name for item in self.multiworld.itempool})
