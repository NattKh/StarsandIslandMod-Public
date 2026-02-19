// =============================================================================
// Starsand Island Standalone Mod v3.7 - version.dll proxy
// No MelonLoader or BepInEx required!
//
// HOTKEYS:
//   F1    = Enable Dev Mode (required for debug features)
//   F2    = Give GM Tool item (one-time, intentional)
//   F3    = Unlock DLC (one-time, patches checks)
//   F4    = Toggle Time Control (on/off)
//   F5    = Unlock All Cosmetics/Equipment (one-time)
//   PgUp  = Speed up time  (when time control is active)
//   PgDn  = Slow down time (when time control is active)
//   Right = Advance time +1 hour (when time control is active)
//   Left  = Rewind time -1 hour  (when time control is active)
//   F6    = Toggle Bomb Arrow Boost (fast fire + big explosions)
//   F8    = Toggle Free Build
//   F9    = Toggle Cheat Store
//   HOME  = Hide/show console
// =============================================================================

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "version_proxy.h"
#include "il2cpp_api.h"

typedef struct { float x, y, z; } Vector3;

// =============================================================================
// State
// =============================================================================

static FILE* g_logFile = NULL;
static char g_logPath[MAX_PATH] = {0};
static CRITICAL_SECTION g_logLock;
static DWORD g_startTick = 0;
static volatile BOOL g_apiResolved = FALSE;
static volatile BOOL g_threadAttached = FALSE;
static Il2CppThread* g_il2cppThread = NULL;

// Feature flags
static BOOL g_devModeEnabled = FALSE;
static BOOL g_gmToolGiven = FALSE;
static BOOL g_dlcPatched = FALSE;
static BOOL g_freeBuildEnabled = FALSE;
static BOOL g_cheatStoreOpen = FALSE;
static BOOL g_cosmeticsUnlocked = FALSE;
static BOOL g_superBuildEnabled = FALSE;

// Time control
static BOOL g_timeControlActive = FALSE;
static int  g_timeSpeedIndex = 3;  // index into g_timeSpeeds (default = 1.0x)
static const float g_timeSpeeds[] = { 0.0f, 0.25f, 0.5f, 1.0f, 2.0f, 3.0f, 5.0f, 10.0f };
static const char* g_timeSpeedNames[] = { "PAUSED", "0.25x", "0.5x", "1.0x", "2.0x", "3.0x", "5.0x", "10.0x" };
#define TIME_SPEED_COUNT 8
#define TIME_SPEED_DEFAULT 3  // index of 1.0x

// =============================================================================
// Logging
// =============================================================================

static void ModLog(const char* fmt, ...) {
    EnterCriticalSection(&g_logLock);
    SYSTEMTIME st;
    GetLocalTime(&st);
    char msgBuf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    va_end(args);
    char fullLine[600];
    snprintf(fullLine, sizeof(fullLine), "[%02d:%02d:%02d] %s",
        st.wHour, st.wMinute, st.wSecond, msgBuf);
    if (g_logFile) { fprintf(g_logFile, "%s\n", fullLine); fflush(g_logFile); }
    printf("%s\n", fullLine);
    LeaveCriticalSection(&g_logLock);
}

// =============================================================================
// Worker thread commands
// =============================================================================

static HANDLE g_cmdEvent = NULL;

#define CMD_DEVMODE      0
#define CMD_GMTOOL       1
#define CMD_DLC          2
#define CMD_ALL          3
#define CMD_TOGGLE_TELE  4
#define CMD_TOGGLE_BUILD 5
#define CMD_TOGGLE_CHEAT 6
static volatile int g_pendingCmd = CMD_ALL;

// Helper
#define FIND_CLASS(ns, name) IL2CPP_FindClass(NULL, ns, name)

static void MainThread_ForceBuildMode(void); // Forward declaration

// Ensure thread is attached (call at start of each command)
static void EnsureAttached(void) {
    if (g_threadAttached) return;
    Il2CppDomain* domain = IL2CPP.domain_get();
    if (!domain) { ModLog("[ERROR] IL2CPP domain is NULL!"); return; }
    g_il2cppThread = IL2CPP.thread_attach(domain);
    g_threadAttached = TRUE;
    ModLog("Attached to IL2CPP GC (thread: 0x%p)", g_il2cppThread);
}

// =============================================================================
// F1: Enable Dev Mode
// =============================================================================

static void DoDevMode(void) {
    EnsureAttached();
    ModLog("");
    ModLog("=== Enabling Dev Mode ===");

    // A) KWorldUtil.IsDevStart = true (static field)
    Il2CppClass* worldUtil = FIND_CLASS("World", "KWorldUtil");
    if (!worldUtil) worldUtil = FIND_CLASS("", "KWorldUtil");
    if (worldUtil) {
        IL2CPP.runtime_class_init(worldUtil);
        Il2CppFieldInfo* devField = IL2CPP.class_get_field_from_name(worldUtil, "IsDevStart");
        if (devField) {
            BOOL val = TRUE;
            IL2CPP.field_static_set_value(devField, &val);
            ModLog("  [OK] KWorldUtil.IsDevStart = true");
        } else ModLog("  [MISS] KWorldUtil.IsDevStart field not found");
    } else ModLog("  [MISS] KWorldUtil class not found");

    // B) GameApp.Instance.IsDevStart = true
    Il2CppClass* gameApp = FIND_CLASS("", "GameApp");
    if (gameApp) {
        IL2CPP.runtime_class_init(gameApp);
        Il2CppFieldInfo* instField = IL2CPP.class_get_field_from_name(gameApp, "m_Instance");
        if (instField) {
            Il2CppObject* appInst = NULL;
            IL2CPP.field_static_get_value(instField, &appInst);
            if (appInst) {
                Il2CppFieldInfo* devF = IL2CPP.class_get_field_from_name(gameApp, "IsDevStart");
                if (devF) {
                    BOOL val = TRUE;
                    IL2CPP.field_set_value(appInst, devF, &val);
                    ModLog("  [OK] GameApp.Instance.IsDevStart = true");
                }
            } else ModLog("  [WARN] GameApp.Instance is null");
        }
    }

    // C) KDebugger.DevMode = true
    Il2CppClass* debugMod = FIND_CLASS("GameFramework.GameDebug", "DebugModule");
    if (debugMod) {
        IL2CPP.runtime_class_init(debugMod);
        Il2CppFieldInfo* instField = IL2CPP.class_get_field_from_name(debugMod, "Instance");
        if (instField) {
            Il2CppObject* modInst = NULL;
            IL2CPP.field_static_get_value(instField, &modInst);
            if (modInst) {
                Il2CppClass* modClass = *(Il2CppClass**)modInst;
                if (!modClass) modClass = FIND_CLASS("GameFramework.GameDebug", "KDebugModule");
                if (modClass) {
                    Il2CppMethodInfo* createDbg = IL2CPP.class_get_method_from_name(modClass, "CreateDebugger", 0);
                    if (createDbg) {
                        Il2CppObject* exc = NULL;
                        Il2CppObject* debugger = IL2CPP.runtime_invoke(createDbg, modInst, NULL, &exc);
                        if (debugger) {
                            Il2CppClass* dbgClass = FIND_CLASS("GameFramework.GameDebug", "KDebugger");
                            if (dbgClass) {
                                // Try backing field name
                                Il2CppFieldInfo* devModeF = IL2CPP.class_get_field_from_name(dbgClass, "<DevMode>k__BackingField");
                                if (!devModeF) devModeF = IL2CPP.class_get_field_from_name(dbgClass, "_003CDevMode_003Ek__BackingField");
                                if (devModeF) {
                                    BOOL val = TRUE;
                                    IL2CPP.field_set_value(debugger, devModeF, &val);
                                    ModLog("  [OK] KDebugger.DevMode = true");
                                } else {
                                    Il2CppMethodInfo* setDev = IL2CPP.class_get_method_from_name(dbgClass, "set_DevMode", 1);
                                    if (setDev) {
                                        BOOL val = TRUE;
                                        void* a[1] = { &val };
                                        IL2CPP.runtime_invoke(setDev, debugger, a, &exc);
                                        ModLog("  [OK] KDebugger.set_DevMode(true)");
                                    } else ModLog("  [MISS] DevMode field/setter not found");
                                }
                            }
                        } else ModLog("  [WARN] CreateDebugger returned null");
                    }
                }
            } else ModLog("  [WARN] DebugModule.Instance is null");
        }
    } else ModLog("  [INFO] DebugModule class not found");

    g_devModeEnabled = TRUE;
}

// =============================================================================
// F2: Give GM Tool
// =============================================================================

static void DoGMTool(void) {
    EnsureAttached();
    if (g_gmToolGiven) {
        ModLog("[F2] GM Tool already given (skipping)");
        return;
    }
    ModLog("");
    ModLog("=== Giving GM Tool ===");
    Il2CppClass* klass = FIND_CLASS("", "KGamePlayDebugger");
    if (!klass) klass = FIND_CLASS("KDebugger", "KGamePlayDebugger");
    if (klass) {
        Il2CppMethodInfo* m = IL2CPP.class_get_method_from_name(klass, "GetGMTool", 0);
        if (m) {
            Il2CppObject* exc = NULL;
            IL2CPP.runtime_invoke(m, NULL, NULL, &exc);
            if (exc) ModLog("  [WARN] GetGMTool threw exception");
            else { ModLog("  [OK] GetGMTool() added to inventory!"); g_gmToolGiven = TRUE; }
        } else ModLog("  [MISS] GetGMTool method not found");
    } else ModLog("  [MISS] KGamePlayDebugger not found");
}

// =============================================================================
// F3: DLC Bypass
// =============================================================================

static void DoDLCBypass(void) {
    EnsureAttached();
    if (g_dlcPatched) {
        ModLog("[F3] DLC already patched (skipping)");
        return;
    }
    ModLog("");
    ModLog("=== DLC Full Bypass ===");

    Il2CppClass* dlcFw = FIND_CLASS("GameFramework", "KDlcFramework");
    if (!dlcFw) dlcFw = FIND_CLASS("", "KDlcFramework");
    if (dlcFw) {
        IL2CPP.runtime_class_init(dlcFw);
        ModLog("  [INFO] KDlcFramework found. Listing all methods:");
        void* iter = NULL;
        Il2CppMethodInfo* mi;
        while ((mi = IL2CPP.class_get_methods(dlcFw, &iter)) != NULL)
            ModLog("    - %s(%d args)", IL2CPP.method_get_name(mi), IL2CPP.method_get_param_count(mi));

        // Call HackAddDlc
        Il2CppMethodInfo* hackAdd = IL2CPP.class_get_method_from_name(dlcFw, "HackAddDlc", 0);
        if (hackAdd) {
            Il2CppObject* exc = NULL;
            IL2CPP.runtime_invoke(hackAdd, NULL, NULL, &exc);
            ModLog("  [OK] HackAddDlc() called!");
        }

        // Check DLC codes
        Il2CppMethodInfo* isDlcCode = IL2CPP.class_get_method_from_name(dlcFw, "IsDlcAvailableCode", 1);
        const char* dlcNames[] = {
            "Starsand Island Classic European Furniture, Fashion & Vehicles DLC",
            "Starsand Island Chinese New Year Furniture & Fashion DLC",
            "EuropeanDLC", "CNYDLC", "DLC_EU", "DLC_CNY"
        };
        for (int i = 0; i < 6; i++) {
            if (isDlcCode) {
                Il2CppString* name = IL2CPP.string_new(dlcNames[i]);
                void* args[1] = { name };
                Il2CppObject* exc = NULL;
                Il2CppObject* result = IL2CPP.runtime_invoke(isDlcCode, NULL, args, &exc);
                if (result && !exc) {
                    int code = *(int*)IL2CPP.object_unbox(result);
                    ModLog("  IsDlcAvailableCode(\"%s\") = %d", dlcNames[i], code);
                }
            }
        }
    } else { ModLog("  [MISS] KDlcFramework not found"); }

    // Patch HasAnyDLCAvailable
    ModLog("");
    ModLog("  === Patching DLC check methods ===");
    Il2CppClass* dlcMethod = FIND_CLASS("GameWorld.DLC", "KDLCMethod");
    if (!dlcMethod) dlcMethod = FIND_CLASS("GameWorld", "KDLCMethod");
    if (!dlcMethod) dlcMethod = FIND_CLASS("", "KDLCMethod");
    if (dlcMethod) {
        ModLog("  [OK] Found KDLCMethod. Methods:");
        void* iter = NULL;
        Il2CppMethodInfo* mi;
        while ((mi = IL2CPP.class_get_methods(dlcMethod, &iter)) != NULL)
            ModLog("    - %s(%d args)", IL2CPP.method_get_name(mi), IL2CPP.method_get_param_count(mi));

        Il2CppMethodInfo* hasAny = IL2CPP.class_get_method_from_name(dlcMethod, "HasAnyDLCAvailable", 1);
        if (hasAny) {
            void** methodPtr = (void**)hasAny;
            void* funcAddr = *methodPtr;
            ModLog("  HasAnyDLCAvailable at 0x%p", funcAddr);
            if (funcAddr) {
                DWORD oldProt;
                if (VirtualProtect(funcAddr, 16, PAGE_EXECUTE_READWRITE, &oldProt)) {
                    unsigned char patch[] = { 0xB0, 0x01, 0xC3 }; // mov al, 1; ret
                    memcpy(funcAddr, patch, 3);
                    VirtualProtect(funcAddr, 16, oldProt, &oldProt);
                    ModLog("  [OK] PATCHED HasAnyDLCAvailable -> always returns TRUE!");
                }
            }
        }
    } else ModLog("  [MISS] KDLCMethod not found");

    // Patch IsDlcAvailable
    if (dlcFw) {
        Il2CppMethodInfo* isDlc = IL2CPP.class_get_method_from_name(dlcFw, "IsDlcAvailable", 1);
        if (isDlc) {
            void** methodPtr = (void**)isDlc;
            void* funcAddr = *methodPtr;
            ModLog("  IsDlcAvailable at 0x%p", funcAddr);
            if (funcAddr) {
                DWORD oldProt;
                if (VirtualProtect(funcAddr, 16, PAGE_EXECUTE_READWRITE, &oldProt)) {
                    unsigned char patch[] = { 0xB0, 0x01, 0xC3 };
                    memcpy(funcAddr, patch, 3);
                    VirtualProtect(funcAddr, 16, oldProt, &oldProt);
                    ModLog("  [OK] PATCHED IsDlcAvailable -> always returns TRUE!");
                }
            }
        }

        // Call HackAddDlc again after patches
        Il2CppMethodInfo* hackAdd = IL2CPP.class_get_method_from_name(dlcFw, "HackAddDlc", 0);
        if (hackAdd) {
            Il2CppObject* exc = NULL;
            IL2CPP.runtime_invoke(hackAdd, NULL, NULL, &exc);
            ModLog("  [OK] HackAddDlc() called again after patching!");
        }
    }

    g_dlcPatched = TRUE;
}

