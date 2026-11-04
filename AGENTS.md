# Instructions for AI coding agents

Read [CONTRIBUTING.md](CONTRIBUTING.md) first: its rules apply to you. This file adds what matters
when an agent works on the repo.

## Project

- `mod/`: a 32-bit Windows DLL (`xinput1_3.dll` proxy) loaded by BioShock Infinite (Steam build of
  2022-05-11, Unreal Engine 3). It hooks the game, drives it through UnrealScript calls and memory
  reads, and contains the Archipelago client (`ap_client.cpp`, WebSocket in `ap_net.cpp`).
- `apworld/bioshock_infinite/`: the Archipelago world and its tests. The client in the DLL only
  knows what the world puts in slot_data.
- `tools/`: scripts that read the installed game's packages to generate tables and images.

## Commands

```powershell
.\build.ps1           # build the DLL, the loop test and the apworld
.\build.ps1 -Lint     # every check of the CI (pre-commit on all files)
.\build.ps1 -Format   # fix formatting
.\build.ps1 -Test     # loop test (needs Archipelago installed, or a source checkout as -ArchipelagoDir)
python apworld/run_tests.py <Archipelago source checkout> [--general]   # apworld tests
```

pre-commit only sees the files git tracks: for new files, run
`pre-commit run --files <files>` too.

## Before saying a change is done

1. `.\build.ps1 -Lint` passes and the build has no warning.
2. `.\build.ps1 -Test` passes when the client, the apworld or a mod command changed.
3. The apworld tests pass when the apworld changed.
4. Changes to the game side are only verified once someone plays the affected part: say clearly
   what was not tested in game instead of claiming it works.

## Do

- Read the module header (`mod/src/*.h`) before changing a module: each one documents its commands,
  its threading and the game behavior it relies on.
- Keep new game addresses in `mod/src/game_build.h`, with how they were found.
- Match the surrounding code: naming, comment density, error handling.
- Keep item and location ids stable; bump `world_version` when generation or slot_data change.
- Treat names that come from the game (classes, functions, maps, checkpoints) as exact strings,
  even misspelled ones: never "correct" them, the mod looks them up by name.

## Don't

- Don't edit generated files (`apworld/bioshock_infinite/{checkpoints,gear,infusions,machines,upgrades,voxophones}.py`,
  `mod/src/ap_{icon,emblem,screamer}.h`): change the tool and regenerate.
- Don't touch `mod/extern/` (third-party code) or add dependencies without asking.
- Don't run tools that rewrite files on their own (spell checkers in fix mode, mass replacements)
  without a way to review every change: a "fixed" game identifier breaks the mod silently.
- Don't commit game files, logs, `BioShockAP.ini` or `BioShockAP_state.json`.
- Don't commit, push, tag or publish unless asked.
- Don't take screenshots of the user's desktop or drive the game's window; ask the user to test in
  game and describe what they see.
