# Starsand Island Mod (Clean)

Standalone mod for **Starsand Island** (Unity IL2CPP). No MelonLoader or BepInEx. Build `StandaloneMod/version.dll` and copy it next to `StarsandIsland.exe`.

## Hotkeys

| Hotkey | Description |
|--------|-------------|
| **F1** | Enable Dev Mode (may be needed for GM Tool) |
| **F2** | Give GM Tool |
| **F4** | Toggle Time Control (turn on to control time; togglable so you don't accidentally skip time) |
| **F6** | Toggle Bomb Arrow Boost (works with all arrows) |
| **PgUp** | Speed up time (Time Control must be on) |
| **PgDn** | Slow down time (Time Control must be on) |
| **Right** | Advance +1 hour (Time Control must be on) |
| **Left** | Rewind -1 hour (Time Control must be on) |
| **F7 / F8** | Toggle Super Free Build (unlock all building parts, no cost build, other QoL) |
| **F10** | Toggle Resource Bypass (bypass certain resource requirements, e.g. bag upgrades) |
| **F11** | Fly Mode (UP/DOWN to move; helpful for getting unstuck if undermap) |
| **F12** | Force Build Mode (increased range, no height restriction, build anywhere outside plot) |
| **HOME** | Hide/Show console (if you don't see the console in-game, press HOME and it opens in another window) |

## Build

```powershell
cd StandaloneMod
.\build.bat
```

Copy `StandaloneMod\version.dll` into the game folder (next to `StarsandIsland.exe`).

## Troubleshooting

- **No console** – Ensure `version.dll` is in the game folder. Add an antivirus exception if needed.
- **Game crashes** – Restore original `version.dll` and rebuild.
- **Hotkeys do nothing** – Wait for "Ready!" in the console; some features require being in-game.