// =============================================================================
// Unlocks (called as part of F5 "activate all")
// =============================================================================

static void DoUnlocks(void) {
    EnsureAttached();
    Il2CppClass* klass = FIND_CLASS("", "KGamePlayDebugger");
    if (!klass) klass = FIND_CLASS("KDebugger", "KGamePlayDebugger");
    if (klass) {
        // Unlock regions
        ModLog("");
        ModLog("=== Unlocking All Regions ===");
        Il2CppMethodInfo* m = IL2CPP.class_get_method_from_name(klass, "UnLockAllRegion", 0);
        if (m) { Il2CppObject* exc = NULL; IL2CPP.runtime_invoke(m, NULL, NULL, &exc); ModLog("  [OK] UnLockAllRegion()"); }

        // Simple unlock all
        ModLog("");
        ModLog("=== Simple Unlock All ===");
        m = IL2CPP.class_get_method_from_name(klass, "SimpleUnlockAll", 0);
        if (m) { Il2CppObject* exc = NULL; IL2CPP.runtime_invoke(m, NULL, NULL, &exc); ModLog("  [OK] SimpleUnlockAll()"); }
    }

    // Try formula unlocks
    ModLog("");
    ModLog("=== Unlocking All Formulas ===");
    Il2CppClass* prodDbg = FIND_CLASS("", "KProducerDebug");
    if (!prodDbg) prodDbg = FIND_CLASS("GameWorld", "KProducerDebug");
    if (prodDbg) {
        Il2CppMethodInfo* m1 = IL2CPP.class_get_method_from_name(prodDbg, "UnLockAllProductionFormula", 0);
        if (m1) { Il2CppObject* exc = NULL; IL2CPP.runtime_invoke(m1, NULL, NULL, &exc); ModLog("  [OK] UnLockAllProductionFormula()"); }
        Il2CppMethodInfo* m2 = IL2CPP.class_get_method_from_name(prodDbg, "UnLockAllProcessFormula", 0);
        if (m2) { Il2CppObject* exc = NULL; IL2CPP.runtime_invoke(m2, NULL, NULL, &exc); ModLog("  [OK] UnLockAllProcessFormula()"); }
    } else ModLog("  [MISS] KProducerDebug not found");
}

// (No F5 "activate all" -- each feature is its own key)

// =============================================================================
// F8: Toggle Free Build
// =============================================================================

static void DoToggleFreeBuild(void) {
    EnsureAttached();
    g_freeBuildEnabled = !g_freeBuildEnabled;
    ModLog("[F8] Free Build: %s", g_freeBuildEnabled ? "ON" : "OFF");

    if (g_freeBuildEnabled) {
        Il2CppClass* klass = FIND_CLASS("", "KGamePlayDebugger");
        if (!klass) klass = FIND_CLASS("KDebugger", "KGamePlayDebugger");
        if (klass) {
            Il2CppMethodInfo* m = IL2CPP.class_get_method_from_name(klass, "SimpleUnlockAll", 0);
            if (m) {
                Il2CppObject* exc = NULL;
                IL2CPP.runtime_invoke(m, NULL, NULL, &exc);
                if (exc) ModLog("  [WARN] SimpleUnlockAll threw exception");
                else ModLog("  [OK] SimpleUnlockAll() called");
            }
        }
    } else {
        ModLog("  [INFO] Restart game to restore building costs.");
    }
}

// =============================================================================
// F9: Toggle Cheat Store
// CRITICAL: Unity UI calls MUST happen on the main thread!
// Strategy: Subclass the game window and use PostMessage to dispatch to main thread.
// PostMessage is guaranteed cross-thread. The subclassed WndProc runs on the
// window-owning thread = Unity's main thread.
// =============================================================================

#define WM_MOD_CHEATSTORE  (WM_APP + 0xBEEF)
#define WM_MOD_TIMESCALE   (WM_APP + 0xBEF0)
#define WM_MOD_UNLOCKALL   (WM_APP + 0xBEF1)
#define WM_MOD_TIMEOFDAY   (WM_APP + 0xBEF2)  // wParam = hours to add (signed)
#define WM_MOD_BOMBARROW   (WM_APP + 0xBEF3)  // wParam = 1 enable, 0 disable
#define WM_MOD_SUPERBUILD  (WM_APP + 0xBEF4)  // Toggle Super Free Build
#define WM_MOD_RESOURCE_BYPASS (WM_APP + 0xBEF5) // Toggle Resource Bypass
#define WM_MOD_FLY_TOGGLE      (WM_APP + 0xBEF6) // Toggle Fly Mode
#define WM_MOD_FLY_MOVE        (WM_APP + 0xBEF7) // Move Up/Down (wParam: 1=Up, 2=Down)
#define WM_MOD_FORCE_BUILD     (WM_APP + 0xBEF8) // Force Build Mode (F12)
static HWND g_gameWnd = NULL;
static WNDPROC g_origWndProc = NULL;
static volatile BOOL g_hookInstalled = FALSE;

// Helper: Open/close cheat store via IL2CPP (MUST be called on main thread)
static void MainThread_CheatStoreAction(int action) {
    // action: 1=open, 2=close, 3=toggle (OpenOrClose)
    ModLog("  [MAIN THREAD] Cheat store action=%d (tid=%lu)", action, GetCurrentThreadId());

    if (!g_apiResolved) { ModLog("  [SKIP] API not resolved"); return; }

    __try {
        Il2CppClass* openMgr = FIND_CLASS("GameFramework.UGUI", "KUIOpenManager");
        if (!openMgr) openMgr = FIND_CLASS("", "KUIOpenManager");
        if (!openMgr) { ModLog("  [MISS] KUIOpenManager class not found"); return; }

        Il2CppClass* cheatViewClass = FIND_CLASS("UI", "UICheatStoreView");
        if (!cheatViewClass) cheatViewClass = FIND_CLASS("", "UICheatStoreView");
        if (!cheatViewClass) { ModLog("  [MISS] UICheatStoreView class not found"); return; }

        // Get KUIOpenManager.Instance (Singleton<KUIOpenManager>)
        Il2CppObject* mgr = NULL;
        Il2CppMethodInfo* getInst = IL2CPP.class_get_method_from_name(openMgr, "get_Instance", 0);
        if (getInst) {
            Il2CppObject* exc = NULL;
            mgr = IL2CPP.runtime_invoke(getInst, NULL, NULL, &exc);
            if (exc) ModLog("  [WARN] get_Instance threw exception");
        }
        if (!mgr) { ModLog("  [MISS] KUIOpenManager.Instance is null"); return; }
        ModLog("  [OK] KUIOpenManager instance: 0x%p", mgr);

        // Build System.Type for UICheatStoreView
        Il2CppType* ilType = IL2CPP.class_get_type(cheatViewClass);
        typedef Il2CppObject* (*fn_type_get_object)(Il2CppType*);
        fn_type_get_object typeGetObj = (fn_type_get_object)GetProcAddress(
            IL2CPP.hGameAssembly, "il2cpp_type_get_object");
        Il2CppObject* typeObj = typeGetObj ? typeGetObj(ilType) : NULL;
        if (!typeObj) { ModLog("  [MISS] Could not create Type object"); return; }
        ModLog("  [OK] Type object: 0x%p", typeObj);

        // ---- Approach 1: OpenOrClose(Type, bool, bool) - cleanest toggle ----
        if (action == 3) {
            Il2CppMethodInfo* toggleM = IL2CPP.class_get_method_from_name(
                openMgr, "OpenOrClose", 3);
            if (toggleM) {
                int canOpen = 1, canClose = 1;
                void* args[3] = { typeObj, &canOpen, &canClose };
                Il2CppObject* exc = NULL;
                IL2CPP.runtime_invoke(toggleM, mgr, args, &exc);
                if (!exc) {
                    ModLog("  [OK] OpenOrClose(UICheatStoreView, true, true) succeeded!");
                    return;
                }
                ModLog("  [WARN] OpenOrClose threw exception");
            } else {
                ModLog("  [MISS] OpenOrClose(3) not found, falling back...");
            }
            // Fall through to Open
            action = 1;
        }

        // ---- Approach 2: Open(Type) or Close(Type) ----
        {
            const char* methodName = (action == 1) ? "Open" : "Close";
            Il2CppMethodInfo* m = IL2CPP.class_get_method_from_name(openMgr, methodName, 1);
            if (m) {
                void* args[1] = { typeObj };
                Il2CppObject* exc = NULL;
                IL2CPP.runtime_invoke(m, mgr, args, &exc);
                if (!exc) {
                    ModLog("  [OK] %s(UICheatStoreView) succeeded on main thread!", methodName);
                    return;
                }
                ModLog("  [WARN] %s threw managed exception", methodName);
            } else {
                ModLog("  [MISS] %s(1 arg) not found", methodName);
            }
        }

        // ---- Approach 3: OpenAsync(Type, null, null) ----
        if (action == 1) {
            Il2CppMethodInfo* asyncM = IL2CPP.class_get_method_from_name(
                openMgr, "OpenAsync", 3);
            if (asyncM) {
                void* args[3] = { typeObj, NULL, NULL };
                Il2CppObject* exc = NULL;
                IL2CPP.runtime_invoke(asyncM, mgr, args, &exc);
                if (!exc) {
                    ModLog("  [OK] OpenAsync(UICheatStoreView) succeeded on main thread!");
                    return;
                }
                ModLog("  [WARN] OpenAsync threw exception");
            }
        }

        // ---- Approach 4: DefaultOpen(Type) ----
        if (action == 1) {
            Il2CppMethodInfo* defOpen = IL2CPP.class_get_method_from_name(
                openMgr, "DefaultOpen", 1);
            if (defOpen) {
                void* args[1] = { typeObj };
                Il2CppObject* exc = NULL;
                IL2CPP.runtime_invoke(defOpen, mgr, args, &exc);
                if (!exc) {
                    ModLog("  [OK] DefaultOpen(UICheatStoreView) succeeded!");
                    return;
                }
                ModLog("  [WARN] DefaultOpen threw exception");
            }
        }

        ModLog("  [FAIL] All main-thread approaches exhausted.");
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ModLog("  [CRASH] Main thread exception: 0x%08X", GetExceptionCode());
    }
}

// =============================================================================
// Time Scale Control (runs on MAIN THREAD via PostMessage)
// Sets both CGlobalTimeSystem.GAME_TIME_SCALE and UnityEngine.Time.timeScale
// =============================================================================

static void MainThread_SetTimeScale(float scale) {
    ModLog("  [MAIN THREAD] Setting time scale to %.2f (tid=%lu)", scale, GetCurrentThreadId());

    if (!g_apiResolved) { ModLog("  [SKIP] API not resolved"); return; }

    int success = 0;

    // --- Method 1: CGlobalTimeSystem.GAME_TIME_SCALE (static field) ---
    __try {
        Il2CppClass* timeSys = FIND_CLASS("GameTime", "CGlobalTimeSystem");
        if (!timeSys) timeSys = FIND_CLASS("", "CGlobalTimeSystem");
        if (timeSys) {
            IL2CPP.runtime_class_init(timeSys);
            Il2CppFieldInfo* scaleField = IL2CPP.class_get_field_from_name(timeSys, "GAME_TIME_SCALE");
            if (scaleField) {
                float val = scale;
                IL2CPP.field_static_set_value(scaleField, &val);
                ModLog("  [OK] CGlobalTimeSystem.GAME_TIME_SCALE = %.2f", scale);
                success++;
            } else ModLog("  [MISS] GAME_TIME_SCALE field not found");
        } else ModLog("  [MISS] CGlobalTimeSystem not found");
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ModLog("  [CRASH] CGlobalTimeSystem: 0x%08X", GetExceptionCode());
    }

    // --- Method 2: KTimeScale.Level (static property) ---
    __try {
        Il2CppClass* ktScale = FIND_CLASS("GameFramework", "KTimeScale");
        if (!ktScale) ktScale = FIND_CLASS("", "KTimeScale");
        if (ktScale) {
            IL2CPP.runtime_class_init(ktScale);
            // Try setter: set_Level(float)
            Il2CppMethodInfo* setLevel = IL2CPP.class_get_method_from_name(ktScale, "set_Level", 1);
            if (setLevel) {
                float val = scale;
                void* args[1] = { &val };
                Il2CppObject* exc = NULL;
                IL2CPP.runtime_invoke(setLevel, NULL, args, &exc);
                if (!exc) { ModLog("  [OK] KTimeScale.Level = %.2f", scale); success++; }
                else ModLog("  [WARN] set_Level threw exception");
            } else {
                // Fallback: set m_Level field directly
                Il2CppFieldInfo* lvlField = IL2CPP.class_get_field_from_name(ktScale, "m_Level");
                if (lvlField) {
                    float val = scale;
                    IL2CPP.field_static_set_value(lvlField, &val);
                    ModLog("  [OK] KTimeScale.m_Level = %.2f (direct)", scale);
                    success++;
                } else ModLog("  [MISS] KTimeScale.m_Level not found");
            }
        } else ModLog("  [MISS] KTimeScale not found");
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ModLog("  [CRASH] KTimeScale: 0x%08X", GetExceptionCode());
    }

    // --- Method 3: UnityEngine.Time.timeScale (property) ---
    __try {
        Il2CppClass* timeClass = FIND_CLASS("UnityEngine", "Time");
        if (!timeClass) timeClass = FIND_CLASS("", "Time");
        if (timeClass) {
            IL2CPP.runtime_class_init(timeClass);
            Il2CppMethodInfo* setTS = IL2CPP.class_get_method_from_name(timeClass, "set_timeScale", 1);
            if (setTS) {
                float val = scale;
                void* args[1] = { &val };
                Il2CppObject* exc = NULL;
                IL2CPP.runtime_invoke(setTS, NULL, args, &exc);
                if (!exc) { ModLog("  [OK] UnityEngine.Time.timeScale = %.2f", scale); success++; }
                else ModLog("  [WARN] set_timeScale threw exception");
            } else ModLog("  [MISS] Time.set_timeScale not found");
        } else ModLog("  [MISS] UnityEngine.Time not found");
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ModLog("  [CRASH] Time.timeScale: 0x%08X", GetExceptionCode());
    }

    if (success > 0)
        ModLog("  Time scale set via %d method(s)", success);
    else
        ModLog("  [FAIL] Could not set time scale through any method");
}

// =============================================================================
// Time of Day Control (runs on MAIN THREAD via PostMessage)
// Advances or rewinds the game clock by N hours
// =============================================================================

