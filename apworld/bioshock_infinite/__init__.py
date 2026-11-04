"""BioShock Infinite for Archipelago.

The checks are items sold in the game's vending machines (each machine has its own), the
Voxophones, Infusion bottles and Gear packages picked up, the Vigors found and the story
checkpoints reached. Received items unlock the machines' wares (health, salts, ammo), upgrade
health, shield or salts (Infusions), and give Vigors, upgrades, Gear pieces or Silver Eagles. The
game mod (xinput1_3.dll) does the rest: it contains the Archipelago client (connection from the
game's menu), which only reads the slot_data below.
"""

from BaseClasses import Item, ItemClassification, Location, Region, Tutorial
from Options import OptionError
from worlds.AutoWorld import WebWorld, World

from .data import (
    AREAS,
    CHECKPOINT_LOCATIONS,
    DOLLAR_BILLS,
    GAME,
    GEAR_ITEMS,
    GEAR_LOCATIONS,
    INFUSION,
    INFUSION_LOCATIONS,
    LOCKPICKING,
    LOCKPICKING_FIRST_AREA,
    PASSES,
    SILVER_EAGLES,
    TEARS,
    TEARS_FIRST_AREA,
    TRAPS,
    UNLOCKS,
    UPGRADE_SHOPS,
    UPGRADES,
    VICTORY,
    VIGOR_LOCATIONS,
    VIGOR_VENDING_LOOT,
    VIGORS,
    VOXOPHONE_LOCATIONS,
    gear_item_name,
    item_name_groups,
    item_name_to_id,
    location_name_groups,
    location_name_to_id,
    shop_location_name,
    upgrade_item_name,
    upgrade_shop_location_name,
    vigor_item_name,
)
from .options import BioShockInfiniteOptions, CheckpointChecks, Goal, option_groups


class BioShockInfiniteItem(Item):
    game = GAME


class BioShockInfiniteLocation(Location):
    game = GAME


class BioShockInfiniteWeb(WebWorld):
    tutorials = [
        Tutorial(
            "Multiworld Setup Guide",
            "Installing the mod and connecting BioShock Infinite to Archipelago.",
            "English",
            "setup_en.md",
            "setup/en",
            ["Alaix"],
        )
    ]
    option_groups = option_groups
    bug_report_page = "https://github.com/Alaixs/BioshockInfiniteAP/issues"


