"""World tables: items, locations (vending machines, Voxophones, Infusions, Vigors, Gear, checkpoints)."""

from .checkpoints import CHECKPOINTS
from .gear import GEAR_ITEMS, GEAR_PICKUPS
from .infusions import INFUSIONS
from .machines import DOLLAR_BILLS
from .upgrades import UPGRADE_MACHINES, UPGRADES
from .voxophones import VOXOPHONES

GAME = "BioShock Infinite"

# Ids only need to be unique within this game.
BASE_ID = 77_100_000

# Unlock item -> loot patterns (from XVendingMachine.ItemList). A trailing "*" covers every
# variant, like a double clip sold elsewhere.
UNLOCKS: dict[str, list[str]] = {
    "Health Kit": ["Loot_Consumable_Health_HealthKit"],
    "Small Health Kit": ["Loot_Consumable_Health_HealthKitSmall"],
    "Salts": ["Loot_Currency_VigorEnergy_Vending"],
    "Small Salts": ["Loot_Currency_VigorEnergy_VendingSmall"],
    "Carbine Ammo": ["Loot_Ammo_Weapon_Carbine_Founder*"],
    "Burstgun Ammo": ["Loot_Ammo_Weapon_Carbine_VP*"],
    "Volley Gun Ammo": ["Loot_Ammo_Weapon_FlakCannon_Founder*"],
    "Hail Fire Ammo": ["Loot_Ammo_Weapon_FlakCannon_VP*"],
    "Hand Cannon Ammo": ["Loot_Ammo_Weapon_HandCannon*"],
    "Pistol Ammo": ["Loot_Ammo_Weapon_Pistol*"],
    "Machine Gun Ammo": ["Loot_Ammo_Weapon_MachineGun_Founder*"],
    "Repeater Ammo": ["Loot_Ammo_Weapon_MachineGun_VP*"],
    "RPG Ammo": ["Loot_Ammo_Weapon_RPG*"],
    "Shotgun Ammo": ["Loot_Ammo_Weapon_Shotgun_Founder*"],
    "Heater Ammo": ["Loot_Ammo_Weapon_Shotgun_VP*"],
    "Sniper Rifle Ammo": ["Loot_Ammo_Weapon_SniperRifle*"],
}

# Filler: name -> Silver Eagles given (XPlayerController.ClientAdjustCurrency(1, n)).
SILVER_EAGLES: dict[str, int] = {
    "10 Silver Eagles": 10,
    "25 Silver Eagles": 25,
    "50 Silver Eagles": 50,
}

# Traps: item name -> mod command (trap <type> <seconds>), triggered on receipt.
TRAPS: dict[str, str] = {
    "Disarm Trap": "disarm 20",
    "Jammed Weapons Trap": "jam 30",
    "Vigor Lock Trap": "novigor 30",
    "Elizabeth Strike Trap": "lizstrike 60",
    "Walk Only Trap": "walk 30",
    "Boys of Silence Trap": "scream 0",
}

# lock_tears: the first Tears come with the Hall of Heroes, so the logic needs this from Battleship
# Bay on.
TEARS = "Elizabeth's Tears"
TEARS_FIRST_AREA = "Battleship Bay"

# lock_lockpicking: free locks on the story path stay open. Elizabeth picks her first locks in
# Battleship Bay, and some checks are behind paid locks.
LOCKPICKING = "Lockpicking"
LOCKPICKING_FIRST_AREA = "Battleship Bay"

# area_passes: (item, mod key, first area behind it). The other areas start with a cutscene, so the
# passes before them gate them.
PASSES: list[tuple[str, str, str]] = [
    ("Monument Island Pass", "monumentisland", "Monument Island"),
    ("Finkton Pass", "finkton", "Finkton"),
]

# One per bottle: the player picks health, shield or salts on receipt.
INFUSION = "Infusion"

