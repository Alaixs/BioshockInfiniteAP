# BioShock Infinite Setup Guide

## Required software

- Windows and BioShock Infinite on Steam, up to date (build of 2022-05-11). Other versions (GOG,
  Epic, older builds) are not supported: the mod detects them and stays inactive.
- The mod, `xinput1_3.dll`, from the
  [latest release](https://github.com/Alaixs/BioshockInfiniteAP/releases/latest).
- Archipelago 0.6.7 or newer, with `bioshock_infinite.apworld` from the same release in its
  `custom_worlds` folder (double-click the file to install it). Only needed to generate games.

## Installing the mod

Copy `xinput1_3.dll` into `<Steam>\steamapps\common\BioShock Infinite\Binaries\Win32\`.
To uninstall, delete it. The mod writes `BioShockAP.log` next to it.

## Connecting

The Archipelago client is built into the mod: no separate client is needed.

1. Start BioShock Infinite. On the main menu, press the button shown next to **ARCHIPELAGO**.
2. In the first field, enter `slot name@server:port`, for example `Booker@archipelago.gg:38281`.
   The port defaults to 38281. Enter the room password in the second field, if there is one.
   Ctrl+V pastes into the selected field.
3. Select **LOG IN**. The main menu then shows **CONNECTED: your slot name**.

The address and password are remembered, and the game logs in with them by itself when it starts.
To turn that off, press the button shown next to **[X] AUTO-CONNECT** on the login screen (or click
it). If the connection drops, the mod tries again every few seconds. Anything you buy or pick up
while disconnected is sent once you are back online.

## Playing

Load your game and visit a Dollar Bill vending machine: the Archipelago items are listed below its
usual wares.

There is no chat in the game. For chat and server commands (`!hint`, `!release`...), open the
**Text Client** from the Archipelago Launcher and connect it to the same slot while you play.

## Troubleshooting

- No ARCHIPELAGO button: open `BioShockAP.log`. "unknown build" means the game is not the current
  Steam version; no log at all means the DLL is not in `Binaries\Win32`.
- To report a problem, [open an issue](https://github.com/Alaixs/BioshockInfiniteAP/issues) with
  your `BioShockAP.log`.
