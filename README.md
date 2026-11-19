# BioShock Infinite Archipelago

An [Archipelago](https://archipelago.gg) multiworld randomizer for BioShock Infinite. It has two
parts: a game mod with the Archipelago client built in, and the apworld that generates games.

This is a beta (0.1): expect rough edges. When something goes wrong, please
[open an issue](../../issues/new/choose) with your `BioShockAP.log`.

## What changes in the game

- The vending machines (Dollar Bill, Veni! Vidi! Vigor!, Minuteman's Armory) sell items from the
  multiworld. Their usual wares (health, salts, ammo) stay locked until someone sends you the
  matching unlock.
- Voxophones, Infusion bottles, Gear packages, Vigors and story chapters can be checks too.
- You receive those unlocks, Infusions (chosen on the game's own screen), Vigors, Vigor and weapon
  upgrades, Gear pieces, Silver Eagles and a few traps.
- Optionally, Elizabeth's Tears, her lockpicking and the way to Monument Island and Finkton are
  locked behind items as well.
- You log in from the game's main menu, short messages at the bottom of the screen tell you what
  was sent and received, and DeathLink is supported.

Every option is described on the [game page](apworld/bioshock_infinite/docs/en_BioShock%20Infinite.md).

## Requirements

- Windows and BioShock Infinite on Steam, up to date. Other versions (GOG, Epic, older builds) are
  not supported: the mod detects them and stays inactive. Linux and Steam Deck are untested.
- [Archipelago](https://github.com/ArchipelagoMW/Archipelago/releases) 0.6.7 or newer.

## Install

1. Download `xinput1_3.dll`, `bioshock_infinite.apworld` and `BioShockInfinite.yaml` from the
   [latest release](../../releases/latest).
2. Copy `xinput1_3.dll` into `<Steam>\steamapps\common\BioShock Infinite\Binaries\Win32\`.
   To uninstall, delete it.
3. Double-click `bioshock_infinite.apworld` to add it to Archipelago (or copy it into
   Archipelago's `custom_worlds` folder). Only whoever generates the game needs it.
4. Edit `BioShockInfinite.yaml` (your slot name and options) and generate the multiworld as usual.

## Play

1. Start the game. On the main menu, select **ARCHIPELAGO**.
2. Enter `slot@server:port` (for example `Booker@archipelago.gg:38281`) and the room password, if
   there is one, then log in.
3. Start a new game or load one.

The game remembers your login and connects by itself at the next start (untick AUTO-CONNECT on the
login screen to turn that off). If the connection drops, the mod reconnects on its own, and what
you buy or pick up meanwhile is sent once it is back.

There is no chat in the game: for chat and commands such as `!hint`, connect Archipelago's Text
Client to the same slot. The [setup guide](apworld/bioshock_infinite/docs/setup_en.md) has the
details.

## Troubleshooting

- **No ARCHIPELAGO button on the main menu.** Open `BioShockAP.log`, next to the DLL. "unknown
  build" means the game is not the current Steam version; no log at all means the DLL is not in
  `Binaries\Win32`.
- **Cannot connect.** Check the slot name, the address and the port of the room, and that the
  room is still open. The login screen shows the server's answer.
- **Antivirus warning.** The DLL hooks into the game, which some antivirus programs dislike. Every
  release is built from this repository's sources by GitHub Actions; you can also build it
  yourself (below).

## Build from source

Windows with Visual Studio 2022 (C++ desktop workload), CMake 3.20+ and Python 3.11+:

```powershell
python -m venv .venv
.venv\Scripts\python -m pip install -r requirements-dev.txt
.\build.ps1            # mod\bin\Release\xinput1_3.dll and apworld\build\bioshock_infinite.apworld
.\build.ps1 -Install   # also copies them into the game and into Archipelago
```

[CONTRIBUTING.md](CONTRIBUTING.md) explains the tests, the debug mode and the project's rules.

## License

MIT, see [LICENSE](LICENSE). Third-party code is listed in [THIRD_PARTY.md](THIRD_PARTY.md).
BioShock Infinite is a trademark of Take-Two Interactive. This project is not affiliated with
Take-Two, 2K or Irrational Games.