# Possession and Shock Jockey stay where they are: the story needs them. (mod key, name, area,
# where the game gives it, Consumable_Plasmid_* given on receipt)
VIGORS: list[tuple[str, str, str, str, int]] = [
    ("devilskiss", "Devil's Kiss", "Town Center", "Raffle Square", 241),
    ("murderofcrows", "Murder of Crows", "Town Center", "Comstock Center Rooftops", 257),
    ("buckingbronco", "Bucking Bronco", "Battleship Bay", "Hall of Heroes", 184),
    ("charge", "Charge", "Finkton", "Finkton Proper", 543),
    ("undertow", "Undertow", "Finkton", "The Factory", 5),
    ("returntosender", "Return to Sender", "Emporia", "Downtown Emporia", 474),
]


# Veni! Vidi! Vigor! row of each randomized Vigor, hidden until its item comes in: otherwise the
# player would pay for a Vigor the mod takes away at once.
VIGOR_VENDING_LOOT = {
    "devilskiss": "Loot_Consumable_Plasmid_Incinerate_Founder_MaxAmmo",
    "murderofcrows": "Loot_Consumable_Plasmid_MurderOfCrows_Founder_MaxAmmo",
    "buckingbronco": "Loot_Consumable_Plasmid_RollingThunder_Founder_MaxAmmo",
    "charge": "Loot_Consumable_Plasmid_Charge_Founder_MaxAmmo",
    "undertow": "Loot_Consumable_Plasmid_Undertow_Founder_MaxAmmo",
    "returntosender": "Loot_Consumable_Plasmid_ReturnToSender_Founder_MaxAmmo",
}


def vigor_item_name(name: str) -> str:
    return f"Vigor: {name}"


def vigor_location_name(name: str, place: str) -> str:
    return f"Vigor - {name} ({place})"


# Gear: one specific piece per item, given as is by the game (XItem.ItemLookupID).
def gear_item_name(name: str) -> str:
    return f"Gear: {name}"


# Veni! Vidi! Vigor! and Minuteman's Armory upgrades: one per item, applied on receipt.
def upgrade_item_name(name: str) -> str:
    return f"Upgrade: {name}"


VICTORY = "Victory"

MAX_ITEMS_PER_MACHINE = 5

# Game areas, in story order. Monument Island has no vending machine.
AREAS = ["Town Center", "Monument Island", "Battleship Bay", "Finkton", "Emporia", "Hand of the Prophet"]
SHOP_AREAS = [area for area in AREAS if any(a == area for a, _, _ in [*DOLLAR_BILLS, *UPGRADE_MACHINES])]
VOXOPHONE_AREAS = [area for area in AREAS if any(v[0] == area for v in VOXOPHONES)]
INFUSION_AREAS = [area for area in AREAS if any(i[0] == area for i in INFUSIONS)]
GEAR_AREAS = [area for area in AREAS if any(g[0] == area for g in GEAR_PICKUPS)]


def shop_location_name(area: str, number: int, item: int) -> str:
    return f"{area} Dollar Bill {number} - Item {item}"


def voxophone_location_name(title: str, place: str) -> str:
    return f"Voxophone - {title} ({place})"


def numbered_location_names(kind: str, pickups: list[tuple[str, str, str]]) -> list[str]:
    """Names of placed objects (area, id, place), in order: "<kind> - <place>", numbered when
    several share a place."""
    places = [place or area for area, _, place in pickups]
    seen: dict[str, int] = {}
    names = []
    for place in places:
        seen[place] = seen.get(place, 0) + 1
        names.append(f"{kind} - {place} {seen[place]}" if places.count(place) > 1 else f"{kind} - {place}")
    return names


def infusion_location_names() -> list[str]:
    return numbered_location_names("Infusion", INFUSIONS)


