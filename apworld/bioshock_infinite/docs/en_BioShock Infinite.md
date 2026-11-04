# BioShock Infinite

## What does randomization do to this game?

Dollar Bill vending machines become Archipelago shops. Each machine sells its own Archipelago items
(shown with their real name and recipient), priced in Silver Eagles. Your own
traps are never sold in your own machines, nor your own Silver Eagles (except, rarely, when nothing
else can take their place).

The machines' own wares (health kits, salts and every ammo type) are locked until you receive the
matching unlock item from the multiworld.

With `items_per_upgrade_machine` (1 by default), Veni! Vidi! Vigor! and Minuteman's Armory machines
sell Archipelago items too, and their Vigor and weapon upgrades become Archipelago items: you get
an upgrade the moment you receive it, and the machines no longer sell them.

Voxophones (audio logs) and Infusion bottles are locations too (options `voxophone_areas` and
`infusion_areas`). Picking up an Infusion bottle no longer opens the upgrade choice: the bottle is
taken and its Archipelago item is sent instead.

Gear packages (hats, shirts, pants and boots) are locations with `gear_areas`: picking one up
takes it without giving you a piece and sends its Archipelago item. The 10 Season Pass packages of
the Blue Ribbon are not locations.

With `checkpoint_checks`, reaching a point of the story sends a check: the 37 chapters of the Load
Chapter menu by default (`chapters`), or every autosave checkpoint of the story (`all`, 99).

With `randomize_vigors`, Devil's Kiss, Murder of Crows, Bucking Bronco, Charge, Undertow and Return
to Sender are Archipelago items: the first time the game gives you one (bottle, enemy drop...), it
is taken away and its location is checked; you get the Vigor (with the game's tutorial) when you
receive its item. Possession and Shock Jockey are given by the story as usual: it needs them.

With `lock_tears` (on by default), Elizabeth can't open Tears (cover, weapons, Sky-Hooks, allies...)
until you receive the Elizabeth's Tears item: the use key on a Tear only shows a message. The
logic places that item before Battleship Bay, where the first Tears appear. Tears the story opens by
itself are not affected.

With `lock_lockpicking` (on by default), Elizabeth can't pick the locked doors and safes that cost
lockpicks until you receive the Lockpicking item: using one only shows a message. Free locks on the
story path are not affected. The logic places that item before Battleship Bay.

With `area_passes` (on by default), the gondola from Town Center to Monument Island and the First
Lady airship to Finkton only take you once you receive the Monument Island Pass and the Finkton Pass:
until then, choosing to leave only shows a message. The logic places each pass before its area.
Battleship Bay, Emporia and the Hand of the Prophet have no pass, because the game takes you there
through a cutscene; the two passes still stand in front of them in the story.

## What is the goal?

With `goal: siphon` (default), reach the end of the story: the goal is complete when Songbird
destroys the Siphon on Monument Island. With `goal: shop`, buy every Archipelago item sold in the
Dollar Bill machines.

## Which items can be in another player's world?

- Unlocks for each Dollar Bill ware: Health Kit, Small Health Kit, Salts, Small Salts and the ammo
  of each weapon.
- Infusions: when you receive one, the game's own Infusion screen opens and you choose Health,
  Shield or Salts. If it can't open right now (at the very start of Town Center the game hasn't
  loaded the upgrades yet), a message at the bottom of the screen lets you choose with F9, F10 or
  F11, and the upgrade is applied as soon as it can be.
- Elizabeth's Tears (`lock_tears`): Elizabeth can open Tears.
- Lockpicking (`lock_lockpicking`): Elizabeth can pick the locks that cost lockpicks.
- Monument Island Pass and Finkton Pass (`area_passes`): the way to these areas opens.
- Vigors (Devil's Kiss, Murder of Crows, Bucking Bronco, Charge, Undertow, Return to Sender).
- Vigor and weapon upgrades (62), applied as soon as you receive them. Where the current level hasn't
  loaded an upgrade yet (Town Center, parts of Emporia), it is applied in the next level that has it.
- Gear pieces, one per Gear package in the game (no duplicates): received straight into your Gear
  inventory, in any level.
- Bundles of Silver Eagles.
- Traps (`trap_percentage` of the Silver Eagles, 10% by default), triggered as soon as you receive
  them: Disarm (no weapon, Vigor or melee for 20 s), Jammed Weapons (no gunfire for 30 s), Vigor
  Lock (no Vigor for 30 s), Elizabeth Strike (she tosses you nothing and opens no Tear for 60 s) and
  Walk Only (no sprint or jump for 30 s).
- With `boys_of_silence_trap` (off by default; **content warning: jump scare and gore**), the Boys
  of Silence trap: their deafening scream with a bloody full-screen image.

## What does another world's item look like in BioShock Infinite?

It appears as a new row in the vending machines, named after the item and its recipient, with the
Archipelago logo.

## Is DeathLink supported?

Yes, with the `death_link` option. When Booker dies, everyone else using DeathLink dies too; when
another player dies, Booker dies (as soon as he is in game, not during menus or loading screens).

## How do I know what happened in the multiworld?

Short messages appear at the bottom of the screen, like the game's own tutorial hints, in the game's
language: items you send to other players, items other players send you, what a Voxophone, an
Infusion bottle or a Gear package contained, DeathLink deaths (shown after you respawn, with their
cause), goal completion and connection loss.
