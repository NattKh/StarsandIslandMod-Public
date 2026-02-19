# Starsand Island Mod

A MelonLoader mod template for **Starsand Island** (Unity IL2CPP). This game uses Unity IL2CPP, and MelonLoader's built-in IL2CPP assembly generator can fail on newer Unity versions. This project provides workarounds.

## Prerequisites

- **.NET 6.0 SDK** – [Download](https://dotnet.microsoft.com/download/dotnet/6.0)
- **MelonLoader** – Already installed in your game folder
- **Starsand Island** – Installed via Steam

## Quick Start

### 1. Build the Mod

```powershell
cd "c:\Program Files (x86)\Steam\steamapps\common\StarsandIsland\StarsandIslandMod"
dotnet build -c Release
```

The built mod will be in `StarsandIsland\Mods\StarsandIslandMod\`.

### 2. Run the Game

Launch Starsand Island. The mod should load and log to the MelonLoader console.

---

## If MelonLoader's IL2CPP Generator Failed

If you see errors like "Failed to generate Il2Cpp interop assemblies" when starting the game:

### Option A: Try MelonLoader Config Tweaks

Edit `UserData\Loader.cfg` and set:

```ini
[unityengine]
# Force offline generation (don't contact remote API)
force_offline_generation = true
# Force regeneration on next launch
force_regeneration = true
```

Or try launching with: `--melonloader.agfoffline` and `--melonloader.agfregenerate`

### Option B: Generate DLLs with External Tools

Run the provided script to download and run **Cpp2IL** and **Il2CppDumper**:

```powershell
cd "c:\Program Files (x86)\Steam\steamapps\common\StarsandIsland\StarsandIslandMod"
.\GenerateDLLs.ps1 -All
```

This creates:
- **Cpp2IL** → `Generated\cpp2il_out\` – Decompiled DLLs. Open in **DnSpy** to explore game code.
- **Il2CppDumper** → `Generated\Il2CppDumper\` – Dummy DLLs + `il2cpp.h` for **Ghidra** reverse engineering.

### Option C: Force Unity Version (Cpp2IL)

If Cpp2IL fails with version detection, try:

```powershell
# After extracting Cpp2IL, run manually:
Cpp2IL.exe --game-path "C:\...\StarsandIsland" --output-path ".\Generated\cpp2il_out" --just-give-me-dlls-asap-dammit --force-unity-version 6000f1
```

Try `6000f1`, `2022f1`, or `2023f1` depending on the game's Unity version.

---

## Project Structure

```
StarsandIslandMod/
├── StarsandIslandMod.csproj   # Project file
├── StarsandIslandMod.cs       # Mod source
├── GenerateDLLs.ps1          # DLL generation script
├── README.md                  # This file
└── Generated/                 # Created by GenerateDLLs.ps1
    ├── cpp2il_out/            # Cpp2IL output
    └── Il2CppDumper/          # Il2CppDumper output
```

## Extending the Mod

1. **Run `GenerateDLLs.ps1 -Cpp2IL`** to get game DLLs.
2. **Open `Generated\cpp2il_out\Assembly-CSharp.dll`** in DnSpy to find classes and methods.
3. Add a reference in `.csproj`:
   ```xml
   <Reference Include="Assembly-CSharp">
     <HintPath>Generated\cpp2il_out\Assembly-CSharp.dll</HintPath>
     <Private>false</Private>
   </Reference>
   ```
4. Use **Harmony** (0Harmony) to patch game methods. Example:
   ```csharp
   [HarmonyPatch(typeof(SomeGameClass), "SomeMethod")]
   class Patch { ... }
   ```

## Useful Game Assemblies (from ScriptingAssemblies.json)

- `Assembly-CSharp.dll` – Main game logic
- `GameWorld.dll` – World/simulation
- `GameFramework.dll` – Core framework
- `UI.dll` – UI code

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Build fails: "MelonLoader not found" | Ensure path in `.csproj` matches your game install. |
| Game crashes on launch | Disable the mod (remove from Mods folder) and check MelonLoader logs. |
| Cpp2IL/Il2CppDumper fails | Starsand Island may use Unity 6; tools may need updates. Try Il2CppInspectorRedux as alternative. |
| No Mods folder | Create `StarsandIsland\Mods\` – MelonLoader creates it on first run. |

## Tools in StarsandIslandMod\Tools\

| Tool | Purpose |
|------|---------|
| **Il2CppInspectorRedux** (2026.1) | Newest - supports metadata v104 (Unity 6000.5). Run `RunIl2CppInspector.ps1` |
| **Il2CppDumper-agmbk** | Fork with v38+ metadata support. Use with decrypted metadata. |
| **Il2CppMetadataExtractor** | Frida script to dump decrypted metadata from running game |
| **Cpp2IL** | Via `GenerateDLLs.ps1 -Cpp2IL` - decompiles without metadata |

**Note:** Starsand Island's metadata appears encrypted. Il2CppInspectorRedux and Il2CppDumper both fail with "metadata not valid". You must dump decrypted metadata from memory (Frida) first, then run the dumpers.

## Links

- [MelonLoader](https://melonloader.co/)
- [Il2CppInspectorRedux](https://github.com/LukeFZ/Il2CppInspectorRedux) - 2026.1
- [Cpp2IL](https://github.com/SamboyCoding/Cpp2IL)
- [Il2CppDumper](https://github.com/Perfare/Il2CppDumper)
- [DnSpy](https://github.com/dnSpy/dnSpy)