item_name_to_id: dict[str, int] = {
    name: BASE_ID + index for index, name in enumerate([*UNLOCKS, *SILVER_EAGLES], start=1)
}
# From BASE_ID + 100 on: additions don't shift the existing items.
item_name_to_id[INFUSION] = BASE_ID + 110
item_name_to_id[TEARS] = BASE_ID + 130
item_name_to_id[LOCKPICKING] = BASE_ID + 140
item_name_to_id.update({name: BASE_ID + 131 + i for i, (name, _, _) in enumerate(PASSES)})
item_name_to_id.update({vigor_item_name(name): BASE_ID + 120 + i for i, (_, name, _, _, _) in enumerate(VIGORS)})
# Gear: BASE_ID + 200 + index, in the order of GEAR_ITEMS (the game's effects order).
item_name_to_id.update({gear_item_name(name): BASE_ID + 200 + i for i, (name, _, _) in enumerate(GEAR_ITEMS)})
# Traps: BASE_ID + 400 + index.
item_name_to_id.update({name: BASE_ID + 400 + i for i, name in enumerate(TRAPS)})
# Upgrades: BASE_ID + 300 + index, in the order of UPGRADES.
item_name_to_id.update({upgrade_item_name(name): BASE_ID + 300 + i for i, (name, _, _) in enumerate(UPGRADES)})

SHOP_LOCATIONS: list[tuple[str, str, int]] = [
    (shop_location_name(area, number, item), path, item)
    for area, number, path in DOLLAR_BILLS
    for item in range(1, MAX_ITEMS_PER_MACHINE + 1)
]

# Voxophones: (location name, area, recording id in the game).
VOXOPHONE_LOCATIONS: list[tuple[str, str, str]] = [
    (voxophone_location_name(title, place), area, recording) for area, recording, title, _, place in VOXOPHONES
]

location_name_to_id: dict[str, int] = {
    name: BASE_ID + index for index, (name, _, _) in enumerate(SHOP_LOCATIONS, start=1)
}
# From BASE_ID + 1000 on, in the order of the game's ids: a location keeps its number even if the
# list changes order.
VOXOPHONE_BASE_ID = BASE_ID + 1000
location_name_to_id.update(
    {
        name: VOXOPHONE_BASE_ID + index
        for index, (name, _, _) in enumerate(sorted(VOXOPHONE_LOCATIONS, key=lambda v: v[2].lower()), start=1)
    }
)

# Infusions: (location name, area, bottle id in the game), from BASE_ID + 2000 on, in the order of
# the game's ids.
INFUSION_LOCATIONS: list[tuple[str, str, str]] = [
    (name, area, ident) for name, (area, ident, _) in zip(infusion_location_names(), INFUSIONS)
]
INFUSION_BASE_ID = BASE_ID + 2000
location_name_to_id.update(
    {
        name: INFUSION_BASE_ID + index
        for index, (name, _, _) in enumerate(sorted(INFUSION_LOCATIONS, key=lambda i: i[2].lower()), start=1)
    }
)

# Vigors: BASE_ID + 3000 + index.
VIGOR_LOCATIONS: list[tuple[str, str, str]] = [
    (vigor_location_name(name, place), area, key) for key, name, area, place, _ in VIGORS
]
location_name_to_id.update({name: BASE_ID + 3000 + i for i, (name, _, _) in enumerate(VIGOR_LOCATIONS)})

# Gear packages: (location name, area, package id in the game), from BASE_ID + 4000 on, in the
# order of the game's ids.
GEAR_LOCATIONS: list[tuple[str, str, str]] = [
    (name, area, ident) for name, (area, ident, _) in zip(numbered_location_names("Gear", GEAR_PICKUPS), GEAR_PICKUPS)
]
location_name_to_id.update(
    {
        name: BASE_ID + 4000 + index
        for index, (name, _, _) in enumerate(sorted(GEAR_LOCATIONS, key=lambda g: g[2].lower()), start=1)
    }
)

# Upgrade machines: (area, number in the area for this kind, kind, paths). Archipelago rows are sold
# there like in the Dollar Bills; location BASE_ID + 5000 + 10 * index + row.
UPGRADE_SHOPS: list[tuple[str, int, str, list[str]]] = []
for _area, _kind, _paths in UPGRADE_MACHINES:
    UPGRADE_SHOPS.append(
        (_area, sum(1 for a, _, k, _ in UPGRADE_SHOPS if a == _area and k == _kind) + 1, _kind, _paths)
    )


