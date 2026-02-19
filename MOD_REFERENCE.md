# Starsand Island Mod - Technical Reference & Knowledge Base

## Last Updated
February 2026 - v3.5 (Added Unlock All Cosmetics: F5)

---

## 1. Game Architecture Overview

**Engine:** Unity 6 (IL2CPP backend, NOT Mono)
**Platform:** Windows x64, Steam
**Game Path:** `C:\Program Files (x86)\Steam\steamapps\common\StarsandIsland\`
**Game Executable:** `StarsandIsland.exe`
**IL2CPP Runtime:** `GameAssembly.dll` (contains all compiled C# game code as native)
**Metadata:** `StarsandIsland_Data\il2cpp_data\Metadata\global-metadata.dat` (encrypted at rest, decrypted in memory at runtime)

### Key Files
| File | Purpose |
|------|---------|
| `GameAssembly.dll` | IL2CPP compiled game code |
| `StarsandIsland.exe` | Unity player launcher |
| `version.dll` | Our proxy DLL mod (loaded automatically by Windows DLL search order) |
| `SDKDLL/` | Decompiled C# source from IL2CPP (our reverse-engineering reference) |

### Why Not Standard Mod Loaders
Unity 6 + IL2CPP breaks compatibility with BepInEx and MelonLoader. The game uses encrypted metadata which further complicates things. A standalone native DLL proxy (`version.dll`) is the only reliable approach.

---

## 2. Mod Architecture

### version.dll Proxy Pattern
Windows searches for `version.dll` in the application directory before `System32`. Our custom `version.dll`:
1. Exports all functions from the real `version.dll` (forwards to `C:\Windows\System32\version.dll`)
2. Runs mod initialization code in `DllMain(DLL_PROCESS_ATTACH)`
3. Spawns background threads for IL2CPP interaction and hotkey monitoring

### Files
| File | Purpose |
|------|---------|
| `StarsandIslandMod/StandaloneMod/mod_main.c` | Main mod source (single C file) |
| `StarsandIslandMod/StandaloneMod/il2cpp_api.h` | IL2CPP function pointer types and global struct |
| `StarsandIslandMod/StandaloneMod/version_proxy.h` | DLL export forwarding declarations |
| `StarsandIslandMod/StandaloneMod/version.def` | Module definition for DLL exports |

### Build Command
```
cl /nologo /O2 /W3 /LD /MD /DWIN32 /D_WINDOWS /DNDEBUG mod_main.c /Fe:version.dll /link /DEF:version.def /DLL user32.lib kernel32.lib psapi.lib
```
Requires Visual Studio x64 tools (`vcvarsall.bat x64`).

### Thread Model
The mod uses **3 threads**:

1. **Hotkey Thread** (`HotkeyThread`) - Polls `GetAsyncKeyState` for F-key presses, dispatches commands
2. **IL2CPP Worker Thread** (`IL2CppWorkerThread`) - Executes IL2CPP API calls that don't touch Unity UI (DevMode, GM Tool, DLC bypass, Free Build). Must call `il2cpp_thread_attach` to register with IL2CPP's GC.
3. **Main Thread** (Unity's) - Used for UI operations via window subclass + PostMessage pattern

### CRITICAL: Thread Safety Rules
- **IL2CPP GC**: Any thread calling IL2CPP APIs must first call `il2cpp_thread_attach(il2cpp_domain_get())`. Failure causes "Collecting from unknown thread" crash.
- **Unity UI**: ALL UI operations (opening/closing views, showing panels, coroutines) MUST run on Unity's main thread. Calling from other threads causes game freeze/deadlock.
- **Solution for UI calls**: Window subclass pattern (see Section 6).

---

## 3. IL2CPP API Usage

### Resolving Functions
All IL2CPP functions are resolved dynamically from `GameAssembly.dll` via `GetProcAddress`:

```c
IL2CPP.domain_get = (fn_il2cpp_domain_get)GetProcAddress(hDll, "il2cpp_domain_get");
IL2CPP.class_from_name = ...;
IL2CPP.runtime_invoke = ...;
// etc.
```

### Key IL2CPP Functions Used
| Function | Purpose |
|----------|---------|
| `il2cpp_domain_get()` | Get the IL2CPP app domain |
| `il2cpp_thread_attach(domain)` | Attach current thread to GC |
| `il2cpp_domain_get_assemblies(domain, &count)` | List loaded assemblies |
| `il2cpp_assembly_get_image(asm)` | Get image from assembly |
| `il2cpp_class_from_name(image, namespace, name)` | Find a class by namespace+name |
| `il2cpp_class_get_method_from_name(klass, name, argCount)` | Find method by name and arg count |
| `il2cpp_runtime_invoke(method, instance, args[], &exception)` | Call a managed method |
| `il2cpp_class_get_field_from_name(klass, name)` | Get field info |
| `il2cpp_field_static_get_value(field, &value)` | Read static field |
| `il2cpp_field_static_set_value(field, &value)` | Write static field |
| `il2cpp_field_get_offset(field)` | Get instance field offset |
| `il2cpp_class_get_type(klass)` | Get Il2CppType from class |
| `il2cpp_type_get_object(type)` | Get System.Type (reflection object) from Il2CppType |
| `il2cpp_object_unbox(obj)` | Unbox a value type |
| `il2cpp_runtime_class_init(klass)` | Initialize static constructor |

### Finding Classes (FIND_CLASS macro)
```c
#define FIND_CLASS(ns, name) FindClassInAllAssemblies(ns, name)
```
Iterates all loaded assemblies to find a class. Try with the correct namespace first, then fallback to empty namespace `""`.

### Common Namespaces
| Namespace | Contains |
|-----------|----------|
| `"GameFramework.UGUI"` | UI system: KUIOpenManager, KUIViewManager, KUIViewBase, etc. |
| `"GameFramework.GameDebug"` | Debug system: KDebugModule, KDebugger, KEntityDebugWindow |
| `"GameFramework"` | Core: Singleton, GameFrameworkModule |
| `"UI"` | Game UI views: UICheatStoreView, KUIInventoryDebug, etc. |
| `"GameInput"` | Input: KInputAction, KInput |
| `"Inventory"` | Inventory system |
| `""` (empty) | Many classes; fallback when namespace doesn't match |

### Calling Instance Methods
```c
Il2CppObject* exc = NULL;
Il2CppObject* result = IL2CPP.runtime_invoke(method, instance, args, &exc);
```
- `instance`: the object pointer (NULL for static methods)
- `args`: array of `void*` pointers to arguments
  - Reference types (objects, strings): pass the pointer directly
  - Value types (int, bool, float): pass pointer to the value (`&myInt`)
  - NULL arguments: pass `NULL` in the array
- `exc`: receives thrown exception (NULL if none)

### Getting Singleton Instances
Most game managers use `Singleton<T>` or `SingletonMono<T>`:
```c
// Method 1: get_Instance property
Il2CppMethodInfo* getInst = IL2CPP.class_get_method_from_name(klass, "get_Instance", 0);
Il2CppObject* inst = IL2CPP.runtime_invoke(getInst, NULL, NULL, &exc);

