from dataclasses import dataclass

from Options import (
    Choice,
    DeathLink,
    DefaultOnToggle,
    OptionGroup,
    OptionSet,
    PerGameCommonOptions,
    Range,
    StartInventoryPool,
    Toggle,
)

from .data import GEAR_AREAS, INFUSION_AREAS, MAX_ITEMS_PER_MACHINE, SHOP_AREAS, VOXOPHONE_AREAS


class Goal(Choice):
    """What completes your game.
    Siphon: reach the end of the story, when Songbird destroys the Siphon on Monument Island.
    Shop: buy every Archipelago item sold in the Dollar Bill machines."""

    display_name = "Goal"
    option_siphon = 0
    option_shop = 1
    default = 0


class ItemsPerMachine(Range):
    """How many Archipelago items each Dollar Bill vending machine sells (34 machines in the game)."""

    display_name = "Items per Machine"
    range_start = 1
    range_end = MAX_ITEMS_PER_MACHINE
    default = 2


class ItemsPerUpgradeMachine(Range):
    """How many Archipelago items each Veni! Vidi! Vigor! and Minuteman's Armory machine sells
    (54 machines in the game). With 1 or more, their Vigor and weapon upgrades become Archipelago
    items, applied as soon as you receive them; the machines no longer sell them.
    0 keeps these machines and upgrades vanilla."""

    display_name = "Items per Upgrade Machine"
    range_start = 0
    range_end = MAX_ITEMS_PER_MACHINE
    default = 1


class ShopAreas(OptionSet):
    """Areas whose vending machines (Dollar Bill, Veni! Vidi! Vigor!, Minuteman's Armory) sell
    Archipelago items.
    Removing late areas keeps other players from waiting on the end of your game."""

    display_name = "Shop Areas"
    valid_keys = frozenset(SHOP_AREAS)
    default = frozenset(SHOP_AREAS)


class VoxophoneAreas(OptionSet):
    """Areas whose Voxophones (audio logs) are locations: picking one up sends a check.
    Leave empty to keep Voxophones out of the randomizer."""

    display_name = "Voxophone Areas"
    valid_keys = frozenset(VOXOPHONE_AREAS)
    default = frozenset(VOXOPHONE_AREAS)


class InfusionAreas(OptionSet):
    """Areas whose Infusion bottles are locations: picking one up sends a check instead of
    upgrading you. One Infusion item per bottle is shuffled into the multiworld: when you
    receive it, the game's Infusion screen opens and you pick Health, Shield or Salts.
    Leave empty to keep Infusions vanilla."""

    display_name = "Infusion Areas"
    valid_keys = frozenset(INFUSION_AREAS)
    default = frozenset(INFUSION_AREAS)


class GearAreas(OptionSet):
    """Areas whose Gear packages (hats, shirts, pants, boots) are locations: picking one up sends
    a check instead of giving you a piece. For each package, one Gear piece is shuffled into the
    multiworld (no duplicates); you receive it straight into your Gear inventory.
    Leave empty to keep Gear vanilla."""

    display_name = "Gear Areas"
    valid_keys = frozenset(GEAR_AREAS)
    default = frozenset(GEAR_AREAS)


class CheckpointChecks(Choice):
    """Reaching a point of the story sends a check.
    Chapters: the 37 chapters of the Load Chapter menu (Welcome Center to Command Deck).
    All: every autosave checkpoint of the story (99), chapters included.
    Off: no story checks."""

    display_name = "Checkpoint Checks"
    option_off = 0
    option_chapters = 1
    option_all = 2
    default = 1


class TrapPercentage(Range):
    """Percentage of filler items (Silver Eagles) replaced by traps: Disarm, Jammed Weapons,
    Vigor Lock, Elizabeth Strike and Walk Only (plus the Boys of Silence trap if enabled).
    Your own traps are never sold in your own shops: in a solo game, you get no more of them than
    your other locations can hold."""

    display_name = "Trap Percentage"
    range_start = 0
    range_end = 100
    default = 10


class BoysOfSilenceTrap(Toggle):
    """CONTENT WARNING: jump scare, gore. Adds the Boys of Silence trap to the traps: a sudden,
    very loud scream and a bloody full-screen image. Off by default."""

    display_name = "Boys of Silence Trap (jump scare)"


class RandomizeVigors(DefaultOnToggle):
    """Devil's Kiss, Murder of Crows, Bucking Bronco, Charge, Undertow and Return to Sender become
    Archipelago items: finding one in the game sends a check, and you can only use it once you
    receive its item. Possession and Shock Jockey stay where the story gives them (the story
    needs them)."""

    display_name = "Randomize Vigors"


class MinimumPrice(Range):
    """Price, in Silver Eagles, of the cheapest Archipelago shop item."""

    display_name = "Minimum Price"
    range_start = 1
    range_end = 500
    default = 10


class MaximumPrice(Range):
    """Price, in Silver Eagles, of the most expensive Archipelago shop item."""

    display_name = "Maximum Price"
    range_start = 1
    range_end = 500
    default = 50


class LockVendingItems(DefaultOnToggle):
    """Health kits, salts and every ammo type can't be bought from Dollar Bill machines
    until you receive the matching unlock item."""

    display_name = "Lock Vending Items"


class LockTears(DefaultOnToggle):
    """Elizabeth can't open Tears (cover, weapons, allies...) until you receive the Elizabeth's
    Tears item. The logic places it before Battleship Bay, where the first Tears appear."""

    display_name = "Lock Tears"


class LockLockpicking(DefaultOnToggle):
    """Elizabeth can't pick locked doors and safes that cost lockpicks until you receive the
    Lockpicking item. Free locks on the story path are not affected. The logic places it before
    Battleship Bay, where the first locks appear."""

    display_name = "Lock Lockpicking"


class AreaPasses(DefaultOnToggle):
    """The gondola to Monument Island and the First Lady to Finkton only take you once you receive
    the Monument Island Pass and the Finkton Pass. The logic places each pass before its area."""

    display_name = "Area Passes"


@dataclass
class BioShockInfiniteOptions(PerGameCommonOptions):
    goal: Goal
    items_per_machine: ItemsPerMachine
    items_per_upgrade_machine: ItemsPerUpgradeMachine
    shop_areas: ShopAreas
    voxophone_areas: VoxophoneAreas
    infusion_areas: InfusionAreas
    gear_areas: GearAreas
    checkpoint_checks: CheckpointChecks
    trap_percentage: TrapPercentage
    boys_of_silence_trap: BoysOfSilenceTrap
    randomize_vigors: RandomizeVigors
    minimum_price: MinimumPrice
    maximum_price: MaximumPrice
    lock_vending_items: LockVendingItems
    lock_tears: LockTears
    lock_lockpicking: LockLockpicking
    area_passes: AreaPasses
    death_link: DeathLink
    start_inventory_from_pool: StartInventoryPool


# Sections of the options page and of the template YAML (the others go to "Game Options").
option_groups = [
    OptionGroup(
        "Locations",
        [
            ShopAreas,
            ItemsPerMachine,
            ItemsPerUpgradeMachine,
            VoxophoneAreas,
            InfusionAreas,
            GearAreas,
            CheckpointChecks,
            RandomizeVigors,
        ],
    ),
    OptionGroup("Locks", [LockVendingItems, LockTears, LockLockpicking, AreaPasses]),
    OptionGroup("Shop Prices", [MinimumPrice, MaximumPrice]),
    OptionGroup("Traps", [TrapPercentage, BoysOfSilenceTrap]),
]