def upgrade_shop_location_name(area: str, kind: str, number: int, item: int) -> str:
    return f"{area} {kind} {number} - Item {item}"


location_name_to_id.update(
    {
        upgrade_shop_location_name(area, kind, number, item): BASE_ID + 5000 + 10 * index + item
        for index, (area, number, kind, _) in enumerate(UPGRADE_SHOPS)
        for item in range(1, MAX_ITEMS_PER_MACHINE + 1)
    }
)

# Story checkpoints: (location name, area, game step, chapter of the "Load Chapter" menu?).
# "Chapter - <title>" or "Checkpoint - <title>", numbered if the title repeats; location
# BASE_ID + 6000 + index in CHECKPOINTS.
CHECKPOINT_LOCATIONS: list[tuple[str, str, str, bool]] = []
for _chapter, _area, _step, _title in CHECKPOINTS:
    _base = f"{'Chapter' if _chapter else 'Checkpoint'} - {_title}"
    _same = [t for c, _, _, t in CHECKPOINTS if t == _title and c == _chapter]
    _name = (
        f"{_base} {sum(1 for n, *_ in CHECKPOINT_LOCATIONS if n.startswith(_base + ' ')) + 1}"
        if len(_same) > 1
        else _base
    )
    CHECKPOINT_LOCATIONS.append((_name, _area, _step, _chapter))
location_name_to_id.update({name: BASE_ID + 6000 + i for i, (name, _, _, _) in enumerate(CHECKPOINT_LOCATIONS)})

location_name_groups: dict[str, set[str]] = {
    area: {
        shop_location_name(a, n, k)
        for a, n, _ in DOLLAR_BILLS
        if a == area
        for k in range(1, MAX_ITEMS_PER_MACHINE + 1)
    }
    | {name for name, a, _ in VOXOPHONE_LOCATIONS if a == area}
    | {name for name, a, _ in INFUSION_LOCATIONS if a == area}
    | {name for name, a, _ in VIGOR_LOCATIONS if a == area}
    | {name for name, a, _ in GEAR_LOCATIONS if a == area}
    | {name for name, a, _, _ in CHECKPOINT_LOCATIONS if a == area}
    | {
        upgrade_shop_location_name(a, k, n, i)
        for a, n, k, _ in UPGRADE_SHOPS
        if a == area
        for i in range(1, MAX_ITEMS_PER_MACHINE + 1)
    }
    for area in AREAS
}
location_name_groups["Vigors"] = {name for name, _, _ in VIGOR_LOCATIONS}
location_name_groups["Voxophones"] = {name for name, _, _ in VOXOPHONE_LOCATIONS}
location_name_groups["Infusions"] = {name for name, _, _ in INFUSION_LOCATIONS}
location_name_groups["Gear"] = {name for name, _, _ in GEAR_LOCATIONS}
location_name_groups["Chapters"] = {name for name, _, _, chapter in CHECKPOINT_LOCATIONS if chapter}
location_name_groups["Checkpoints"] = {name for name, _, _, _ in CHECKPOINT_LOCATIONS}
location_name_groups["Upgrade Machines"] = {
    upgrade_shop_location_name(a, k, n, i) for a, n, k, _ in UPGRADE_SHOPS for i in range(1, MAX_ITEMS_PER_MACHINE + 1)
}
location_name_groups["Dollar Bills"] = {name for name, _, _ in SHOP_LOCATIONS}
item_name_groups: dict[str, set[str]] = {
    "Gear": {gear_item_name(name) for name, _, _ in GEAR_ITEMS},
    "Vigors": {vigor_item_name(name) for _, name, _, _, _ in VIGORS},
    "Upgrades": {upgrade_item_name(name) for name, _, _ in UPGRADES},
    "Traps": set(TRAPS),
    "Passes": {name for name, _, _ in PASSES},
}