// Method 2: static m_Instance field (on class or parent)
Il2CppFieldInfo* f = IL2CPP.class_get_field_from_name(klass, "m_Instance");
IL2CPP.field_static_get_value(f, &inst);
```

---

## 4. Game Framework - Key Classes & Systems

### Debug System (GameFramework.GameDebug)

#### KDebugModule (internal, GameFrameworkModule)
- `List<KDebugOption> m_DebugOptions` - All registered debug options (50+ items)
- `IDebugger CreateDebugger()` - Creates a KDebugger instance

#### KDebugger (internal, IDebugger)
- `DevMode` property (bool) - **THE master gate for debug features**
- Contains `List<KDebugInspector>` and `List<KDebugCommandGroup>`

#### KEntityDebugWindow (SingletonMono)
- `static bool DebugEnable` - Static flag to enable debug rendering
- `bool IsShow` - Whether debug window is visible
- `IDebugger m_Debugger` - Reference to the debugger instance

#### Debug Flag Activation Chain
To enable debug features, set ALL of these:
1. `KWorldUtil.IsDevStart = true` (static field)
2. `GameApp.Instance.IsDevStart = true` (instance field on singleton)
3. `KDebugModule.Instance → CreateDebugger() → KDebugger.DevMode = true`

### UI System (GameFramework.UGUI)

#### KUIOpenManager (Singleton<KUIOpenManager>)
**The primary way to open/close UI views.** Has registered openers for each view type.

| Method | Args | Notes |
|--------|------|-------|
| `Open(Type viewType)` | 1 (Type) | Synchronous open |
| `Close(Type viewType)` | 1 (Type) | Synchronous close |
| `OpenOrClose(Type, bool canOpen, bool canClose)` | 3 | **Best for toggling** |
| `OpenAsync(Type, object context, Action<object,KUIViewBase> finish)` | 3 | Async open |
| `DefaultOpen(Type)` | 1 | Opens with default settings |
| `DefaultClose(Type)` | 1 | Close with animation |
| `IsOpen(Type)` | 1 | Check if open |
| `SetOpener(Type, KUIViewOpener)` | 2 | Register custom opener |

Internally, `Open()` calls `InternalGetOpener(Type)` which looks up a `KUIViewOpener` from `m_Openers` dictionary. The opener handles `Create → Init → Show` lifecycle.

#### KUIViewManager (SingletonMono<KUIViewManager>)
Manages all active view instances.

| Method | Args | Notes |
|--------|------|-------|
| `GetUIView(Type)` | 1 | Get existing view instance (null if not created) |
| `AddUIView(Type)` | 1 | Create and add a new view |
| `AddUIViewAsync(Type, context, callback)` | 3 | Async creation |
| `RemoveAllUIView(KUIViewBase expect)` | 1 | Remove all except given |

Has `Dictionary<Type, KUIViewBase> m_UIViews` for type→instance lookup.

#### KUIViewBase (base class for all views)
| Member | Type | Notes |
|--------|------|-------|
| `Show()` | virtual method | Show the view |
| `Hide()` | virtual method | Hide the view |
| `InternalShow()` | protected internal | Low-level show |
| `InternalHide()` | protected internal | Low-level hide |
| `Init()` | internal | Initialize (calls OnInit) |
| `OnInit()` | protected virtual | Override point for init |
| `OnShow()` | protected virtual | Override point for show |
| `OnHide()` | protected virtual | Override point for hide |
| `Create(Type)` | static | Create new view instance |
| `Get(Type)` | static | Get existing instance |

#### KUIView<TSelf, TBinder> (generic view with singleton)
```csharp
public abstract class KUIView<TSelf, TBinder> : KUIView<TBinder>
    where TSelf : KUIView<TSelf, TBinder>
    where TBinder : MonoBehaviour
{
    private static TSelf m_Instance;
    public static TSelf Instance => ...;
}
```
Example: `UICheatStoreView : KUIView<UICheatStoreView, UICheatStore>`

#### KUIViewOpener / KUIPanelOpener
Handles the open/close lifecycle for views. `KUIPanelOpener` extends this with panel history tracking.

### Input System (GameInput)

#### KInputAction (generated input action class)
Contains all input action maps as nested structs:
- `OpenPanelActions` - Panel hotkeys (Inventory, Map, CheatStore, etc.)
- `DevActions` - Dev hotkeys (ToggleCursor, ToggleDevPanel, FocusEntity, etc.)
- `GPUDebugActions` - GPU debug overlay

Key fields for CheatStore:
- `InputAction m_OpenPanel_CheatStore` (private readonly)
- `OpenPanelActions.CheatStore` property returns the InputAction
- Actual keybinding is in Unity's serialized binary asset (not visible in decompiled code)

#### KInput (static accessor)
```csharp
public static KInputAction.OpenPanelActions OpenPanel => ...;
public static KInputAction.DevActions Dev => ...;
```

### Time System (GameTime / GameFramework)

#### CGlobalTimeSystem (namespace: GameTime)
- `KGameSystem<CGlobalTimeSystem>` with ISynchronizable
- **`static float GAME_TIME_SCALE`** -- the master game time multiplier
- `KGameTime Now` -- current game time
- `float TotalElapsedSeconds`, `long TotalFrame`, `float DeltaSeconds`
- `bool IsTimePausing()`, `IGameModifier GetPauseTimeModifier(string reason)`

#### KTimeScale (namespace: GameFramework)
Static class controlling engine time scale level:
- **`static float Level { get; set; }`** -- time scale level (set via `set_Level(1 arg)`)
- `static float m_Level` -- backing field (fallback direct write)
- `static bool IsPause { get; }`
- `static void Pause(object source)` / `Resume(object source)`
- `static void Reset()`

#### UnityEngine.Time
- **`static float timeScale { get; set; }`** -- Unity's built-in time scale
- Hooked by game via `TimeHook` class (delegates `HookGet_timeScale`, `HookSet_timeScale`)

#### Time Control Implementation
Sets ALL THREE values for reliable speed control:
1. `CGlobalTimeSystem.GAME_TIME_SCALE` (static field) -- game-level time
2. `KTimeScale.Level` (property/field) -- framework time scale
3. `UnityEngine.Time.timeScale` (property) -- Unity engine time

All set via PostMessage → main thread (same pattern as cheat store).
Speed steps: PAUSED(0x), 0.25x, 0.5x, 1.0x, 2.0x, 3.0x, 5.0x, 10.0x

### Item / Inventory System (Inventory namespace)

#### KItemTemplateSet (Singleton, extends JsonTemplateSet<KItemTemplateSet, ItemTemplate>)
Central registry of ALL item definitions in the game.
- `Dictionary<object, ItemTemplate> AllTemplates` -- all templates keyed by ID
- `Values` -- iterate all templates
- `GetTemplate(object primaryKey)` -- get by ID

#### ItemTemplate
- `string ID` -- unique item identifier
- `ItemType Type` -- item type (class, not enum)
- `ItemExtensionObj Extensions` -- DataObj<ItemExtension> (Dictionary<Type, ItemExtension>)
- Check `Extensions.ContainsKey(typeof(EquipmentItemExt))` to identify clothing/equipment

#### CGlobalItemSystem (KGameSystem singleton)
- `static GetCurrent()` -- get instance
- `KItem CreateItem(ItemTemplate template, int stack, int quality)` -- create item instance

#### KGridItemSet
- `Push(KItem item)` -- add item to container
- `Pop(int slotIndex, int count)` -- remove item
- Used by CInventory, CWardrobe, CInventoryOutfit

#### CWardrobe (Outfits namespace)
- `KGridItemSet ItemSet` -- item storage
- `Push(KItem item)` -- add to wardrobe
- Get via `KEcsEntity.GetComponent(typeof(CWardrobe))` on player entity

#### EquipmentItemExt (XSandbox namespace, extends ItemExtension)
- Present in Extensions of clothing/equipment ItemTemplates
- Has fields: Part (slot type), Gender, IsSuit, UseAsSwim, UseAsSleep, CanChangeColor

### Cheat Store (UI namespace)

#### UICheatStoreView : KUIView<UICheatStoreView, UICheatStore>
The cheat item spawner UI. Contains:
- `OnToggleCheatStore(InputAction.CallbackContext)` - Input handler
- `OnInit()` - Creates tabs per ItemType, initializes search
- `OnShow()` - Registers toggle callback
- `OnHide()` - Cleanup
- Uses coroutines (`ICoroutine`, `WaitForSeconds`) for async item loading

#### KUIInventoryDebug : KDebugOption
- `OpenCheatStore()` - Instance method to open cheat store
- Found at index ~35 in `KDebugModule.m_DebugOptions` list

#### KUIInventoryCommand.OpenCheatStore : KButtonDebugCommand<CInventory>
- Debug command that opens cheat store (requires debug window)

---

## 5. Current Mod Features (v3.3)

### Hotkeys
| Key | Command | Function |
|-----|---------|----------|
| F1 | Dev Mode | Sets KWorldUtil.IsDevStart, GameApp.IsDevStart, KDebugger.DevMode = true |
| F2 | GM Tool | Gives GM Tool inventory item (one-time, checks `g_gmToolGiven`) |
| F3 | DLC Unlock | Patches DLC check functions in memory (one-time, checks `g_dlcPatched`) |
| F4 | Time Control | Toggles time control on/off (restores 1.0x when off) |
| F5 | Unlock Cosmetics | Gives all equipment/clothing items to wardrobe (one-time) |
| PgUp | Speed Up | Increases time speed: 0→0.25→0.5→1→2→3→5→10x |
| PgDn | Slow Down | Decreases time speed: 10→5→3→2→1→0.5→0.25→0x (paused) |
| F8 | Free Build | Unlocks all construction templates via `SimpleUnlockAll()` |
| F9 | Cheat Store | Opens/closes UICheatStoreView via main thread dispatch |
| HOME | Console | Toggles console window visibility |

### DLC Bypass Technique
Patches the function prologue of DLC check methods with `mov al, 1; ret` (bytes: `B0 01 C3`):
```c
VirtualProtect(funcPtr, 3, PAGE_EXECUTE_READWRITE, &old);
memcpy(funcPtr, "\xB0\x01\xC3", 3);
VirtualProtect(funcPtr, 3, old, &old);
```

### GM Tool Item
Uses `il2cpp_runtime_invoke` to call `KItemSpawner.SpawnItem(template, quality)` or equivalent method to add the GM Tool item to the player's inventory.

---

## 6. Main Thread Dispatch Pattern (CRITICAL)

### The Problem
Unity UI operations (opening views, showing panels, running coroutines) MUST happen on Unity's main thread. Our mod runs IL2CPP calls from a worker thread. Calling UI methods from the wrong thread causes the game to freeze/deadlock -- the `Open(Type)` call returns successfully but the resulting `OnInit()` / `OnShow()` / coroutine execution deadlocks.

### The Solution: Window Subclass + PostMessage

This is the **proven working approach** as of v3.3:

```
[Hotkey Thread]                    [Unity Main Thread]
     |                                    |
     | PostMessage(WM_MOD_CHEATSTORE)     |
     |  ──────────────────────────────>   |
     |                                    | ModWndProc receives message
     |                                    | Calls IL2CPP Open/Close
     |                                    | OnInit/OnShow run correctly
     |                                    | View opens without freeze!