static void MainThread_ChangeTimeOfDay(int deltaHours) {
    ModLog("  [MAIN THREAD] Changing time of day by %+d hours", deltaHours);
    if (!g_apiResolved) return;

    Il2CppObject* exc0 = NULL;

    __try {
        // Get CGlobalTimeSystem instance via KEcsSingletonComponent.Get(context) pattern
        Il2CppClass* timeSysClass = FIND_CLASS("GameTime", "CGlobalTimeSystem");
        if (!timeSysClass) timeSysClass = FIND_CLASS("", "CGlobalTimeSystem");
        if (!timeSysClass) { ModLog("  [MISS] CGlobalTimeSystem not found"); return; }

        IL2CPP.runtime_class_init(timeSysClass);

        // CGlobalTimeSystem -> KGameSystem<T> -> KEcsSingletonComponent<T>
        // Access pattern: KEcsSingletonComponent<T>.Get(IEcsContext context)
        // First get KWorldUtil.CurContext
        Il2CppClass* kWorldUtilClass = FIND_CLASS("World", "KWorldUtil");
        if (!kWorldUtilClass) kWorldUtilClass = FIND_CLASS("", "KWorldUtil");
        Il2CppObject* timeSys = NULL;
        Il2CppClass* cls = NULL;

        if (kWorldUtilClass) {
            IL2CPP.runtime_class_init(kWorldUtilClass);
            Il2CppMethodInfo* getCurCtx = IL2CPP.class_get_method_from_name(kWorldUtilClass, "get_CurContext", 0);
            if (getCurCtx) {
                Il2CppObject* context = IL2CPP.runtime_invoke(getCurCtx, NULL, NULL, &exc0);
                if (context) {
                    ModLog("  [OK] KWorldUtil.CurContext: 0x%p", context);
                    // Now call CGlobalTimeSystem.Get(context) - static method on parent class
                    Il2CppMethodInfo* getMethod = NULL;
                    cls = timeSysClass;
                    while (cls && !getMethod) {
                        getMethod = IL2CPP.class_get_method_from_name(cls, "Get", 1);
                        if (!getMethod) cls = IL2CPP.class_get_parent(cls);
                    }
                    if (getMethod) {
                        void* args[1] = { context };
                        timeSys = IL2CPP.runtime_invoke(getMethod, NULL, args, &exc0);
                    } else ModLog("  [MISS] Get(context) method not found");
                } else ModLog("  [MISS] CurContext is null");
            } else ModLog("  [MISS] get_CurContext not found");
        }

        // Fallback: try static field ms_CacheComponent
        if (!timeSys) {
            Il2CppFieldInfo* cacheF = NULL;
            cls = timeSysClass;
            while (cls && !cacheF) {
                cacheF = IL2CPP.class_get_field_from_name(cls, "ms_CacheComponent");
                if (!cacheF) cls = IL2CPP.class_get_parent(cls);
            }
            if (cacheF) {
                IL2CPP.field_static_get_value(cacheF, &timeSys);
                if (timeSys) ModLog("  [OK] Got CGlobalTimeSystem from ms_CacheComponent");
            }
        }

        if (timeSys) {
            ModLog("  [OK] CGlobalTimeSystem: 0x%p", timeSys);

            // Now field is KGameTime struct - directly modify it
            // CGlobalTimeSystem.Now is a public KGameTime field
            Il2CppFieldInfo* nowField = IL2CPP.class_get_field_from_name(timeSysClass, "Now");
            if (nowField) {
                int nowOffset = IL2CPP.field_get_offset(nowField);
                // KGameTime is a struct with a single TimeSpan m_TimeSpan field
                // TimeSpan is stored as a single int64 (ticks)
                // 1 hour = 3600 * 10000000 ticks
                long long* ticksPtr = (long long*)((char*)timeSys + nowOffset);
                long long oldTicks = *ticksPtr;
                long long hourTicks = (long long)deltaHours * 3600LL * 10000000LL;
                *ticksPtr = oldTicks + hourTicks;

                // Calculate current hour for display
                long long totalSecs = *ticksPtr / 10000000LL;
                int hours = (int)((totalSecs / 3600) % 24);
                ModLog("  [OK] CGlobalTimeSystem.Now updated.");

                // Also update TotalElapsedSeconds (likely the source of truth)
                // Try standard backing field for property
                Il2CppFieldInfo* totalSecField = IL2CPP.class_get_field_from_name(timeSysClass, "<TotalElapsedSeconds>k__BackingField");
                if (!totalSecField) totalSecField = IL2CPP.class_get_field_from_name(timeSysClass, "_003CTotalElapsedSeconds_003Ek__BackingField");
                
                if (totalSecField) {
                    float* elapsedPtr = (float*)((char*)timeSys + IL2CPP.field_get_offset(totalSecField));
                    float oldElapsed = *elapsedPtr;
                    *elapsedPtr += (float)(deltaHours * 3600);
                    ModLog("  [OK] TotalElapsedSeconds: %.1f -> %.1f", oldElapsed, *elapsedPtr);
                } else {
                    ModLog("  [WARN] TotalElapsedSeconds backing field not found");
                }

                // Also try to update delta seconds to force an update? Maybe not needed.
                return;
            } else ModLog("  [MISS] CGlobalTimeSystem.Now field not found");
        }

        // Fallback: Try KTimeDebugOption from debug module
        ModLog("  Trying KTimeDebugOption fallback...");
        Il2CppClass* timeDbgClass = FIND_CLASS("GameTime", "KTimeDebugOption");
        if (!timeDbgClass) timeDbgClass = FIND_CLASS("", "KTimeDebugOption");
        if (timeDbgClass) {
            // Find instance in KDebugModule.m_DebugOptions
            Il2CppClass* dbgModule = FIND_CLASS("GameFramework.GameDebug", "KDebugModule");
            if (!dbgModule) dbgModule = FIND_CLASS("", "KDebugModule");
            if (dbgModule) {
                Il2CppObject* dbgInst = NULL;
                cls = dbgModule;
                while (cls && !dbgInst) {
                    Il2CppMethodInfo* gi = IL2CPP.class_get_method_from_name(cls, "get_Instance", 0);
                    if (gi) { Il2CppObject* exc = NULL; dbgInst = IL2CPP.runtime_invoke(gi, NULL, NULL, &exc); }
                    cls = IL2CPP.class_get_parent(cls);
                }
                if (dbgInst) {
                    Il2CppFieldInfo* optList = IL2CPP.class_get_field_from_name(dbgModule, "m_DebugOptions");
                    if (optList) {
                        int off = IL2CPP.field_get_offset(optList);
                        Il2CppObject* list = *(Il2CppObject**)((char*)dbgInst + off);
                        if (list) {
                            Il2CppClass* listCls = *(Il2CppClass**)list;
                            Il2CppMethodInfo* getCount = IL2CPP.class_get_method_from_name(listCls, "get_Count", 0);
                            Il2CppMethodInfo* getItem = IL2CPP.class_get_method_from_name(listCls, "get_Item", 1);
                            if (getCount && getItem) {
                                Il2CppObject* cntObj = IL2CPP.runtime_invoke(getCount, list, NULL, &exc0);
                                int count = cntObj ? *(int*)IL2CPP.object_unbox(cntObj) : 0;
                                for (int i = 0; i < count; i++) {
                                    void* args[1] = { &i };
                                    Il2CppObject* opt = IL2CPP.runtime_invoke(getItem, list, args, &exc0);
                                    if (!opt) continue;
                                    Il2CppClass* optCls = *(Il2CppClass**)opt;
                                    const char* name = IL2CPP.class_get_name(optCls);
                                    if (strcmp(name, "KTimeDebugOption") == 0) {
                                        ModLog("  [OK] Found KTimeDebugOption at index %d", i);
                                        // Get CurHour, add delta, set
                                        Il2CppMethodInfo* getH = IL2CPP.class_get_method_from_name(optCls, "get_CurHour", 0);
                                        Il2CppMethodInfo* setH = IL2CPP.class_get_method_from_name(optCls, "set_CurHour", 1);
                                        if (getH && setH) {
                                            Il2CppObject* hObj = IL2CPP.runtime_invoke(getH, opt, NULL, &exc0);
                                            if (hObj) {
                                                int curH = *(int*)IL2CPP.object_unbox(hObj);
                                                int newH = curH + deltaHours;
                                                // Wrap around 0-23
                                                while (newH < 0) newH += 24;
                                                while (newH >= 24) newH -= 24;
                                                void* sArgs[1] = { &newH };
                                                IL2CPP.runtime_invoke(setH, opt, sArgs, &exc0);
                                                ModLog("  [OK] CurHour: %d -> %d", curH, newH);
                                            }
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ModLog("  [CRASH] TimeOfDay exception: 0x%08X", GetExceptionCode());
    }
}

// =============================================================================
// F6: Bomb Arrow Boost (MAIN THREAD)
// 1. Modifies KCrossbowMotion.BaseMaxAccumulatingTime for faster fire rate
// 2. Patches KExplosion.get_Range to return bigger blast radius
// =============================================================================

static BOOL g_bombBoostActive = FALSE;
static float g_origAccumTime = 0.0f;
static BOOL g_origAccumSaved = FALSE;
static BYTE g_origExplosionGetRange[16] = {0};
static BOOL g_origExplosionSaved = FALSE;
static void* g_explosionGetRangePtr = NULL;

static void MainThread_BombArrowBoost(int enable) {
    ModLog("  [MAIN THREAD] Bomb Arrow Boost: %s (tid=%lu)", enable ? "ON" : "OFF", GetCurrentThreadId());
    if (!g_apiResolved) return;

    Il2CppObject* exc0 = NULL;

    __try {
        // === Part 1: Rate of Fire ===
        // Get player entity -> CHunter -> HunterMotion -> KCrossbowMotion
        Il2CppClass* kWorldUtilClass = FIND_CLASS("World", "KWorldUtil");
        if (!kWorldUtilClass) kWorldUtilClass = FIND_CLASS("", "KWorldUtil");
        Il2CppObject* playerEntity = NULL;
        if (kWorldUtilClass) {
            IL2CPP.runtime_class_init(kWorldUtilClass);
            Il2CppMethodInfo* getMain = IL2CPP.class_get_method_from_name(kWorldUtilClass, "get_MainEntity", 0);
            if (getMain) playerEntity = IL2CPP.runtime_invoke(getMain, NULL, NULL, &exc0);
        }
        if (!playerEntity) { ModLog("  [MISS] No player entity"); return; }

        // Get CHunter component
        Il2CppClass* hunterClass = FIND_CLASS("GameWorld.Hunting", "CHunter");
        if (!hunterClass) hunterClass = FIND_CLASS("", "CHunter");
        Il2CppObject* hunter = NULL;
        if (hunterClass) {
            // Use GetComponentByType on entity
            Il2CppClass* entityClass = *(Il2CppClass**)playerEntity;
            Il2CppMethodInfo* getCompByType = NULL;
            Il2CppClass* cls = entityClass;
            while (cls && !getCompByType) {
                getCompByType = IL2CPP.class_get_method_from_name(cls, "GetComponentByType", 1);
                if (!getCompByType) cls = IL2CPP.class_get_parent(cls);
            }
            if (getCompByType) {
                Il2CppType* ht = IL2CPP.class_get_type(hunterClass);
                typedef Il2CppObject* (*fn_tgo)(Il2CppType*);
                fn_tgo typeGetObj = (fn_tgo)GetProcAddress(IL2CPP.hGameAssembly, "il2cpp_type_get_object");
                Il2CppObject* hTypeObj = typeGetObj ? typeGetObj(ht) : NULL;
                if (hTypeObj) {
                    void* args[1] = { hTypeObj };
                    hunter = IL2CPP.runtime_invoke(getCompByType, playerEntity, args, &exc0);
                }
            }
        }
        if (!hunter) { ModLog("  [MISS] CHunter not found on player"); return; }
        ModLog("  [OK] CHunter: 0x%p", hunter);

        // Get HunterMotion property (IHuntingMotion, concrete = KCrossbowMotion or KBowMotion etc.)
        Il2CppMethodInfo* getMotion = NULL;
        {
            Il2CppClass* cls = hunterClass;
            while (cls && !getMotion) {
                getMotion = IL2CPP.class_get_method_from_name(cls, "get_HunterMotion", 0);
                if (!getMotion) cls = IL2CPP.class_get_parent(cls);
            }
        }
        Il2CppObject* motion = NULL;
        if (getMotion) motion = IL2CPP.runtime_invoke(getMotion, hunter, NULL, &exc0);
        if (!motion) {
            ModLog("  [INFO] HunterMotion is NULL (not holding a ranged weapon?)");
            ModLog("  [HINT] Equip your crossbow first, then press F6");
        } else {
            Il2CppClass* motionClass = *(Il2CppClass**)motion;
            const char* motionName = IL2CPP.class_get_name(motionClass);
            ModLog("  [OK] HunterMotion: 0x%p (%s)", motion, motionName);

            // Find BaseMaxAccumulatingTime field
            Il2CppFieldInfo* accumField = NULL;
            {
                Il2CppClass* cls = motionClass;
                while (cls && !accumField) {
                    accumField = IL2CPP.class_get_field_from_name(cls, "BaseMaxAccumulatingTime");
                    if (!accumField) cls = IL2CPP.class_get_parent(cls);
                }
            }
            if (accumField) {
                int offset = (int)IL2CPP.field_get_offset(accumField);
                float* accumPtr = (float*)((char*)motion + offset);
                if (enable) {
                    if (!g_origAccumSaved) {
                        g_origAccumTime = *accumPtr;
                        g_origAccumSaved = TRUE;
                        ModLog("  [OK] Saved original BaseMaxAccumulatingTime: %.3f", g_origAccumTime);
                    }
                    *accumPtr = 0.05f;  // Near-instant charge
                    ModLog("  [OK] BaseMaxAccumulatingTime: %.3f -> 0.050 (FAST FIRE)", g_origAccumTime);
                } else {
                    float restore = g_origAccumSaved ? g_origAccumTime : 1.0f;
                    *accumPtr = restore;
                    ModLog("  [OK] BaseMaxAccumulatingTime restored to: %.3f", restore);
                }
            } else {
                ModLog("  [MISS] BaseMaxAccumulatingTime field not found");
            }
        }

        // === Part 2: Blast Radius ===
        // Patch KExplosion.get_Range to return a larger value
        // This is an assembly-level patch like the DLC bypass
        Il2CppClass* explosionClass = FIND_CLASS("External.Physics", "KExplosion");
        if (!explosionClass) explosionClass = FIND_CLASS("", "KExplosion");
        if (explosionClass) {
            Il2CppMethodInfo* getRange = IL2CPP.class_get_method_from_name(explosionClass, "get_Range", 0);
            if (getRange) {
                // Get the compiled method pointer
                void* methodPtr = *(void**)getRange;  // First field of MethodInfo is the method pointer
                if (methodPtr && methodPtr != (void*)1 && methodPtr != (void*)-1) {
                    g_explosionGetRangePtr = methodPtr;
                    ModLog("  [OK] KExplosion.get_Range at: 0x%p", methodPtr);

                    if (enable) {
                        // Save original bytes
                        if (!g_origExplosionSaved) {
                            DWORD oldProtect;
                            VirtualProtect(methodPtr, 16, PAGE_EXECUTE_READWRITE, &oldProtect);
                            memcpy(g_origExplosionGetRange, methodPtr, 16);
                            g_origExplosionSaved = TRUE;
                            VirtualProtect(methodPtr, 16, oldProtect, &oldProtect);
                            ModLog("  [OK] Saved original get_Range bytes");
                        }

                        // Patch get_Range to always return 25.0f
                        // x64 asm: mov eax, 0x41C80000 (25.0f); movd xmm0, eax; ret
                        // Bytes: B8 00 00 C8 41  66 0F 6E C0  C3
                        BYTE patch[] = { 0xB8, 0x00, 0x00, 0xC8, 0x41,   // mov eax, 0x41C80000 (25.0f)
                                         0x66, 0x0F, 0x6E, 0xC0,          // movd xmm0, eax
                                         0xC3 };                            // ret
                        DWORD oldProtect;
                        VirtualProtect(methodPtr, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect);
                        memcpy(methodPtr, patch, sizeof(patch));
                        VirtualProtect(methodPtr, sizeof(patch), oldProtect, &oldProtect);
                        ModLog("  [OK] Patched get_Range -> always returns 25.0 (big blast)");
                    } else {
                        // Restore original bytes
                        if (g_origExplosionSaved) {
                            DWORD oldProtect;
                            VirtualProtect(methodPtr, 16, PAGE_EXECUTE_READWRITE, &oldProtect);
                            memcpy(methodPtr, g_origExplosionGetRange, 16);
                            VirtualProtect(methodPtr, 16, oldProtect, &oldProtect);
                            ModLog("  [OK] Restored original get_Range");
                        }
                    }
                } else {
                    ModLog("  [MISS] get_Range method pointer invalid");
                }
            } else {
                ModLog("  [MISS] KExplosion.get_Range not found");
            }
        } else {
            ModLog("  [MISS] KExplosion class not found");
        }

    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ModLog("  [CRASH] BombArrowBoost exception: 0x%08X", GetExceptionCode());
    }
}

// =============================================================================
// F5: Unlock All Cosmetics / Equipment
// Iterates KItemTemplateSet, finds all items with EquipmentItemExt,
// creates them via CGlobalItemSystem.CreateItem and pushes to wardrobe.
// MUST run on main thread.
// =============================================================================

static void MainThread_UnlockCosmetics(void) {
    ModLog("  [MAIN THREAD] Unlocking all cosmetics (tid=%lu)", GetCurrentThreadId());
    if (!g_apiResolved) { ModLog("  [SKIP] API not resolved"); return; }

    __try {
        // 1. Get KItemTemplateSet.Instance (Singleton)
        Il2CppClass* templateSetClass = FIND_CLASS("Inventory", "KItemTemplateSet");
        if (!templateSetClass) templateSetClass = FIND_CLASS("", "KItemTemplateSet");
        if (!templateSetClass) { ModLog("  [MISS] KItemTemplateSet not found"); return; }

        IL2CPP.runtime_class_init(templateSetClass);
        Il2CppObject* templateSet = NULL;

        // Walk parent chain for get_Instance (Singleton<T> pattern)
        Il2CppClass* cls = templateSetClass;
        Il2CppMethodInfo* getInst = NULL;
        while (cls && !getInst) {
            getInst = IL2CPP.class_get_method_from_name(cls, "get_Instance", 0);
            if (!getInst) cls = IL2CPP.class_get_parent(cls);
        }
        if (getInst) {
            Il2CppObject* exc = NULL;
            templateSet = IL2CPP.runtime_invoke(getInst, NULL, NULL, &exc);
        }
        if (!templateSet) { ModLog("  [MISS] KItemTemplateSet.Instance is null"); return; }
        ModLog("  [OK] KItemTemplateSet: 0x%p", templateSet);

        // 2. Get AllTemplates dictionary for count
        Il2CppMethodInfo* getAllTempl = NULL;
        cls = templateSetClass;
        while (cls && !getAllTempl) {
            getAllTempl = IL2CPP.class_get_method_from_name(cls, "get_AllTemplates", 0);
            if (!getAllTempl) cls = IL2CPP.class_get_parent(cls);
        }
        if (!getAllTempl) { ModLog("  [MISS] get_AllTemplates not found"); return; }

        Il2CppObject* exc0 = NULL;
        Il2CppObject* allTemplDict = IL2CPP.runtime_invoke(getAllTempl, templateSet, NULL, &exc0);
        if (!allTemplDict) { ModLog("  [MISS] AllTemplates returned null"); return; }

        // 3. Get EquipmentItemExt Type object for checking
        Il2CppClass* equipExtClass = FIND_CLASS("XSandbox", "EquipmentItemExt");
        if (!equipExtClass) equipExtClass = FIND_CLASS("", "EquipmentItemExt");
        Il2CppType* equipExtType = equipExtClass ? IL2CPP.class_get_type(equipExtClass) : NULL;
        typedef Il2CppObject* (*fn_tgo)(Il2CppType*);
        fn_tgo typeGetObj = (fn_tgo)GetProcAddress(IL2CPP.hGameAssembly, "il2cpp_type_get_object");
        Il2CppObject* equipExtTypeObj = (equipExtType && typeGetObj) ? typeGetObj(equipExtType) : NULL;

        // 6. Get CGlobalItemSystem.GetCurrent()
        Il2CppClass* itemSysClass = FIND_CLASS("Inventory", "CGlobalItemSystem");
        if (!itemSysClass) itemSysClass = FIND_CLASS("", "CGlobalItemSystem");
        Il2CppObject* itemSys = NULL;
        if (itemSysClass) {
            Il2CppMethodInfo* getCur = IL2CPP.class_get_method_from_name(itemSysClass, "GetCurrent", 0);
            if (getCur) itemSys = IL2CPP.runtime_invoke(getCur, NULL, NULL, &exc0);
        }
        if (!itemSys) { ModLog("  [MISS] CGlobalItemSystem.GetCurrent() returned null"); return; }
        ModLog("  [OK] CGlobalItemSystem: 0x%p", itemSys);

        // 7. Get player entity via KWorldUtil.MainEntity
        Il2CppClass* kWorldUtilClass = FIND_CLASS("World", "KWorldUtil");
        if (!kWorldUtilClass) kWorldUtilClass = FIND_CLASS("", "KWorldUtil");
        Il2CppObject* playerEntity = NULL;
        if (kWorldUtilClass) {
            IL2CPP.runtime_class_init(kWorldUtilClass);
            Il2CppMethodInfo* getMain = IL2CPP.class_get_method_from_name(kWorldUtilClass, "get_MainEntity", 0);
            if (getMain) playerEntity = IL2CPP.runtime_invoke(getMain, NULL, NULL, &exc0);
        }
        if (!playerEntity) { ModLog("  [FAIL] MainEntity is null - are you in-game?"); return; }
        ModLog("  [OK] MainEntity: 0x%p", playerEntity);

        // 8. Get KInventoryMethod.PushItem(KEcsEntity, KItem) - static, handles routing
        Il2CppClass* invMethodClass = FIND_CLASS("Inventory", "KInventoryMethod");
        if (!invMethodClass) invMethodClass = FIND_CLASS("", "KInventoryMethod");
        Il2CppMethodInfo* pushItemM = NULL;
        if (invMethodClass) {
            IL2CPP.runtime_class_init(invMethodClass);
            pushItemM = IL2CPP.class_get_method_from_name(invMethodClass, "PushItem", 2);
        }
        if (!pushItemM) { ModLog("  [MISS] KInventoryMethod.PushItem(2) not found"); return; }
        ModLog("  [OK] KInventoryMethod.PushItem found");

        // 9. Get CreateItem method
        Il2CppMethodInfo* createItem = IL2CPP.class_get_method_from_name(itemSysClass, "CreateItem", 3);
        if (!createItem) { ModLog("  [MISS] CreateItem(3) not found"); return; }

        // 10. Get dictionary count for logging
        Il2CppClass* dictClass = *(Il2CppClass**)allTemplDict;
        Il2CppMethodInfo* getDictCount = NULL;
        cls = dictClass;
        while (cls && !getDictCount) {
            getDictCount = IL2CPP.class_get_method_from_name(cls, "get_Count", 0);
            if (!getDictCount) cls = IL2CPP.class_get_parent(cls);
        }
        int dictCount = 0;
        if (getDictCount) {
            Il2CppObject* cntObj = IL2CPP.runtime_invoke(getDictCount, allTemplDict, NULL, &exc0);
            if (cntObj) dictCount = *(int*)IL2CPP.object_unbox(cntObj);
        }
        ModLog("  [OK] AllTemplates has %d entries", dictCount);

        // 11. Get enumerator - try BaseTemplateSet.GetEnumerator() first (IEnumerable<T>)
        Il2CppMethodInfo* getEnum = NULL;
        cls = templateSetClass;
        while (cls && !getEnum) {
            getEnum = IL2CPP.class_get_method_from_name(cls, "GetEnumerator", 0);
            if (!getEnum) cls = IL2CPP.class_get_parent(cls);
        }
        Il2CppObject* enumerator2 = NULL;
        if (getEnum) {
            enumerator2 = IL2CPP.runtime_invoke(getEnum, templateSet, NULL, &exc0);
            if (enumerator2) ModLog("  [OK] Got enumerator from BaseTemplateSet: %s",
                IL2CPP.class_get_name(*(Il2CppClass**)enumerator2));
        }

        // Fallback: Dictionary.Values.GetEnumerator
        if (!enumerator2) {
            ModLog("  [INFO] BaseTemplateSet enumerator failed, trying Dict.Values...");
            Il2CppMethodInfo* getValues = NULL;
            cls = dictClass;
            while (cls && !getValues) {
                getValues = IL2CPP.class_get_method_from_name(cls, "get_Values", 0);
                if (!getValues) cls = IL2CPP.class_get_parent(cls);
            }
            if (getValues) {
                Il2CppObject* values = IL2CPP.runtime_invoke(getValues, allTemplDict, NULL, &exc0);
                if (values) {
                    Il2CppClass* vc = *(Il2CppClass**)values;
                    Il2CppMethodInfo* ge2 = NULL;
                    cls = vc;
                    while (cls && !ge2) {
                        ge2 = IL2CPP.class_get_method_from_name(cls, "GetEnumerator", 0);
                        if (!ge2) cls = IL2CPP.class_get_parent(cls);
                    }
                    if (ge2) enumerator2 = IL2CPP.runtime_invoke(ge2, values, NULL, &exc0);
                    if (enumerator2) ModLog("  [OK] Got enumerator from Dict.Values: %s",
                        IL2CPP.class_get_name(*(Il2CppClass**)enumerator2));
                }
            }
        }
        if (!enumerator2) { ModLog("  [MISS] Could not get any enumerator"); return; }
        
        static void MainThread_ToggleBuildLimits(BOOL enable); // Forward Decl

        // 12. Get MoveNext / get_Current
        Il2CppClass* enumClass = *(Il2CppClass**)enumerator2;
        Il2CppMethodInfo* moveNext = NULL;
        Il2CppMethodInfo* getCurrent = NULL;
        cls = enumClass;
        while (cls) {
            if (!moveNext) moveNext = IL2CPP.class_get_method_from_name(cls, "MoveNext", 0);
            if (!getCurrent) getCurrent = IL2CPP.class_get_method_from_name(cls, "get_Current", 0);
            cls = IL2CPP.class_get_parent(cls);
        }
        if (!moveNext) { ModLog("  [MISS] MoveNext not found on enumerator"); return; }
        if (!getCurrent) { ModLog("  [MISS] get_Current not found on enumerator"); return; }

        // 13. Iterate and add ALL items (skip equipment filtering since it was crashing)
        //     Just create and push every single template - the game's PushItem handles routing
        Il2CppClass* itemTmplClass = FIND_CLASS("Inventory", "ItemTemplate");
        if (!itemTmplClass) itemTmplClass = FIND_CLASS("", "ItemTemplate");

        int totalItems = 0, addedItems = 0, errors = 0;
        ModLog("  Scanning %d item templates (adding all equipment)...", dictCount);

        while (totalItems < dictCount + 100) {  // safety limit
            Il2CppObject* exc1 = NULL;

            // MoveNext
            __try {
                Il2CppObject* moveResult = IL2CPP.runtime_invoke(moveNext, enumerator2, NULL, &exc1);
                if (exc1) { ModLog("  [ERR] MoveNext exception at item %d", totalItems); break; }
                if (!moveResult) break;
                BOOL hasNext = *(BOOL*)IL2CPP.object_unbox(moveResult);
                if (!hasNext) break;
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                ModLog("  [CRASH] MoveNext SEH at item %d: 0x%08X", totalItems, GetExceptionCode());
                break;
            }

            // get_Current
            Il2CppObject* tmpl = NULL;
            __try {
                tmpl = IL2CPP.runtime_invoke(getCurrent, enumerator2, NULL, &exc1);
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                ModLog("  [CRASH] get_Current SEH at item %d: 0x%08X", totalItems, GetExceptionCode());
                errors++;
                continue;
            }
            if (!tmpl) { errors++; continue; }
            totalItems++;

            // Check if equipment by looking at Extensions.ContainsKey(EquipmentItemExt)
            if (equipExtTypeObj && itemTmplClass) {
                Il2CppFieldInfo* extField = IL2CPP.class_get_field_from_name(itemTmplClass, "Extensions");
                if (extField) {
                    int extOffset = (int)IL2CPP.field_get_offset(extField);
                    Il2CppObject* extensions = *(Il2CppObject**)((char*)tmpl + extOffset);
                    if (!extensions) continue;  // No extensions = skip

                    // First time: find ContainsKey
                    static Il2CppMethodInfo* s_containsKeyM = NULL;
                    if (!s_containsKeyM) {
                        Il2CppClass* extDictClass = *(Il2CppClass**)extensions;
                        Il2CppClass* c = extDictClass;
                        while (c && !s_containsKeyM) {
                            s_containsKeyM = IL2CPP.class_get_method_from_name(c, "ContainsKey", 1);
                            if (!s_containsKeyM) c = IL2CPP.class_get_parent(c);
                        }
                    }
                    if (s_containsKeyM) {
                        Il2CppObject* exc2 = NULL;
                        void* ckArgs[1] = { equipExtTypeObj };
                        Il2CppObject* hasKeyResult = IL2CPP.runtime_invoke(s_containsKeyM, extensions, ckArgs, &exc2);
                        if (!hasKeyResult || exc2) continue;
                        BOOL hasEquipExt = *(BOOL*)IL2CPP.object_unbox(hasKeyResult);
                        if (!hasEquipExt) continue;
                    }
                }
            }

            // Create item and push via KInventoryMethod.PushItem(entity, item)
            __try {
                int stack = 1, quality = 0;
                void* ciArgs[3] = { tmpl, &stack, &quality };
                Il2CppObject* exc3 = NULL;
                Il2CppObject* newItem = IL2CPP.runtime_invoke(createItem, itemSys, ciArgs, &exc3);
                if (!newItem || exc3) { errors++; continue; }

                // PushItem is static: PushItem(playerEntity, newItem)
                Il2CppObject* exc4 = NULL;
                void* pushArgs[2] = { playerEntity, newItem };
                IL2CPP.runtime_invoke(pushItemM, NULL, pushArgs, &exc4);
                if (!exc4) addedItems++;
                else errors++;
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                errors++;
            }

            if (addedItems > 0 && addedItems % 50 == 0)
                ModLog("    ... added %d items so far (%d scanned, %d errors)", addedItems, totalItems, errors);
        }

        ModLog("  [DONE] Scanned %d templates, added %d items, %d errors",
            totalItems, addedItems, errors);
        g_cosmeticsUnlocked = TRUE;

    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ModLog("  [CRASH] UnlockCosmetics exception: 0x%08X", GetExceptionCode());
    }
}

// =============================================================================
// F7: Super Free Build (Bypass Restrictions)
// =============================================================================

// Forward declaration
static void PatchMethod(const char* className, const char* methodName, int argCount, 
                       void** origPtr, unsigned char* origBytes, 
                       const unsigned char* patchBytes, int patchSize, BOOL enable);

static void MainThread_ToggleSuperBuild(void) {
    g_superBuildEnabled = !g_superBuildEnabled;
    ModLog("[F7/F8] Super Free Build: %s", g_superBuildEnabled ? "ON" : "OFF");

    if (!g_apiResolved) { ModLog("  [SKIP] API not resolved"); return; }

    __try {
    
    // Hook KConstructionUtil.CheckChange
    // Signature: public static bool CheckChange(CConstructionPart part, out string errorText)
    static void* g_origCheckChangePtr = NULL;
    static unsigned char g_origCheckChangeBytes[16];
    
    Il2CppClass* utilClass = FIND_CLASS("Construction", "KConstructionUtil");
    if (!utilClass) utilClass = FIND_CLASS("", "KConstructionUtil");
    
    if (utilClass) {
        Il2CppMethodInfo* checkM = IL2CPP.class_get_method_from_name(utilClass, "CheckChange", 2);
        if (checkM) {
            void* funcAddr = *(void**)checkM;
            if (funcAddr) {
                if (!g_origCheckChangePtr) {
                   g_origCheckChangePtr = funcAddr;
                   memcpy(g_origCheckChangeBytes, funcAddr, 16);
                   ModLog("  [INIT] Cached original CheckChange bytes");
                }
                
                DWORD oldProt;
                if (VirtualProtect(funcAddr, 16, PAGE_EXECUTE_READWRITE, &oldProt)) {
                    if (g_superBuildEnabled) {
                        // mov al, 1; ret
                        unsigned char patch[] = { 0xB0, 0x01, 0xC3 };
                        memcpy(funcAddr, patch, 3);
                        ModLog("  [OK] PATCHED CheckChange -> Always True (Restrictions Removed)");
                    } else {
                        memcpy(funcAddr, g_origCheckChangeBytes, 3); 
                        ModLog("  [OK] Restored CheckChange restriction logic");
                    }
                    VirtualProtect(funcAddr, 16, oldProt, &oldProt);
                } else {
                    ModLog("  [ERR] VirtualProtect failed");
                }
            } else {
                ModLog("  [ERR] CheckChange method pointer is null");
            }
        } else {
            ModLog("  [MISS] CheckChange method not found");
        }
    } else {
        ModLog("  [MISS] KConstructionUtil class not found");
    }

    // =========================================================================
    // God Mode: CheckOverlapValidation (Collision Check)
    // Signature: public static bool CheckOverlapValidation(...)
    // =========================================================================
    static void* g_origOverlapValPtr = NULL;
    static unsigned char g_origOverlapValBytes[16];

    if (utilClass) {
        Il2CppMethodInfo* m = IL2CPP.class_get_method_from_name(utilClass, "CheckOverlapValidation", 5);
        if (m) {
            void* funcAddr = *(void**)m;
            if (funcAddr) {
                if (!g_origOverlapValPtr) {
                    g_origOverlapValPtr = funcAddr;
                    memcpy(g_origOverlapValBytes, funcAddr, 16);
                }
                DWORD oldProt;
                if (VirtualProtect(funcAddr, 16, PAGE_EXECUTE_READWRITE, &oldProt)) {
                    if (g_superBuildEnabled) {
                        // mov al, 1; ret
                        unsigned char patch[] = { 0xB0, 0x01, 0xC3 };
                        memcpy(funcAddr, patch, 3);
                        ModLog("  [OK] PATCHED CheckOverlapValidation -> Always True");
                    } else {
                        memcpy(funcAddr, g_origOverlapValBytes, 3);
                    }
                    VirtualProtect(funcAddr, 16, oldProt, &oldProt);
                }
            }
        } else ModLog("  [MISS] CheckOverlapValidation not found");
    }

    // =========================================================================
    // God Mode: CheckOverlapping (Collision Error Text)
    // Signature: public static string CheckOverlapping(...)
    // =========================================================================
    static void* g_origOverlappingPtr = NULL;
    static unsigned char g_origOverlappingBytes[16];

    if (utilClass) {
        Il2CppMethodInfo* m = IL2CPP.class_get_method_from_name(utilClass, "CheckOverlapping", 5);
        if (m) {
            void* funcAddr = *(void**)m;
            if (funcAddr) {
                if (!g_origOverlappingPtr) {
                    g_origOverlappingPtr = funcAddr;
                    memcpy(g_origOverlappingBytes, funcAddr, 16);
                }
                DWORD oldProt;
                if (VirtualProtect(funcAddr, 16, PAGE_EXECUTE_READWRITE, &oldProt)) {
                    if (g_superBuildEnabled) {
                        // xor eax, eax; ret (return null string)
                        unsigned char patch[] = { 0x31, 0xC0, 0xC3 };
                        memcpy(funcAddr, patch, 3);
                        ModLog("  [OK] PATCHED CheckOverlapping -> Always Null");
                    } else {
                        memcpy(funcAddr, g_origOverlappingBytes, 3);
                    }
                    VirtualProtect(funcAddr, 16, oldProt, &oldProt);
                }
            }
        } else ModLog("  [MISS] CheckOverlapping not found");
    }

    // =========================================================================
    // God Mode: RegionAuthorityCheck (Ownership Check)
    // Signature: public static string RegionAuthorityCheck(...)
    // =========================================================================
    static void* g_origRegionAuthPtr = NULL;
    static unsigned char g_origRegionAuthBytes[16];

    if (utilClass) {
        Il2CppMethodInfo* m = IL2CPP.class_get_method_from_name(utilClass, "RegionAuthorityCheck", 5);
        if (m) {
            void* funcAddr = *(void**)m;
            if (funcAddr) {
                if (!g_origRegionAuthPtr) {
                    g_origRegionAuthPtr = funcAddr;
                    memcpy(g_origRegionAuthBytes, funcAddr, 16);
                }
                DWORD oldProt;
                if (VirtualProtect(funcAddr, 16, PAGE_EXECUTE_READWRITE, &oldProt)) {
                    if (g_superBuildEnabled) {
                         // xor eax, eax; ret (return null string)
                        unsigned char patch[] = { 0x31, 0xC0, 0xC3 };
                        memcpy(funcAddr, patch, 3);
                        ModLog("  [OK] PATCHED RegionAuthorityCheck -> Always Null");
                    } else {
                    }
                    VirtualProtect(funcAddr, 16, oldProt, &oldProt);
                }
            }
        } else ModLog("  [MISS] RegionAuthorityCheck not found");
    }

    // =========================================================================
    // God Mode: CheckConstructionCost (Material Check)
    // Signature: public static string CheckConstructionCost(...)
    // =========================================================================
    static void* g_origCheckCostPtr = NULL;
    static unsigned char g_origCheckCostBytes[16];

    if (utilClass) {
        Il2CppMethodInfo* m = IL2CPP.class_get_method_from_name(utilClass, "CheckConstructionCost", 5);
        if (m) {
            void* funcAddr = *(void**)m;
            if (funcAddr) {
                if (!g_origCheckCostPtr) {
                    g_origCheckCostPtr = funcAddr;
                    memcpy(g_origCheckCostBytes, funcAddr, 16);
                }
                DWORD oldProt;
                if (VirtualProtect(funcAddr, 16, PAGE_EXECUTE_READWRITE, &oldProt)) {
                    if (g_superBuildEnabled) {
                         // xor eax, eax; ret (return null string)
                        unsigned char patch[] = { 0x31, 0xC0, 0xC3 };
                        memcpy(funcAddr, patch, 3);
                        ModLog("  [OK] PATCHED CheckConstructionCost -> Always Null");
                    } else {
                        memcpy(funcAddr, g_origCheckCostBytes, 3);
                    }
                    VirtualProtect(funcAddr, 16, oldProt, &oldProt);
                    ModLog("  [ERR] CheckChange method pointer is null");
                }
            } else {
                ModLog("  [MISS] CheckChange method not found");
            }
        } else {
            ModLog("  [MISS] KConstructionUtil class not found");
        }

        // =========================================================================
        // God Mode: CheckOverlapValidation (Collision Check)
        // Signature: public static bool CheckOverlapValidation(...)
        // =========================================================================
        static void* g_origOverlapValPtr = NULL;
        static unsigned char g_origOverlapValBytes[16];

        if (utilClass) {
            Il2CppMethodInfo* m = IL2CPP.class_get_method_from_name(utilClass, "CheckOverlapValidation", 5);
            if (m) {
                void* funcAddr = *(void**)m;
                if (funcAddr) {
                    if (!g_origOverlapValPtr) {
                        g_origOverlapValPtr = funcAddr;
                        memcpy(g_origOverlapValBytes, funcAddr, 16);
                    }
                    DWORD oldProt;
                    if (VirtualProtect(funcAddr, 16, PAGE_EXECUTE_READWRITE, &oldProt)) {
                        if (g_superBuildEnabled) {
                            // mov al, 1; ret
                            unsigned char patch[] = { 0xB0, 0x01, 0xC3 };
                            memcpy(funcAddr, patch, 3);
                            ModLog("  [OK] PATCHED CheckOverlapValidation -> Always True");
                        } else {
                            memcpy(funcAddr, g_origOverlapValBytes, 3);
                        }
                        VirtualProtect(funcAddr, 16, oldProt, &oldProt);
                    }
                }
            } else ModLog("  [MISS] CheckOverlapValidation not found");
        }

        // =========================================================================
        // God Mode: CheckOverlapping (Collision Error Text)
        // Signature: public static string CheckOverlapping(...)
        // =========================================================================
        static void* g_origOverlappingPtr = NULL;
        static unsigned char g_origOverlappingBytes[16];

        if (utilClass) {
            Il2CppMethodInfo* m = IL2CPP.class_get_method_from_name(utilClass, "CheckOverlapping", 5);
            if (m) {
                void* funcAddr = *(void**)m;
                if (funcAddr) {
                    if (!g_origOverlappingPtr) {
                        g_origOverlappingPtr = funcAddr;
                        memcpy(g_origOverlappingBytes, funcAddr, 16);
                    }
                    DWORD oldProt;
                    if (VirtualProtect(funcAddr, 16, PAGE_EXECUTE_READWRITE, &oldProt)) {
                        if (g_superBuildEnabled) {
                            // xor eax, eax; ret (return null string)
                            unsigned char patch[] = { 0x31, 0xC0, 0xC3 };
                            memcpy(funcAddr, patch, 3);
                            ModLog("  [OK] PATCHED CheckOverlapping -> Always Null");
                        } else {
                            memcpy(funcAddr, g_origOverlappingBytes, 3);
                        }
                        VirtualProtect(funcAddr, 16, oldProt, &oldProt);
                    }
                }
            } else ModLog("  [MISS] CheckOverlapping not found");
        }

        // =========================================================================
        // God Mode: RegionAuthorityCheck (Ownership Check)
        // Signature: public static string RegionAuthorityCheck(...)
        // =========================================================================
        static void* g_origRegionAuthPtr = NULL;
        static unsigned char g_origRegionAuthBytes[16];

        if (utilClass) {
            Il2CppMethodInfo* m = IL2CPP.class_get_method_from_name(utilClass, "RegionAuthorityCheck", 5);
            if (m) {
                void* funcAddr = *(void**)m;
                if (funcAddr) {
                    if (!g_origRegionAuthPtr) {
                        g_origRegionAuthPtr = funcAddr;
                        memcpy(g_origRegionAuthBytes, funcAddr, 16);
                    }
                    DWORD oldProt;
                    if (VirtualProtect(funcAddr, 16, PAGE_EXECUTE_READWRITE, &oldProt)) {
                        if (g_superBuildEnabled) {
                             // xor eax, eax; ret (return null string)
                            unsigned char patch[] = { 0x31, 0xC0, 0xC3 };
                            memcpy(funcAddr, patch, 3);
                            ModLog("  [OK] PATCHED RegionAuthorityCheck -> Always Null");
                        } else {
                            memcpy(funcAddr, g_origRegionAuthBytes, 3);
                        }
                        VirtualProtect(funcAddr, 16, oldProt, &oldProt);
                    }
                }
            } else ModLog("  [MISS] RegionAuthorityCheck not found");
        }

        // =========================================================================
        // God Mode: CheckConstructionCost (Material Check)
        // Signature: public static string CheckConstructionCost(...)
        // =========================================================================
        static void* g_origCheckCostPtr = NULL;
        static unsigned char g_origCheckCostBytes[16];

        if (utilClass) {
            Il2CppMethodInfo* m = IL2CPP.class_get_method_from_name(utilClass, "CheckConstructionCost", 5);
            if (m) {
                void* funcAddr = *(void**)m;
                if (funcAddr) {
                    if (!g_origCheckCostPtr) {
                        g_origCheckCostPtr = funcAddr;
                        memcpy(g_origCheckCostBytes, funcAddr, 16);
                    }
                    DWORD oldProt;
                    if (VirtualProtect(funcAddr, 16, PAGE_EXECUTE_READWRITE, &oldProt)) {
                        if (g_superBuildEnabled) {
                             // xor eax, eax; ret (return null string)
                            unsigned char patch[] = { 0x31, 0xC0, 0xC3 };
                            memcpy(funcAddr, patch, 3);
                            ModLog("  [OK] PATCHED CheckConstructionCost -> Always Null");
                        } else {
                            memcpy(funcAddr, g_origCheckCostBytes, 3);
                        }
                        VirtualProtect(funcAddr, 16, oldProt, &oldProt);
                    }
                }
            }
        } else ModLog("  [MISS] CheckConstructionCost not found");
    }

    // =========================================================================
    // God Mode: DoConstructionCost (Material Consumption)
    // Signature: public static void DoConstructionCost(...)
    // =========================================================================
    static void* g_origDoCostPtr = NULL;
    static unsigned char g_origDoCostBytes[16];

    if (utilClass) {
        Il2CppMethodInfo* m = IL2CPP.class_get_method_from_name(utilClass, "DoConstructionCost", 5);
        if (m) {
            void* funcAddr = *(void**)m;
            if (funcAddr) {
                if (!g_origDoCostPtr) {
                    g_origDoCostPtr = funcAddr;
                    memcpy(g_origDoCostBytes, funcAddr, 16);
                }
                DWORD oldProt;
                if (VirtualProtect(funcAddr, 16, PAGE_EXECUTE_READWRITE, &oldProt)) {
                    if (g_superBuildEnabled) {
                         // ret (do nothing)
                        unsigned char patch[] = { 0xC3 };
                        memcpy(funcAddr, patch, 1);
                        ModLog("  [OK] PATCHED DoConstructionCost -> Do Nothing");
                    } else {
                        memcpy(funcAddr, g_origDoCostBytes, 1);
                    }
                    VirtualProtect(funcAddr, 16, oldProt, &oldProt);
                }
            }
        } else ModLog("  [MISS] DoConstructionCost not found");
    }

    // =========================================================================
    // Unlock All Blueprints: CUnlockedConstructionTemplates.IsLearn(ConstructionTemplate) -> bool
    // =========================================================================
    static void* g_origIsLearnPtr = NULL;
    static unsigned char g_origIsLearnBytes[16];
    
    // Patch: mov al, 1; ret (return true)
    unsigned char patchRetTrue[] = { 0xB0, 0x01, 0xC3 }; 

    PatchMethod("CUnlockedConstructionTemplates", "IsLearn", 1,
                &g_origIsLearnPtr, g_origIsLearnBytes,
                patchRetTrue, sizeof(patchRetTrue), g_superBuildEnabled);

    // =========================================================================
    // Layout Placement Bypass: KUIConstructionCtrl_Blueprint.CheckValidation(...) -> string
    // =========================================================================
    static void* g_origCheckValidPtr = NULL;
    static unsigned char g_origCheckValidBytes[16];

    // Patch: xor eax, eax; ret (return null)
    unsigned char patchRetNull[] = { 0x31, 0xC0, 0xC3 };

    PatchMethod("KUIConstructionCtrl_Blueprint", "CheckValidation", 3,
                &g_origCheckValidPtr, g_origCheckValidBytes,
                patchRetNull, sizeof(patchRetNull), g_superBuildEnabled);

    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ModLog("  [CRASH] SuperBuild Toggle exception: 0x%08X", GetExceptionCode());
    }
}

// =============================================================================
// F10: Global Material Bypass (Resources)
// Hooks inventory check/consumption methods to allow crafting/upgrading freely
// =============================================================================

static BOOL g_resourceBypassEnabled = FALSE;
static void* g_origGetTotalItemAmountPtr = NULL;
static unsigned char g_origGetTotalItemAmountBytes[16];
static void* g_origGetItemAmountPtr = NULL;
static unsigned char g_origGetItemAmountBytes[16];
static void* g_origRemoveItemPtr = NULL;
static unsigned char g_origRemoveItemBytes[16];
static void* g_origRemoveItemsPtr = NULL;
static unsigned char g_origRemoveItemsBytes[16];
static void* g_origHasItemPtr = NULL;
static unsigned char g_origHasItemBytes[16];

// Helper to patch a method
static void PatchMethod(const char* className, const char* methodName, int argCount, 
                       void** origPtr, unsigned char* origBytes, 
                       const unsigned char* patchBytes, int patchSize, BOOL enable) {
    if (!*origPtr) {
        // Find method
        Il2CppClass* klass = FIND_CLASS("Inventory", className);
        if (!klass) klass = FIND_CLASS("Construction", className); // Added Construction
        if (!klass) klass = FIND_CLASS("Construction.Extend.Impl", className); // Added
        if (!klass) klass = FIND_CLASS("UI", className); // Added
        if (!klass) klass = FIND_CLASS("", className);
        if (klass) {
            // runtime_class_init(klass); // usually not needed for finding methods but safe
            Il2CppMethodInfo* m = IL2CPP.class_get_method_from_name(klass, methodName, argCount);
            if (m) {
                *origPtr = *(void**)m;
                memcpy(origBytes, *origPtr, 16);
            } else {
                 ModLog("  [MISS] %s.%s(%d) not found", className, methodName, argCount);
            }
        } else {
            ModLog("  [MISS] Class %s not found", className);
        }
    }

    if (*origPtr) {
        DWORD oldProt;
        if (VirtualProtect(*origPtr, 16, PAGE_EXECUTE_READWRITE, &oldProt)) {
            if (enable) {
                memcpy(*origPtr, patchBytes, patchSize);
                ModLog("  [OK] PATCHED %s.%s", className, methodName);
            } else {
                memcpy(*origPtr, origBytes, patchSize); // catch size match
            }
            VirtualProtect(*origPtr, 16, oldProt, &oldProt);
        }
    }
}

static void MainThread_ToggleResourceBypass(void) {
    g_resourceBypassEnabled = !g_resourceBypassEnabled;
    ModLog("[F10] Global Resource Bypass: %s", g_resourceBypassEnabled ? "ON" : "OFF");
    
    if (!g_apiResolved) return;

    __try {
        // 1. KInventoryMethod.GetTotalItemAmount(KEcsEntity, ItemTemplate) -> int
        // Patch: mov eax, 999999; ret
        unsigned char patchRet999[] = { 0xB8, 0x3F, 0x42, 0x0F, 0x00, 0xC3 }; // mov eax, 999999; ret
        PatchMethod("KInventoryMethod", "GetTotalItemAmount", 2, 
                    &g_origGetTotalItemAmountPtr, g_origGetTotalItemAmountBytes, 
                    patchRet999, sizeof(patchRet999), g_resourceBypassEnabled);

        // 2. KInventoryMethod.GetItemAmount(KEcsEntity, ItemMatch, bool) -> int
        // Note: GetItemAmount has 3 args in C#, IL2CPP might vary. 
        // SDK says: public static int GetItemAmount(KEcsEntity entity, ItemMatch match, bool includeRemoteAccess = false)
        PatchMethod("KInventoryMethod", "GetItemAmount", 3, 
                    &g_origGetItemAmountPtr, g_origGetItemAmountBytes, 
                    patchRet999, sizeof(patchRet999), g_resourceBypassEnabled);
        
        // 3. KInventoryUtil.RemoveItem(CInventory, KItem, int, bool) -> bool
        // Patch: mov al, 1; ret (return true)
        unsigned char patchRetTrue[] = { 0xB0, 0x01, 0xC3 }; 
        PatchMethod("KInventoryUtil", "RemoveItem", 4, 
                    &g_origRemoveItemPtr, g_origRemoveItemBytes, 
                    patchRetTrue, sizeof(patchRetTrue), g_resourceBypassEnabled);

        // 4. KInventoryUtil.RemoveItems(CInventory, ItemMatch, int, bool) -> bool
        PatchMethod("KInventoryUtil", "RemoveItems", 4, 
                    &g_origRemoveItemsPtr, g_origRemoveItemsBytes, 
                    patchRetTrue, sizeof(patchRetTrue), g_resourceBypassEnabled);

        // 5. KGridItemSetUtil.HasItem(IGridItemSetProvider, KItem) -> bool
        // Generic HasItem check
        PatchMethod("KGridItemSetUtil", "HasItem", 2, 
                    &g_origHasItemPtr, g_origHasItemBytes, 
                    patchRetTrue, sizeof(patchRetTrue), g_resourceBypassEnabled);

        // 6. KGridItemSetUtil.GetAmount(IGridItemSetProvider, ItemMatch) -> int
        static void* g_origGridGetAmountPtr = NULL;
        static unsigned char g_origGridGetAmountBytes[16];
        PatchMethod("KGridItemSetUtil", "GetAmount", 2, 
                    &g_origGridGetAmountPtr, g_origGridGetAmountBytes, 
                    patchRet999, sizeof(patchRet999), g_resourceBypassEnabled);


        // 7. KConstructionBluePrint.DoConstructCost(KEcsEntity) -> void
        // Prevent material consumption for blueprints/layouts
        static void* g_origBpDoCostPtr = NULL;
        static unsigned char g_origBpDoCostBytes[16];
        unsigned char patchRetVoid[] = { 0xC3 }; // ret

        PatchMethod("KConstructionBluePrint", "DoConstructCost", 1,
                    &g_origBpDoCostPtr, g_origBpDoCostBytes,
                    patchRetVoid, sizeof(patchRetVoid), g_resourceBypassEnabled);

        // 8. KConstructionBluePrint.RemovePartIfMaterialNotEnough(GameObject) -> void
        // Prevent parts being removed if cost check fails
        static void* g_origBpRemovePartPtr = NULL;
        static unsigned char g_origBpRemovePartBytes[16];

        PatchMethod("KConstructionBluePrint", "RemovePartIfMaterialNotEnough", 1,
                    &g_origBpRemovePartPtr, g_origBpRemovePartBytes,
                    patchRetVoid, sizeof(patchRetVoid), g_resourceBypassEnabled);


    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ModLog("  [CRASH] Resource Bypass exception: 0x%08X", GetExceptionCode());
    }
}

// =============================================================================
// F11: Vertical Fly Mode (Unstuck)
// =============================================================================
static BOOL g_flyModeEnabled = FALSE;

static void MainThread_ToggleFlyMode(void) {
    g_flyModeEnabled = !g_flyModeEnabled;
    ModLog("[F11] Fly Mode: %s (Use Arrow UP/DOWN to move)", g_flyModeEnabled ? "ON" : "OFF");
}

static void MainThread_DoFlyMove(int direction) {
    // direction: 1 = UP, 2 = DOWN
    if (!g_apiResolved) return;
    
    __try {
        Il2CppClass* worldUtil = FIND_CLASS("World", "KWorldUtil");
        if (!worldUtil) worldUtil = FIND_CLASS("", "KWorldUtil");
        
        if (worldUtil) {
            IL2CPP.runtime_class_init(worldUtil); // Ensure static fields init
            
            // Get KWorldUtil.MainEntityTransform (static property/field)
            Il2CppObject* mainTransform = NULL;
            
            // Try property first
            Il2CppMethodInfo* getTrans = IL2CPP.class_get_method_from_name(worldUtil, "get_MainEntityTransform", 0);
            if (getTrans) {
                Il2CppObject* exc = NULL;
                mainTransform = IL2CPP.runtime_invoke(getTrans, NULL, NULL, &exc);
            }
            
            // Fallback to field if property fails
            if (!mainTransform) {
                 Il2CppFieldInfo* field = IL2CPP.class_get_field_from_name(worldUtil, "m_MainEntityTransform");
                 if (field) IL2CPP.field_static_get_value(field, &mainTransform);
            }

            if (mainTransform) {
                Il2CppClass* transClass = ((Il2CppObject*)mainTransform)->klass;
                
                // Get Position
                Il2CppMethodInfo* getPos = IL2CPP.class_get_method_from_name(transClass, "get_Position", 0);
                Il2CppMethodInfo* teleport = IL2CPP.class_get_method_from_name(transClass, "Teleport", 1);
                
                if (getPos && teleport) {
                     Il2CppObject* exc = NULL;
                     Il2CppObject* posObj = IL2CPP.runtime_invoke(getPos, mainTransform, NULL, &exc);
                     if (posObj) {
                         // Vector3 is struct, unbox
                         Vector3 pos = *(Vector3*)IL2CPP.object_unbox(posObj);
                         
                         // Increased speed for underwater escape
                         float moveAmt = 10.0f; 
                         if (direction == 2) moveAmt = -10.0f;
                         
                         pos.y += moveAmt;
                         
                         // Teleport
                         void* args[1] = { &pos };
                         IL2CPP.runtime_invoke(teleport, mainTransform, args, &exc);
                         // ModLog("  Fly: Y=%.2f", pos.y); // Spammy
                     }
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        // Suppress errors to avoid log spam during hold
    }
}

// Subclassed window procedure - runs on Unity's main thread
static LRESULT CALLBACK ModWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_MOD_CHEATSTORE) {
        MainThread_CheatStoreAction((int)wParam);
        return 0;
    }
    if (msg == WM_MOD_TIMESCALE) {
        // lParam carries the float as raw bits
        float scale;
        LONG bits = (LONG)lParam;
        memcpy(&scale, &bits, sizeof(float));
        MainThread_SetTimeScale(scale);
        return 0;
    }
    if (msg == WM_MOD_UNLOCKALL) {
        MainThread_UnlockCosmetics();
        return 0;
    }
    if (msg == WM_MOD_TIMEOFDAY) {
        int deltaHours = (int)(INT_PTR)wParam;
        MainThread_ChangeTimeOfDay(deltaHours);
        return 0;
    }
    if (msg == WM_MOD_BOMBARROW) {
        int enable = (int)wParam;
        MainThread_BombArrowBoost(enable);
        return 0;
    }
    if (msg == WM_MOD_SUPERBUILD) {
        MainThread_ToggleSuperBuild(); // Existing
        MainThread_ToggleBuildLimits(g_superBuildEnabled); // NEW
        return 0;
    }
    if (msg == WM_MOD_RESOURCE_BYPASS) {
        MainThread_ToggleResourceBypass();
        return 0;
    }
    if (msg == WM_MOD_RESOURCE_BYPASS) {
        MainThread_ToggleResourceBypass();
        return 0;
    }
    if (msg == WM_MOD_FLY_TOGGLE) {
        MainThread_ToggleFlyMode();
        return 0;
    }
    if (msg == WM_MOD_FLY_MOVE) {
        MainThread_DoFlyMove((int)wParam);
        return 0;
    }
    if (msg == WM_MOD_FORCE_BUILD) {
        MainThread_ForceBuildMode();
        return 0;
    }
    return CallWindowProcA(g_origWndProc, hwnd, msg, wParam, lParam);
}

// Find and subclass the Unity game window (call once)
static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    DWORD wndPid = 0;
    GetWindowThreadProcessId(hwnd, &wndPid);
    if (wndPid == (DWORD)lParam && IsWindowVisible(hwnd)) {
        char cls[64] = {0};
        GetClassNameA(hwnd, cls, sizeof(cls));
        if (strcmp(cls, "UnityWndClass") == 0) {
            g_gameWnd = hwnd;
            return FALSE; // stop enumerating
        }
    }
    return TRUE;
}

static void InstallMainThreadHook(void) {
    if (g_hookInstalled) return;

    // Try known class name first
    g_gameWnd = FindWindowA("UnityWndClass", NULL);

    // Enumerate windows as fallback
    if (!g_gameWnd) {
        DWORD pid = GetCurrentProcessId();
        EnumWindows(EnumWindowsProc, (LPARAM)pid);
    }

    if (!g_gameWnd) {
        ModLog("  [ERROR] Cannot find Unity game window!");
        return;
    }

    // Subclass the window - our ModWndProc will run on the main thread
    g_origWndProc = (WNDPROC)SetWindowLongPtrA(g_gameWnd, GWLP_WNDPROC, (LONG_PTR)ModWndProc);
    if (g_origWndProc) {
        g_hookInstalled = TRUE;
        ModLog("  [OK] Window subclassed! hwnd=0x%p origProc=0x%p", g_gameWnd, g_origWndProc);
    } else {
        ModLog("  [ERROR] SetWindowLongPtr failed (err=%lu)", GetLastError());
    }
}

static void DoToggleCheatStore(void) {
    g_cheatStoreOpen = !g_cheatStoreOpen;
    ModLog("[F9] Cheat Store: %s", g_cheatStoreOpen ? "OPENING" : "CLOSING");

    // Install hook on first use
    if (!g_hookInstalled) {
        InstallMainThreadHook();
    }

    if (g_hookInstalled && g_gameWnd) {
        // PostMessage is guaranteed cross-thread safe
        // action: 3=toggle (OpenOrClose), fallback to 1=open or 2=close
        int action = 3; // try OpenOrClose first
        BOOL posted = PostMessageA(g_gameWnd, WM_MOD_CHEATSTORE, (WPARAM)action, 0);
        if (posted) {
            ModLog("  Posted toggle to main thread (msg=0x%X, hwnd=0x%p)",
                WM_MOD_CHEATSTORE, g_gameWnd);
        } else {
            ModLog("  [ERROR] PostMessage failed (err=%lu)", GetLastError());
        }
    } else {
        ModLog("  [ERROR] No main thread hook - cannot safely open cheat store");
    }
}

// =============================================================================
// F4/PgUp/PgDn: Time Control
// =============================================================================

static void PostTimeScale(float scale) {
    // Install hook if needed
    if (!g_hookInstalled) InstallMainThreadHook();
    if (!g_hookInstalled || !g_gameWnd) {
        ModLog("  [ERROR] No main thread hook for time scale");
        return;
    }
    // Send float as raw bits in lParam
    LONG bits;
    memcpy(&bits, &scale, sizeof(float));
    PostMessageA(g_gameWnd, WM_MOD_TIMESCALE, 0, (LPARAM)bits);
}

static void DoTimeControlToggle(void) {
    g_timeControlActive = !g_timeControlActive;
    if (g_timeControlActive) {
        ModLog("[F4] Time Control: ON (speed: %s)", g_timeSpeedNames[g_timeSpeedIndex]);
        ModLog("  PgUp/PgDn = speed, Left/Right = time of day (+/-1hr)");
        // Apply current speed
        PostTimeScale(g_timeSpeeds[g_timeSpeedIndex]);
    } else {
        ModLog("[F4] Time Control: OFF (restoring 1.0x)");
        g_timeSpeedIndex = TIME_SPEED_DEFAULT;
        PostTimeScale(1.0f);
    }
}

static void DoTimeSpeedUp(void) {
    if (!g_timeControlActive) {
        ModLog("[PgUp] Time control not active - press F4 first");
        return;
    }
    if (g_timeSpeedIndex < TIME_SPEED_COUNT - 1) {
        g_timeSpeedIndex++;
    }
    ModLog("[PgUp] Speed: %s", g_timeSpeedNames[g_timeSpeedIndex]);
    PostTimeScale(g_timeSpeeds[g_timeSpeedIndex]);
}

static void DoTimeSlowDown(void) {
    if (!g_timeControlActive) {
        ModLog("[PgDn] Time control not active - press F4 first");
        return;
    }
    if (g_timeSpeedIndex > 0) {
        g_timeSpeedIndex--;
    }
    ModLog("[PgDn] Speed: %s", g_timeSpeedNames[g_timeSpeedIndex]);
    PostTimeScale(g_timeSpeeds[g_timeSpeedIndex]);
}

// =============================================================================
// Time of Day: Left/Right arrows (when time control active)
// =============================================================================

static void DoTimeForward(int hours) {
    if (!g_timeControlActive) {
        ModLog("[Time] Time control not active - press F4 first");
        return;
    }
    ModLog("[Time] Advancing time by +%d hour(s)...", hours);
    if (!g_hookInstalled) InstallMainThreadHook();
    if (g_hookInstalled && g_gameWnd) {
        PostMessageA(g_gameWnd, WM_MOD_TIMEOFDAY, (WPARAM)(INT_PTR)hours, 0);
    } else {
        ModLog("  [ERROR] No main thread hook");
    }
}

static void DoTimeBackward(int hours) {
    if (!g_timeControlActive) {
        ModLog("[Time] Time control not active - press F4 first");
        return;
    }
    ModLog("[Time] Rewinding time by -%d hour(s)...", hours);
    if (!g_hookInstalled) InstallMainThreadHook();
    if (g_hookInstalled && g_gameWnd) {
        PostMessageA(g_gameWnd, WM_MOD_TIMEOFDAY, (WPARAM)(INT_PTR)(-hours), 0);
    } else {
        ModLog("  [ERROR] No main thread hook");
    }
}

// =============================================================================
// F6: Toggle Bomb Arrow Boost
// =============================================================================

static void DoToggleBombBoost(void) {
    g_bombBoostActive = !g_bombBoostActive;
    ModLog("[F6] Bomb Arrow Boost: %s", g_bombBoostActive ? "ON" : "OFF");
    if (!g_hookInstalled) InstallMainThreadHook();
    if (g_hookInstalled && g_gameWnd) {
        PostMessageA(g_gameWnd, WM_MOD_BOMBARROW, (WPARAM)(g_bombBoostActive ? 1 : 0), 0);
        ModLog("  Dispatched to main thread");
    } else {
        ModLog("  [ERROR] No main thread hook");
    }
}

// =============================================================================
// F5: Unlock All Cosmetics
// =============================================================================

static void DoUnlockCosmetics(void) {
    if (g_cosmeticsUnlocked) {
        ModLog("[F5] Cosmetics already unlocked this session!");
        return;
    }
    ModLog("[F5] Unlocking all cosmetics/equipment...");

    if (!g_hookInstalled) InstallMainThreadHook();
    if (g_hookInstalled && g_gameWnd) {
        PostMessageA(g_gameWnd, WM_MOD_UNLOCKALL, 0, 0);
        ModLog("  Dispatched to main thread");
    } else {
        ModLog("  [ERROR] No main thread hook");
    }
}

// =============================================================================
// IL2CPP Worker Thread
// =============================================================================

static DWORD WINAPI IL2CppWorkerThread(LPVOID param) {
    while (1) {
        WaitForSingleObject(g_cmdEvent, INFINITE);
        int cmd = g_pendingCmd;
        __try {
            switch (cmd) {
            case CMD_DEVMODE:      DoDevMode(); break;
            case CMD_GMTOOL:       DoGMTool(); break;
            case CMD_DLC:          DoDLCBypass(); break;
            // CMD_ALL removed
            // F7 teleport removed
            case CMD_TOGGLE_BUILD: DoToggleFreeBuild(); break;
            // Cheat store uses timer-based main thread dispatch, not worker
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            ModLog("[FATAL] Unhandled exception in command %d (code: 0x%08X)", cmd, GetExceptionCode());
        }
    }
    return 0;
}

// =============================================================================
// API Resolution
// =============================================================================

static void TryResolveAPI(void) {
    if (g_apiResolved) return;
    if (IL2CPP_Init()) {
        g_apiResolved = TRUE;
        ModLog("[OK] GameAssembly.dll found at 0x%p", IL2CPP.hGameAssembly);
        ModLog("[OK] IL2CPP API resolved! (%d function pointers)",
            (int)(sizeof(IL2CPP) / sizeof(void*)) - 1);
    }
}

// =============================================================================
// Send a command to the worker thread (helper)
// =============================================================================

static void SendCommand(int cmd) {
    if (!g_apiResolved) {
        TryResolveAPI();
        if (!g_apiResolved) {
            ModLog("[ERROR] GameAssembly.dll not found! Is the game loaded?");
            return;
        }
    }
    g_pendingCmd = cmd;
    SetEvent(g_cmdEvent);
}

// =============================================================================
// Hotkey Thread
// =============================================================================

static DWORD WINAPI HotkeyThread(LPVOID param) {
    BOOL consoleShown = TRUE;

    while (1) {
        // HOME = toggle console
        if (GetAsyncKeyState(VK_HOME) & 1) {
            HWND con = GetConsoleWindow();
            if (con) {
                consoleShown = !consoleShown;
                ShowWindow(con, consoleShown ? SW_SHOW : SW_HIDE);
            }
        }

        // F1 = Enable Dev Mode
        if (GetAsyncKeyState(VK_F1) & 1) {
            ModLog("F1 pressed - Enable Dev Mode");
            SendCommand(CMD_DEVMODE);
        }

        // F2 = Give GM Tool
        if (GetAsyncKeyState(VK_F2) & 1) {
            ModLog("F2 pressed - Give GM Tool");
            SendCommand(CMD_GMTOOL);
        }

        // F3 = Unlock DLC
        if (GetAsyncKeyState(VK_F3) & 1) {
            ModLog("F3 pressed - Unlock DLC");
            SendCommand(CMD_DLC);
        }

        // F4 = Toggle Time Control
        if (GetAsyncKeyState(VK_F4) & 1) {
            DoTimeControlToggle();
        }

        // PgUp = Speed up time
        if (GetAsyncKeyState(VK_PRIOR) & 1) {
            DoTimeSpeedUp();
        }

        // PgDn = Slow down time
        if (GetAsyncKeyState(VK_NEXT) & 1) {
            DoTimeSlowDown();
        }

        // Right Arrow = Advance time +1 hour (when time control active)
        if (GetAsyncKeyState(VK_RIGHT) & 1) {
            if (g_timeControlActive) DoTimeForward(1);
        }

        // Left Arrow = Rewind time -1 hour (when time control active)
        if (GetAsyncKeyState(VK_LEFT) & 1) {
            if (g_timeControlActive) DoTimeBackward(1);
        }

        // F5 = Unlock all cosmetics
        if (GetAsyncKeyState(VK_F5) & 1) {
            DoUnlockCosmetics();
        }

        if (GetAsyncKeyState(VK_F6) & 1) {
            DoToggleBombBoost();
        }

        // F7 = Toggle Super Free Build (Bypass Restrictions)
        if (GetAsyncKeyState(VK_F7) & 1) {
            if (!g_hookInstalled) InstallMainThreadHook();
            if (g_hookInstalled && g_gameWnd) {
                PostMessageA(g_gameWnd, WM_MOD_SUPERBUILD, 0, 0);
            } else {
                ModLog("[F7] Error: No main thread hook active");
            }
        }

        // F8 = Toggle Super Free Build (Alternate key)
        if (GetAsyncKeyState(VK_F8) & 1) {
             if (!g_hookInstalled) InstallMainThreadHook();
            if (g_hookInstalled && g_gameWnd) {
                PostMessageA(g_gameWnd, WM_MOD_SUPERBUILD, 0, 0);
            } else {
                ModLog("[F8] Error: No main thread hook active");
            }
        }

        // F9 = Toggle cheat store
        if (GetAsyncKeyState(VK_F9) & 1) {
            DoToggleCheatStore();
        }

        // F10 = Toggle Resource Bypass
        if (GetAsyncKeyState(VK_F10) & 1) {
            if (!g_hookInstalled) InstallMainThreadHook();
            if (g_hookInstalled && g_gameWnd) {
                PostMessageA(g_gameWnd, WM_MOD_RESOURCE_BYPASS, 0, 0);
            } else {
                ModLog("[F10] Error: No main thread hook active");
            }
        }

        // F11 = Toggle Fly Mode
        if (GetAsyncKeyState(VK_F11) & 1) {
            if (!g_hookInstalled) InstallMainThreadHook();
            if (g_hookInstalled && g_gameWnd) {
                PostMessageA(g_gameWnd, WM_MOD_FLY_TOGGLE, 0, 0);
            }
        }

        // F12 = Force Build Mode
        if (GetAsyncKeyState(VK_F12) & 1) {
            if (!g_hookInstalled) InstallMainThreadHook();
            if (g_hookInstalled && g_gameWnd) {
                PostMessageA(g_gameWnd, WM_MOD_FORCE_BUILD, 0, 0);
            }
        }
        
        // Handle Fly Mode Inputs (Arrow Keys)
        if (g_flyModeEnabled && g_hookInstalled && g_gameWnd) {
            if (GetAsyncKeyState(VK_UP) & 0x8000) {
                PostMessageA(g_gameWnd, WM_MOD_FLY_MOVE, 1, 0);
            }
            else if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
                PostMessageA(g_gameWnd, WM_MOD_FLY_MOVE, 2, 0);
            }
        }

        Sleep(50); // Faster polling for smoother flying
    }
    return 0;
}

// =============================================================================
// Heartbeat Thread
// =============================================================================

static DWORD WINAPI HeartbeatThread(LPVOID param) {
    BOOL lastApi = FALSE;
    BOOL lastAttached = FALSE;
    while (1) {
        Sleep(5000);
        if (g_apiResolved != lastApi || g_threadAttached != lastAttached) {
            ModLog("[STATUS] API: %s | Attached: %s",
                g_apiResolved ? "YES" : "no",
                g_threadAttached ? "YES" : "no");
            lastApi = g_apiResolved;
            lastAttached = g_threadAttached;
        }
    }
    return 0;
}

// =============================================================================
// Main Mod Thread
// =============================================================================

static DWORD WINAPI ModThread(LPVOID param) {
    g_startTick = GetTickCount();
    InitializeCriticalSection(&g_logLock);

    // Setup log file
    char modDir[MAX_PATH];
    GetModuleFileNameA(NULL, modDir, MAX_PATH);
    char* lastSlash = strrchr(modDir, '\\');
    if (lastSlash) *(lastSlash + 1) = 0;
    snprintf(g_logPath, sizeof(g_logPath), "%sStarsandIslandMod\\mod_log.txt", modDir);
    char logDirPath[MAX_PATH];
    snprintf(logDirPath, sizeof(logDirPath), "%sStarsandIslandMod", modDir);
    CreateDirectoryA(logDirPath, NULL);
    g_logFile = fopen(g_logPath, "w");

    // Console
    AllocConsole();
    SetConsoleTitleA("Starsand Island Mod v3.7");
    freopen("CONOUT$", "w", stdout);
    freopen("CONIN$", "r", stdin);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hOut, FOREGROUND_GREEN | FOREGROUND_INTENSITY);

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    ModLog("=============================================");
    ModLog("  Starsand Island Standalone Mod v3.7");
    ModLog("  No MelonLoader / No BepInEx required");
    ModLog("=============================================");
    ModLog("Process: %s (PID %d)", exePath, GetCurrentProcessId());
    ModLog("");
    ModLog("  HOTKEYS:");
    ModLog("  F1    = Enable Dev Mode");
    ModLog("  F2    = Give GM Tool (one-time)");
    ModLog("  F3    = Unlock DLC (one-time)");
    ModLog("  F4    = Toggle Time Control");
    ModLog("  F5    = Unlock All Cosmetics");
    ModLog("  F6    = Toggle Bomb Arrow Boost");
    ModLog("  PgUp  = Speed up time");
    ModLog("  PgDn  = Slow down time");
    ModLog("  Right = Advance +1 hour");
    ModLog("  Left  = Rewind  -1 hour");
    ModLog("  F7/F8 = Toggle Super Free Build (Bypass All Restrictions)");
    ModLog("  F9    = Toggle Cheat Store");
    ModLog("  F10   = Toggle Resource Bypass (999k items / No consume)");
    ModLog("  F11   = Fly Mode (Toggle ON, then hold UP/DOWN arrows)");
    ModLog("  HOME  = Hide/Show console");
    ModLog("");

    // Events
    g_cmdEvent = CreateEventA(NULL, FALSE, FALSE, NULL);

    // Threads
    CreateThread(NULL, 0, HotkeyThread, NULL, 0, NULL);
    CreateThread(NULL, 0, HeartbeatThread, NULL, 0, NULL);

    // Wait for GameAssembly.dll
    ModLog("Waiting for GameAssembly.dll...");
    for (int i = 0; i < 240; i++) {
        TryResolveAPI();
        if (g_apiResolved) break;
        Sleep(500);
        if (i % 20 == 19) ModLog("  Still waiting... (%ds)", (i + 1) / 2);
    }

    if (!g_apiResolved) {
        ModLog("[INFO] GameAssembly.dll not found yet. Will retry on key press.");
    }

    ModLog("");
    ModLog(">>> Ready! Use F1-F4, F8-F9 hotkeys. <<<");
    ModLog("");

    // Worker thread
    CreateThread(NULL, 0, IL2CppWorkerThread, NULL, 0, NULL);
    return 0;
}

// =============================================================================
// version.dll Proxy Exports
// =============================================================================

__declspec(dllexport) BOOL WINAPI proxy_GetFileVersionInfoA(LPCSTR a, DWORD b, DWORD c, LPVOID d) {
    return o_GetFileVersionInfoA ? o_GetFileVersionInfoA(a, b, c, d) : FALSE;
}
__declspec(dllexport) BOOL WINAPI proxy_GetFileVersionInfoByHandle(DWORD a, LPCWSTR b, DWORD c, LPVOID d) {
    return o_GetFileVersionInfoByHandle ? o_GetFileVersionInfoByHandle(a, b, c, d) : FALSE;
}
__declspec(dllexport) BOOL WINAPI proxy_GetFileVersionInfoExA(DWORD a, LPCSTR b, DWORD c, DWORD d, LPVOID e) {
    return o_GetFileVersionInfoExA ? o_GetFileVersionInfoExA(a, b, c, d, e) : FALSE;
}
__declspec(dllexport) BOOL WINAPI proxy_GetFileVersionInfoExW(DWORD a, LPCWSTR b, DWORD c, DWORD d, LPVOID e) {
    return o_GetFileVersionInfoExW ? o_GetFileVersionInfoExW(a, b, c, d, e) : FALSE;
}
__declspec(dllexport) DWORD WINAPI proxy_GetFileVersionInfoSizeA(LPCSTR a, LPDWORD b) {
    return o_GetFileVersionInfoSizeA ? o_GetFileVersionInfoSizeA(a, b) : 0;
}
__declspec(dllexport) DWORD WINAPI proxy_GetFileVersionInfoSizeExA(DWORD a, LPCSTR b, LPDWORD c) {
    return o_GetFileVersionInfoSizeExA ? o_GetFileVersionInfoSizeExA(a, b, c) : 0;
}
__declspec(dllexport) DWORD WINAPI proxy_GetFileVersionInfoSizeExW(DWORD a, LPCWSTR b, LPDWORD c) {
    return o_GetFileVersionInfoSizeExW ? o_GetFileVersionInfoSizeExW(a, b, c) : 0;
}
__declspec(dllexport) DWORD WINAPI proxy_GetFileVersionInfoSizeW(LPCWSTR a, LPDWORD b) {
    return o_GetFileVersionInfoSizeW ? o_GetFileVersionInfoSizeW(a, b) : 0;
}
__declspec(dllexport) BOOL WINAPI proxy_GetFileVersionInfoW(LPCWSTR a, DWORD b, DWORD c, LPVOID d) {
    return o_GetFileVersionInfoW ? o_GetFileVersionInfoW(a, b, c, d) : FALSE;
}
__declspec(dllexport) DWORD WINAPI proxy_VerFindFileA(DWORD a, LPCSTR b, LPCSTR c, LPCSTR d, LPSTR e, PUINT f, LPSTR g, PUINT h) {
    return o_VerFindFileA ? o_VerFindFileA(a, b, c, d, e, f, g, h) : 0;
}
__declspec(dllexport) DWORD WINAPI proxy_VerFindFileW(DWORD a, LPCWSTR b, LPCWSTR c, LPCWSTR d, LPWSTR e, PUINT f, LPWSTR g, PUINT h) {
    return o_VerFindFileW ? o_VerFindFileW(a, b, c, d, e, f, g, h) : 0;
}
__declspec(dllexport) DWORD WINAPI proxy_VerInstallFileA(DWORD a, LPCSTR b, LPCSTR c, LPCSTR d, LPCSTR e, LPCSTR f, LPSTR g, PUINT h) {
    return o_VerInstallFileA ? o_VerInstallFileA(a, b, c, d, e, f, g, h) : 0;
}
__declspec(dllexport) DWORD WINAPI proxy_VerInstallFileW(DWORD a, LPCWSTR b, LPCWSTR c, LPCWSTR d, LPCWSTR e, LPCWSTR f, LPWSTR g, PUINT h) {
    return o_VerInstallFileW ? o_VerInstallFileW(a, b, c, d, e, f, g, h) : 0;
}
__declspec(dllexport) BOOL WINAPI proxy_VerQueryValueA(LPCVOID a, LPCSTR b, LPVOID* c, PUINT d) {
    return o_VerQueryValueA ? o_VerQueryValueA(a, b, c, d) : FALSE;
}
__declspec(dllexport) BOOL WINAPI proxy_VerQueryValueW(LPCVOID a, LPCWSTR b, LPVOID* c, PUINT d) {
    return o_VerQueryValueW ? o_VerQueryValueW(a, b, c, d) : FALSE;
}

// =============================================================================
// DLL Entry Point
// =============================================================================

static BOOL IsGameProcess(void) {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char* baseName = strrchr(exePath, '\\');
    if (baseName) baseName++; else baseName = exePath;
    return (_stricmp(baseName, "StarsandIsland.exe") == 0);
}

// =============================================================================
// Build Limit Removal: Camera, Range, Extension
// =============================================================================

static void* g_origCheckConstraintPtr = NULL;
static unsigned char g_origCheckConstraintBytes[16];
static void* g_origCheckRangePtr = NULL;
static unsigned char g_origCheckRangeBytes[16];
static void* g_origLimitCheckPtr = NULL;
static unsigned char g_origLimitCheckBytes[16];
static void* g_origCheckChangePtr = NULL;
static unsigned char g_origCheckChangeBytes[16];
static void* g_origIsLearnPtr = NULL; 
static unsigned char g_origIsLearnBytes[16];
static void* g_origIsUnlockedPtr = NULL;
static unsigned char g_origIsUnlockedBytes[16];
static void* g_origCheckEnterPtr = NULL;
static unsigned char g_origCheckEnterBytes[16];

// Config Backup
static BOOL g_configBackedUp = FALSE;
static float g_backupMaxDist, g_backupOverlapErr, g_backupFreeRay, g_backupFreeClamp, g_backupCamHeight;

static void MainThread_ForceBuildMode() {
    if (!g_apiResolved) return;
    ModLog("[FORCE] Attempting to force enter build mode via F12...");

    Il2CppClass* uiClass = FIND_CLASS("UI", "UIHudEntranceView");
    if (!uiClass) { ModLog("  [ERR] UIHudEntranceView class not found"); return; }
    
    // Get Instance
    Il2CppMethodInfo* getInst = IL2CPP.class_get_method_from_name(uiClass, "get_Instance", 0);
    if (!getInst) { ModLog("  [ERR] UIHudEntranceView.get_Instance not found"); return; }
    
    Il2CppObject* instance = IL2CPP.runtime_invoke(getInst, NULL, NULL, NULL);
    if (!instance) { ModLog("  [ERR] UIHudEntranceView.Instance is null (UI not initialized?)"); return; }

    // Call OpenConstruction
    Il2CppMethodInfo* openConst = IL2CPP.class_get_method_from_name(uiClass, "OpenConstruction", 0);
    if (!openConst) { ModLog("  [ERR] OpenConstruction method not found"); return; }
    
    Il2CppObject* exc = NULL;
    IL2CPP.runtime_invoke(openConst, instance, NULL, &exc);
    if (exc) {
        ModLog("  [ERR] OpenConstruction threw exception!");
    } else {
        ModLog("  [OK] Forced OpenConstruction call!");
    }
}

static void MainThread_ToggleBuildLimits(BOOL enable) {
    if (!g_apiResolved) return;
    
    __try {
        // 1. Hook KTFMMatrixConstraint.CheckConstraint -> Always True (Unlimited extension)
        PatchMethod("KTFMMatrixConstraint", "CheckConstraint", 2,
                    &g_origCheckConstraintPtr, g_origCheckConstraintBytes,
                    (unsigned char*)"\xB0\x01\xC3", 3, enable); // mov al, 1; ret

        // 2. Hook KConstructionUtil.CheckRange -> Always Null (Select anywhere)
        PatchMethod("KConstructionUtil", "CheckRange", 4,
                    &g_origCheckRangePtr, g_origCheckRangeBytes,
                    (unsigned char*)"\x31\xC0\xC3", 3, enable); // xor eax, eax; ret

        // 3. Hook KConstructionUtil.IsReachingConstructionLimit -> Always False (Build anywhere)
        PatchMethod("KConstructionUtil", "IsReachingConstructionLimit", 2,
                    &g_origLimitCheckPtr, g_origLimitCheckBytes,
                    (unsigned char*)"\x31\xC0\xC3", 3, enable); // xor eax, eax; ret 

        // 5. Hook KConstructionUtil.CheckChange -> Always True (Move/Delete Quest Items)
        PatchMethod("KConstructionUtil", "CheckChange", 2,
                    &g_origCheckChangePtr, g_origCheckChangeBytes,
                    (unsigned char*)"\xB0\x01\xC3", 3, enable); // mov al, 1; ret

        // 6. Hook KConstructionUtil.IsConstructionUnlocked -> Always True (Double check)
        PatchMethod("KConstructionUtil", "IsConstructionUnlocked", 1,
                    &g_origIsUnlockedPtr, g_origIsUnlockedBytes,
                    (unsigned char*)"\xB0\x01\xC3", 3, enable); // mov al, 1; ret

        // 7. Hook UIHudEntranceView.CheckAndHandleConstruction -> REVERTED (Broke build mode)
        // PatchMethod("UIHudEntranceView", "CheckAndHandleConstruction", 1, 
        //            &g_origCheckEnterPtr, g_origCheckEnterBytes,
        //            (unsigned char*)"\x31\xC0\xC3", 3, enable);

        // 4. Modify ConstructionConfig (Camera & Height Limits)
        Il2CppClass* configClass = FIND_CLASS("Construction", "ConstructionConfig");
        if (configClass) {
            IL2CPP.runtime_class_init(configClass);
            Il2CppMethodInfo* getInst = IL2CPP.class_get_method_from_name(configClass, "get_Instance", 0);
            if (getInst) {
                Il2CppObject* inst = IL2CPP.runtime_invoke(getInst, NULL, NULL, NULL);
                if (inst) {
                    // Fields
                    Il2CppFieldInfo* fMaxDist = IL2CPP.class_get_field_from_name(configClass, "MaxConstructionDistance");
                    Il2CppFieldInfo* fOverlap = IL2CPP.class_get_field_from_name(configClass, "OverlapError");
                    Il2CppFieldInfo* fFreeRay = IL2CPP.class_get_field_from_name(configClass, "FreeModeRayDistance");
                    Il2CppFieldInfo* fFreeClamp = IL2CPP.class_get_field_from_name(configClass, "FreeModeClampDistance");
                    Il2CppFieldInfo* fCamHeight = IL2CPP.class_get_field_from_name(configClass, "CameraAdditionalHeight");
                    
                    if (fMaxDist && fOverlap && fFreeRay && fFreeClamp && fCamHeight) {
                        if (enable) {
                            float currentMaxDist = 0;
                            IL2CPP.field_get_value(inst, fMaxDist, &currentMaxDist);
                            
                            if (currentMaxDist < 9000.0f) { // Only backup if not already patched
                                IL2CPP.field_get_value(inst, fMaxDist, &g_backupMaxDist);
                                IL2CPP.field_get_value(inst, fOverlap, &g_backupOverlapErr);
                                IL2CPP.field_get_value(inst, fFreeRay, &g_backupFreeRay);
                                IL2CPP.field_get_value(inst, fFreeClamp, &g_backupFreeClamp);
                                IL2CPP.field_get_value(inst, fCamHeight, &g_backupCamHeight);
                                g_configBackedUp = TRUE;
                            }
                            
                            float bigVal = 99999.0f;
                            float smallVal = 0.0f; // No error distance
                            IL2CPP.field_set_value(inst, fMaxDist, &bigVal);
                            IL2CPP.field_set_value(inst, fOverlap, &smallVal); 
                            IL2CPP.field_set_value(inst, fFreeRay, &bigVal);
                            IL2CPP.field_set_value(inst, fFreeClamp, &bigVal);
                            IL2CPP.field_set_value(inst, fCamHeight, &bigVal);
                            ModLog("  [OK] ConstructionConfig limits removed (incl. Height)");
                        } else if (g_configBackedUp) {
                             IL2CPP.field_set_value(inst, fMaxDist, &g_backupMaxDist);
                             IL2CPP.field_set_value(inst, fOverlap, &g_backupOverlapErr);
                             IL2CPP.field_set_value(inst, fFreeRay, &g_backupFreeRay);
                             IL2CPP.field_set_value(inst, fFreeClamp, &g_backupFreeClamp);
                             IL2CPP.field_set_value(inst, fCamHeight, &g_backupCamHeight);
                             ModLog("  [OK] ConstructionConfig limits restored");
                        }
                    }
                }
            }
        }

    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ModLog("  [CRASH] ToggleBuildLimits exception");
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        if (!LoadRealVersionDll()) return FALSE;
        if (IsGameProcess()) CreateThread(NULL, 0, ModThread, NULL, 0, NULL);
        break;
    case DLL_PROCESS_DETACH:
        if (g_logFile) { ModLog("=== Mod Shutting Down ==="); fclose(g_logFile); }
        if (g_realVersionDll) FreeLibrary(g_realVersionDll);
        break;
    }
    return TRUE;
}