class BioShockInfiniteWorld(World):
    """
    BioShock Infinite: Booker DeWitt is sent to the floating city of Columbia to find a girl.
    In this randomizer, vending machines sell other players' items, Voxophones, Infusions, Vigors
    and Gear hide them, and the machines' own wares must be unlocked first.
    """

    game = GAME
    web = BioShockInfiniteWeb()
    options_dataclass = BioShockInfiniteOptions
    options: BioShockInfiniteOptions
    origin_region_name = "Menu"

    item_name_to_id = item_name_to_id
    location_name_to_id = location_name_to_id
    location_name_groups = location_name_groups
    item_name_groups = item_name_groups

    def generate_early(self) -> None:
        if self.options.minimum_price > self.options.maximum_price:
            self.options.minimum_price.value, self.options.maximum_price.value = (
                self.options.maximum_price.value,
                self.options.minimum_price.value,
            )
        self.machines = [
            (area, number, path) for area, number, path in DOLLAR_BILLS if area in self.options.shop_areas.value
        ]
        self.voxophones = [
            (name, area, recording)
            for name, area, recording in VOXOPHONE_LOCATIONS
            if area in self.options.voxophone_areas.value
        ]
        self.infusions = [
            (name, area, ident) for name, area, ident in INFUSION_LOCATIONS if area in self.options.infusion_areas.value
        ]
        self.vigors = list(VIGOR_LOCATIONS) if self.options.randomize_vigors else []
        self.gear = [
            (name, area, ident) for name, area, ident in GEAR_LOCATIONS if area in self.options.gear_areas.value
        ]
        per_upgrade = self.options.items_per_upgrade_machine.value
        self.upgrade_shops = (
            [shop for shop in UPGRADE_SHOPS if shop[0] in self.options.shop_areas.value] if per_upgrade else []
        )
        self.upgrades = list(UPGRADES) if per_upgrade else []
        checks = self.options.checkpoint_checks.value
        self.checkpoints = [
            (name, area, step)
            for name, area, step, chapter in CHECKPOINT_LOCATIONS
            if checks == CheckpointChecks.option_all or (checks == CheckpointChecks.option_chapters and chapter)
        ]
        # Gates: progression items an area and the ones after it need (item, first area).
        self.tears_item = bool(self.options.lock_tears)
        self.gates: list[tuple[str, str]] = [(TEARS, TEARS_FIRST_AREA)] if self.tears_item else []
        self.lockpicking_item = bool(self.options.lock_lockpicking)
        if self.lockpicking_item:
            self.gates.append((LOCKPICKING, LOCKPICKING_FIRST_AREA))
        if self.options.area_passes:
            self.gates += [(name, area) for name, _, area in PASSES]
        count = self.location_count()
        needed = len(UNLOCKS) + len(self.infusions) + len(self.vigors) + len(self.gear) + len(self.upgrades)
        needed += len(self.gates)
        if count < needed:
            raise OptionError(
                f"{self.player_name}: only {count} locations for {needed} items. "
                f"Enable more shop_areas or voxophone_areas, or raise items_per_machine."
            )

    def create_regions(self) -> None:
        menu = Region("Menu", self.player, self.multiworld)
        self.multiworld.regions.append(menu)
        # One region per area. The story is linear: the only rules are the gates (set_rules).
        regions: dict[str, Region] = {}

        def region(area: str) -> Region:
            if area not in regions:
                regions[area] = Region(area, self.player, self.multiworld)
                self.multiworld.regions.append(regions[area])
                menu.connect(regions[area])
            return regions[area]

        def shop_location(name: str, area: str) -> None:
            # The row's name shows before buying, so none of our own traps (other players' are
            # fine). Our Silver Eagles are moved out in finalize_multiworld.
            location = BioShockInfiniteLocation(self.player, name, location_name_to_id[name], region(area))
            location.item_rule = lambda item: not (item.player == self.player and item.name in TRAPS)
            region(area).locations.append(location)
            self.shop_locations.append(location)

        self.shop_locations: list[Location] = []
        self.area_regions = regions

        for area, number, _ in self.machines:
            for item in range(1, self.options.items_per_machine + 1):
                shop_location(shop_location_name(area, number, item), area)
        for area, number, kind, _ in self.upgrade_shops:
            for item in range(1, self.options.items_per_upgrade_machine + 1):
                shop_location(upgrade_shop_location_name(area, kind, number, item), area)
        for name, area, _ in [*self.voxophones, *self.infusions, *self.vigors, *self.gear, *self.checkpoints]:
            region(area).locations.append(
                BioShockInfiniteLocation(self.player, name, location_name_to_id[name], region(area))
            )

        # Victory: the mod reports it when Songbird destroys the Siphon, or when the whole shop is
        # bought.
        if self.options.goal == Goal.option_siphon:
            goal_region, goal_name = region("Hand of the Prophet"), "Siphon Destroyed"
        else:
            goal_region, goal_name = menu, "Shop Complete"
        victory = BioShockInfiniteLocation(self.player, goal_name, None, goal_region)
        victory.place_locked_item(BioShockInfiniteItem(VICTORY, ItemClassification.progression, None, self.player))
        goal_region.locations.append(victory)

    def create_item(self, name: str) -> BioShockInfiniteItem:
        if name in (TEARS, LOCKPICKING) or name in item_name_groups["Passes"]:
            return BioShockInfiniteItem(name, ItemClassification.progression, item_name_to_id[name], self.player)
        if name in TRAPS:
            return BioShockInfiniteItem(name, ItemClassification.trap, item_name_to_id[name], self.player)
        useful = (
            name in UNLOCKS
            or name == INFUSION
            or name in item_name_groups["Vigors"]
            or name in item_name_groups["Gear"]
            or name in item_name_groups["Upgrades"]
        )
        classification = ItemClassification.useful if useful else ItemClassification.filler
        return BioShockInfiniteItem(name, classification, item_name_to_id[name], self.player)

    def get_filler_item_name(self) -> str:
        return self.random.choice(list(SILVER_EAGLES))

    def create_items(self) -> None:
        pool = [self.create_item(name) for name in UNLOCKS]
        pool += [self.create_item(INFUSION) for _ in self.infusions]
        if self.vigors:
            pool += [self.create_item(vigor_item_name(name)) for _, name, _, _, _ in VIGORS]
        # No duplicates: there are more pieces than packages.
        pieces = self.random.sample([gear_item_name(name) for name, _, _ in GEAR_ITEMS], len(self.gear))
        pool += [self.create_item(name) for name in pieces]
        pool += [self.create_item(upgrade_item_name(name)) for name, _, _ in self.upgrades]
        for name, first_area in self.gates:
            early = AREAS[: AREAS.index(first_area)]
            if any(self.area_regions[area].locations for area in early if area in self.area_regions):
                pool.append(self.create_item(name))
            else:  # no location of ours before its area: given from the start
                self.multiworld.push_precollected(self.create_item(name))
        total = self.location_count()
        # Silver Eagles, some replaced by traps. The Boys of Silence scream only if the player
        # asked for it. Our traps are never sold in our own shops: alone in the multiworld, they
        # must fit in our other locations even if every other item of ours lands there first.
        traps = [name for name in TRAPS if name != "Boys of Silence Trap" or self.options.boys_of_silence_trap]
        room = total - len(pool)
        if self.multiworld.players == 1:
            room -= len(self.shop_locations)
        for _ in range(total - len(pool)):
            if traps and room > 0 and self.random.randint(1, 100) <= self.options.trap_percentage:
                pool.append(self.create_item(self.random.choice(traps)))
                room -= 1
            else:
                pool.append(self.create_filler())
        self.multiworld.itempool += pool

    def location_count(self) -> int:
        return (
            len(self.machines) * self.options.items_per_machine
            + len(self.voxophones)
            + len(self.infusions)
            + len(self.vigors)
            + len(self.gear)
            + len(self.upgrade_shops) * self.options.items_per_upgrade_machine
            + len(self.checkpoints)
        )

    def set_rules(self) -> None:
        self.multiworld.completion_condition[self.player] = lambda state: state.has(VICTORY, self.player)
        # Each area needs its own gates and those of the areas before it.
        for area, region in self.area_regions.items():
            needed = self.gates_of(area)
            if needed:
                for entrance in region.entrances:
                    entrance.access_rule = lambda state, needed=needed: state.has_all(needed, self.player)

    def gates_of(self, area: str) -> list[str]:
        """Gate items the area needs."""
        return [name for name, first_area in self.gates if AREAS.index(area) >= AREAS.index(first_area)]

    def finalize_multiworld(self) -> None:
        # Runs after progression balancing. Paying to receive our own money makes no sense, and a
        # fill rule can't be honored solo with many rows per machine, so our Silver Eagles are
        # swapped with the item of another of our locations with the same gates (the logic doesn't
        # change). With no such swap, the row stays.
        shops = set(self.shop_locations)
        others = [
            loc
            for loc in self.multiworld.get_locations(self.player)
            if loc not in shops and loc.address is not None and loc.item and not loc.locked and not self.own_filler(loc)
        ]
        self.random.shuffle(others)
        for shop in self.shop_locations:
            if shop.item and shop.item.player == self.player and shop.item.name in SILVER_EAGLES:
                self.swap_out(shop, others)

    def own_filler(self, location: Location) -> bool:
        item = location.item
        return item.player == self.player and (item.name in SILVER_EAGLES or item.name in TRAPS)

    def gates_at(self, location: Location) -> list[str]:
        area = location.parent_region.name
        return self.gates_of(area) if area in AREAS else []

    def swap_out(self, shop: Location, others: list[Location]) -> None:
        """Swaps the shop row's item with the first of others that keeps the logic as it is."""
        state = self.multiworld.state
        for other in others:
            if other.item.advancement and self.gates_at(other) != self.gates_at(shop):
                continue
            if shop.can_fill(state, other.item, False) and other.can_fill(state, shop.item, False):
                shop.item, other.item = other.item, shop.item
                shop.item.location, other.item.location = shop, other
                others.remove(other)
                return

    def fill_slot_data(self) -> dict:
        low, high = self.options.minimum_price.value, self.options.maximum_price.value
        machines: dict[str, list[int]] = {}
        prices: dict[str, int] = {}
        for area, number, path in self.machines:
            ids = [
                location_name_to_id[shop_location_name(area, number, item)]
                for item in range(1, self.options.items_per_machine + 1)
            ]
            machines[path] = ids
            for loc in ids:
                prices[str(loc)] = self.random.randint(low, high)
        for area, number, kind, paths in self.upgrade_shops:
            ids = [
                location_name_to_id[upgrade_shop_location_name(area, kind, number, item)]
                for item in range(1, self.options.items_per_upgrade_machine + 1)
            ]
            for path in paths:
                machines[path] = ids
            for loc in ids:
                prices[str(loc)] = self.random.randint(low, high)
        # The DLL's client doesn't know the world's tables: everything it needs goes here.
        return {
            "goal": "siphon" if self.options.goal == Goal.option_siphon else "shop",
            "machines": machines,
            "prices": prices,
            "voxophones": {recording: location_name_to_id[name] for name, _, recording in self.voxophones},
            "infusions": {ident: location_name_to_id[name] for name, _, ident in self.infusions},
            "checkpoints": {step: location_name_to_id[name] for name, _, step in self.checkpoints},
            "gear": {ident: location_name_to_id[name] for name, _, ident in self.gear},
            "gear_items": {str(item_name_to_id[gear_item_name(name)]): lookup for name, _, lookup in GEAR_ITEMS},
            # Upgrade item -> game object given on receipt; the original rows stay locked.
            "upgrade_items": {
                str(item_name_to_id[upgrade_item_name(name)]): lookup for name, _, lookup in self.upgrades
            },
            "locked_loot": [loot for _, loot, _ in self.upgrades],
            "traps": {str(item_name_to_id[name]): command for name, command in TRAPS.items()},
            # For trackers: the server doesn't send them the location groups.
            "areas": {
                area: sorted(
                    loc.address
                    for loc in self.multiworld.get_locations(self.player)
                    if loc.address is not None and loc.name in location_name_groups[area]
                )
                for area in AREAS
            },
            "lock_vending_items": bool(self.options.lock_vending_items.value),
            **({"tears_item": item_name_to_id[TEARS]} if self.tears_item else {}),
            **({"lockpick_item": item_name_to_id[LOCKPICKING]} if self.lockpicking_item else {}),
            **(
                {"passes": {str(item_name_to_id[name]): key for name, key, _ in PASSES}}
                if self.options.area_passes
                else {}
            ),
            "death_link": bool(self.options.death_link.value),
            "unlocks": {str(item_name_to_id[name]): patterns for name, patterns in UNLOCKS.items()},
            "currency": {str(item_name_to_id[name]): amount for name, amount in SILVER_EAGLES.items()},
            "infusion_item": item_name_to_id[INFUSION],
            # Randomized Vigors, with the Veni! Vidi! Vigor! row hidden until the item comes in.
            "vigors": {
                key: {
                    "location": location_name_to_id[location],
                    "item": item_name_to_id[vigor_item_name(name)],
                    "lookup": lookup,
                    "loot": VIGOR_VENDING_LOOT[key],
                }
                for (key, name, _, _, lookup), (location, _, _) in zip(VIGORS, VIGOR_LOCATIONS)
            }
            if self.vigors
            else {},
        }