```

#### Implementation Steps:

1. **Find the game window:**
   ```c
   FindWindowA("UnityWndClass", NULL);
   // Fallback: EnumWindows with PID matching + IsWindowVisible
   ```

2. **Subclass the window (one-time):**
   ```c
   g_origWndProc = (WNDPROC)SetWindowLongPtrA(g_gameWnd, GWLP_WNDPROC, (LONG_PTR)ModWndProc);
   ```

3. **Custom WndProc intercepts our message:**
   ```c
   #define WM_MOD_CHEATSTORE (WM_APP + 0xBEEF)

   static LRESULT CALLBACK ModWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
       if (msg == WM_MOD_CHEATSTORE) {
           MainThread_CheatStoreAction((int)wParam);
           return 0;
       }
       return CallWindowProcA(g_origWndProc, hwnd, msg, wParam, lParam);
   }
   ```

4. **Dispatch from any thread:**
   ```c
   PostMessageA(g_gameWnd, WM_MOD_CHEATSTORE, (WPARAM)action, 0);
   // PostMessage is guaranteed cross-thread safe
   ```

5. **On main thread, call IL2CPP:**
   ```c
   // Best method: OpenOrClose(Type, bool, bool) with action=3
   Il2CppMethodInfo* toggleM = class_get_method_from_name(openMgr, "OpenOrClose", 3);
   int canOpen = 1, canClose = 1;
   void* args[3] = { typeObj, &canOpen, &canClose };
   runtime_invoke(toggleM, mgr, args, &exc);
   ```

#### Why SetTimer Doesn't Work
`SetTimer(hwnd, ...)` requires the calling thread to own the window. Our hotkey thread doesn't own the Unity window, so the timer silently fails to fire.

#### Why Direct IL2CPP Calls Freeze
Calling `KUIOpenManager.Open(Type)` from a non-main thread:
- The Open() call itself returns (it queues work)
- But the view's `OnInit()` runs coroutines and Unity API calls
- These try to access Unity internals from the wrong thread
- Deadlock occurs when waiting for main thread resources

### Reuse This Pattern
For ANY future UI operation from the mod, use the same pattern:
1. Define a new `WM_APP + N` message
2. `PostMessage` from any thread
3. Handle in `ModWndProc` on the main thread

---

## 7. SDK Reference Location

Decompiled game source is at:
```
C:\Program Files (x86)\Steam\steamapps\common\StarsandIsland\SDKDLL\
```

### Directory Structure
| Path | Contents |
|------|----------|
| `SDKDLL/GameFramework/GameFramework.UGUI/` | UI framework (KUIOpenManager, KUIViewBase, etc.) |
| `SDKDLL/GameFramework/GameFramework.GameDebug/` | Debug system (KDebugModule, KDebugger, etc.) |
| `SDKDLL/GameWorld/GameInput/` | Input system (KInputAction, KInput) |
| `SDKDLL/GameWorld/Construction/` | Building system |
| `SDKDLL/GameWorld/Inventory/` | Inventory system |
| `SDKDLL/GameWorld/DLCUtility/` | DLC checking |
| `SDKDLL/UI/UI/` | Game-specific UI views (UICheatStoreView, etc.) |

---

## 8. Common Pitfalls & Solutions

| Problem | Cause | Solution |
|---------|-------|----------|
| "Collecting from unknown thread" crash | Thread not attached to IL2CPP GC | Call `il2cpp_thread_attach(domain)` |
| Game freezes after UI call | UI method called from non-main thread | Use PostMessage + window subclass pattern |
| Method returns wrong overload | Generic vs non-generic with same name | Check arg count carefully; generics have 0 args |
| Exception 0x80000003 | IL2CPP breakpoint/assertion | Instance is NULL or method signature mismatch |
| GM Tool spawns duplicates | No guard against repeated calls | Use a `g_gmToolGiven` flag |
| DLC patched multiple times | No guard | Use `g_dlcPatched` flag |
| `version.dll` locked during build | Game still running | `taskkill /IM StarsandIsland.exe /F` before copy |
| Class not found | Wrong namespace | Try both correct namespace AND empty string `""` |
| Static field on parent class | Singleton pattern puts field on base class | Walk parent chain: `il2cpp_class_get_parent(klass)` |

---

## 9. Metadata Extraction Tools

For extracting the decrypted `global-metadata.dat` from memory:
- **Frida script:** `StarsandIslandMod/Tools/Frida/run-metadata-dump.bat`
- **x64dbg method:** `StarsandIslandMod/Tools/Il2CppMetadataExtractor/DUMP-WITH-X64DBG.md`
- **JS REPL script:** `StarsandIslandMod/Tools/Il2CppMetadataExtractor/dump-metadata-repl.js`

---

## 10. Useful Debug Classes in the Game

The game has an extensive debug system. Notable `KDebugOption` subclasses:
- `KUIInventoryDebug` - Cheat store access
- `KDLCDebugger` - DLC debug
- `KNavigationDebug` - Pathfinding debug
- `KLocationDebug` - Location/teleport debug
- `KWeatherDebugger` - Weather control
- `KTimeDebugOption` - Time manipulation
- `KMovementDebugOption` - Movement debug
- `KPetDebug` - Pet debug
- `KRegionDebug` - Region unlocking
- `KNetworkDebugOption` - Network debug
- `KProducerDebug` - Production/crafting debug
- `KScheduleAISRDebug` - NPC schedule debug

Notable `KDebugCommand` subclasses:
- `KUIInventoryCommand.OpenCheatStore` - Opens cheat store from debug window
- `KEntityCommand.CreateActor` - Spawn actors
- `KEntityCommand.CreatePrefab` - Spawn prefabs
- `KWorldObjectCommand.SwitchToLocation` - Teleport to location
- `KFarmCommand.UnLockAllRegion` - Unlock all farm regions
- `KCurrencyCommand.ModifyCurrency` - Add/remove money
- `KMissionCommand.AddMission` - Add missions
- `KNPCAffectionCommand.KModifyNPCAffectionCommand` - Modify NPC affection
- `KNPCAffectionCommand.KUnlockAllNpc` - Unlock all NPCs
