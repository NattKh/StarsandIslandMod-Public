#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "version_proxy.h"
#include "il2cpp_api.h"

typedef struct { float x, y, z; } Vector3;


static FILE* g_logFile = NULL;
static char g_logPath[MAX_PATH] = {0};
static CRITICAL_SECTION g_logLock;
static DWORD g_startTick = 0;
static volatile BOOL g_apiResolved = FALSE;
static volatile BOOL g_threadAttached = FALSE;
static Il2CppThread* g_il2cppThread = NULL;

static BOOL g_devModeEnabled = FALSE;
static BOOL g_gmToolGiven = FALSE;
static BOOL g_freeBuildEnabled = FALSE;
static BOOL g_superBuildEnabled = FALSE;

static BOOL g_timeControlActive = FALSE;
static int  g_timeSpeedIndex = 3;
static const float g_timeSpeeds[] = { 0.0f, 0.25f, 0.5f, 1.0f, 2.0f, 3.0f, 5.0f, 10.0f };
static const char* g_timeSpeedNames[] = { "PAUSED", "0.25x", "0.5x", "1.0x", "2.0x", "3.0x", "5.0x", "10.0x" };
#define TIME_SPEED_COUNT 8
#define TIME_SPEED_DEFAULT 3


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


static HANDLE g_cmdEvent = NULL;

#define CMD_DEVMODE      0
#define CMD_GMTOOL       1
#define CMD_TOGGLE_BUILD 5
static volatile int g_pendingCmd = CMD_DEVMODE;

#define FIND_CLASS(ns, name) IL2CPP_FindClass(NULL, ns, name)

static void MainThread_ForceBuildMode(void);
static void MainThread_ToggleBuildLimits(BOOL enable);

static void EnsureAttached(void) {
    if (g_threadAttached) return;
    Il2CppDomain* domain = IL2CPP.domain_get();
    if (!domain) { ModLog("[ERROR] IL2CPP domain is NULL!"); return; }
    g_il2cppThread = IL2CPP.thread_attach(domain);
    g_threadAttached = TRUE;
    ModLog("Attached to IL2CPP GC (thread: 0x%p)", g_il2cppThread);
}


static void DoDevMode(void) {
    EnsureAttached();
    ModLog("");
    ModLog("=== Enabling Dev Mode ===");

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


#define WM_MOD_TIMESCALE   (WM_APP + 0xBEF0)
#define WM_MOD_TIMEOFDAY   (WM_APP + 0xBEF2)
#define WM_MOD_BOMBARROW   (WM_APP + 0xBEF3)
#define WM_MOD_SUPERBUILD  (WM_APP + 0xBEF4)
#define WM_MOD_RESOURCE_BYPASS (WM_APP + 0xBEF5)
#define WM_MOD_FLY_TOGGLE      (WM_APP + 0xBEF6)
#define WM_MOD_FLY_MOVE        (WM_APP + 0xBEF7)
#define WM_MOD_FORCE_BUILD     (WM_APP + 0xBEF8)
static HWND g_gameWnd = NULL;
static WNDPROC g_origWndProc = NULL;
static volatile BOOL g_hookInstalled = FALSE;

static void MainThread_SetTimeScale(float scale) {
    ModLog("  [MAIN THREAD] Setting time scale to %.2f (tid=%lu)", scale, GetCurrentThreadId());

    if (!g_apiResolved) { ModLog("  [SKIP] API not resolved"); return; }

    int success = 0;

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

    __try {
        Il2CppClass* ktScale = FIND_CLASS("GameFramework", "KTimeScale");
        if (!ktScale) ktScale = FIND_CLASS("", "KTimeScale");
        if (ktScale) {
            IL2CPP.runtime_class_init(ktScale);
            Il2CppMethodInfo* setLevel = IL2CPP.class_get_method_from_name(ktScale, "set_Level", 1);
            if (setLevel) {
                float val = scale;
                void* args[1] = { &val };
                Il2CppObject* exc = NULL;
                IL2CPP.runtime_invoke(setLevel, NULL, args, &exc);
                if (!exc) { ModLog("  [OK] KTimeScale.Level = %.2f", scale); success++; }
                else ModLog("  [WARN] set_Level threw exception");
            } else {
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


static void MainThread_ChangeTimeOfDay(int deltaHours) {
    ModLog("  [MAIN THREAD] Changing time of day by %+d hours", deltaHours);
    if (!g_apiResolved) return;

    Il2CppObject* exc0 = NULL;

    __try {
        Il2CppClass* timeSysClass = FIND_CLASS("GameTime", "CGlobalTimeSystem");
        if (!timeSysClass) timeSysClass = FIND_CLASS("", "CGlobalTimeSystem");
        if (!timeSysClass) { ModLog("  [MISS] CGlobalTimeSystem not found"); return; }

        IL2CPP.runtime_class_init(timeSysClass);

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

            Il2CppFieldInfo* nowField = IL2CPP.class_get_field_from_name(timeSysClass, "Now");
            if (nowField) {
                int nowOffset = IL2CPP.field_get_offset(nowField);
                long long* ticksPtr = (long long*)((char*)timeSys + nowOffset);
                long long oldTicks = *ticksPtr;
                long long hourTicks = (long long)deltaHours * 3600LL * 10000000LL;
                *ticksPtr = oldTicks + hourTicks;

                long long totalSecs = *ticksPtr / 10000000LL;
                int hours = (int)((totalSecs / 3600) % 24);
                ModLog("  [OK] CGlobalTimeSystem.Now updated.");

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

                return;
            } else ModLog("  [MISS] CGlobalTimeSystem.Now field not found");
        }

        ModLog("  Trying KTimeDebugOption fallback...");
        Il2CppClass* timeDbgClass = FIND_CLASS("GameTime", "KTimeDebugOption");
        if (!timeDbgClass) timeDbgClass = FIND_CLASS("", "KTimeDebugOption");
        if (timeDbgClass) {
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
                                        Il2CppMethodInfo* getH = IL2CPP.class_get_method_from_name(optCls, "get_CurHour", 0);
                                        Il2CppMethodInfo* setH = IL2CPP.class_get_method_from_name(optCls, "set_CurHour", 1);
                                        if (getH && setH) {
                                            Il2CppObject* hObj = IL2CPP.runtime_invoke(getH, opt, NULL, &exc0);
                                            if (hObj) {
                                                int curH = *(int*)IL2CPP.object_unbox(hObj);
                                                int newH = curH + deltaHours;
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
        Il2CppClass* kWorldUtilClass = FIND_CLASS("World", "KWorldUtil");
        if (!kWorldUtilClass) kWorldUtilClass = FIND_CLASS("", "KWorldUtil");
        Il2CppObject* playerEntity = NULL;
        if (kWorldUtilClass) {
            IL2CPP.runtime_class_init(kWorldUtilClass);
            Il2CppMethodInfo* getMain = IL2CPP.class_get_method_from_name(kWorldUtilClass, "get_MainEntity", 0);
            if (getMain) playerEntity = IL2CPP.runtime_invoke(getMain, NULL, NULL, &exc0);
        }
        if (!playerEntity) { ModLog("  [MISS] No player entity"); return; }

        Il2CppClass* hunterClass = FIND_CLASS("GameWorld.Hunting", "CHunter");
        if (!hunterClass) hunterClass = FIND_CLASS("", "CHunter");
        Il2CppObject* hunter = NULL;
        if (hunterClass) {
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
                    *accumPtr = 0.05f;
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

        Il2CppClass* explosionClass = FIND_CLASS("External.Physics", "KExplosion");
        if (!explosionClass) explosionClass = FIND_CLASS("", "KExplosion");
        if (explosionClass) {
            Il2CppMethodInfo* getRange = IL2CPP.class_get_method_from_name(explosionClass, "get_Range", 0);
            if (getRange) {
                void* methodPtr = *(void**)getRange;
                if (methodPtr && methodPtr != (void*)1 && methodPtr != (void*)-1) {
                    g_explosionGetRangePtr = methodPtr;
                    ModLog("  [OK] KExplosion.get_Range at: 0x%p", methodPtr);

                    if (enable) {
                        if (!g_origExplosionSaved) {
                            DWORD oldProtect;
                            VirtualProtect(methodPtr, 16, PAGE_EXECUTE_READWRITE, &oldProtect);
                            memcpy(g_origExplosionGetRange, methodPtr, 16);
                            g_origExplosionSaved = TRUE;
                            VirtualProtect(methodPtr, 16, oldProtect, &oldProtect);
                            ModLog("  [OK] Saved original get_Range bytes");
                        }

                        BYTE patch[] = { 0xB8, 0x00, 0x00, 0xC8, 0x41,
                                         0x66, 0x0F, 0x6E, 0xC0,
                                         0xC3 };
                        DWORD oldProtect;
                        VirtualProtect(methodPtr, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect);
                        memcpy(methodPtr, patch, sizeof(patch));
                        VirtualProtect(methodPtr, sizeof(patch), oldProtect, &oldProtect);
                        ModLog("  [OK] Patched get_Range -> always returns 25.0 (big blast)");
                    } else {
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


static void PatchMethod(const char* className, const char* methodName, int argCount, 
                       void** origPtr, unsigned char* origBytes, 
                       const unsigned char* patchBytes, int patchSize, BOOL enable);

static void MainThread_ToggleSuperBuild(void) {
    g_superBuildEnabled = !g_superBuildEnabled;
    ModLog("[F7/F8] Super Free Build: %s", g_superBuildEnabled ? "ON" : "OFF");

    if (!g_apiResolved) { ModLog("  [SKIP] API not resolved"); return; }

    __try {
    
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

    static void* g_origIsLearnPtr = NULL;
    static unsigned char g_origIsLearnBytes[16];
    
    unsigned char patchRetTrue[] = { 0xB0, 0x01, 0xC3 }; 

    PatchMethod("CUnlockedConstructionTemplates", "IsLearn", 1,
                &g_origIsLearnPtr, g_origIsLearnBytes,
                patchRetTrue, sizeof(patchRetTrue), g_superBuildEnabled);

    static void* g_origCheckValidPtr = NULL;
    static unsigned char g_origCheckValidBytes[16];

    unsigned char patchRetNull[] = { 0x31, 0xC0, 0xC3 };

    PatchMethod("KUIConstructionCtrl_Blueprint", "CheckValidation", 3,
                &g_origCheckValidPtr, g_origCheckValidBytes,
                patchRetNull, sizeof(patchRetNull), g_superBuildEnabled);

    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ModLog("  [CRASH] SuperBuild Toggle exception: 0x%08X", GetExceptionCode());
    }
}


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

static void PatchMethod(const char* className, const char* methodName, int argCount, 
                       void** origPtr, unsigned char* origBytes, 
                       const unsigned char* patchBytes, int patchSize, BOOL enable) {
    if (!*origPtr) {
        Il2CppClass* klass = FIND_CLASS("Inventory", className);
        if (!klass) klass = FIND_CLASS("Construction", className);
        if (!klass) klass = FIND_CLASS("Construction.Extend.Impl", className);
        if (!klass) klass = FIND_CLASS("UI", className);
        if (!klass) klass = FIND_CLASS("", className);
        if (klass) {
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
                memcpy(*origPtr, origBytes, patchSize);
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
        unsigned char patchRet999[] = { 0xB8, 0x3F, 0x42, 0x0F, 0x00, 0xC3 };
        PatchMethod("KInventoryMethod", "GetTotalItemAmount", 2, 
                    &g_origGetTotalItemAmountPtr, g_origGetTotalItemAmountBytes, 
                    patchRet999, sizeof(patchRet999), g_resourceBypassEnabled);

        PatchMethod("KInventoryMethod", "GetItemAmount", 3, 
                    &g_origGetItemAmountPtr, g_origGetItemAmountBytes, 
                    patchRet999, sizeof(patchRet999), g_resourceBypassEnabled);
        
        unsigned char patchRetTrue[] = { 0xB0, 0x01, 0xC3 }; 
        PatchMethod("KInventoryUtil", "RemoveItem", 4, 
                    &g_origRemoveItemPtr, g_origRemoveItemBytes, 
                    patchRetTrue, sizeof(patchRetTrue), g_resourceBypassEnabled);

        PatchMethod("KInventoryUtil", "RemoveItems", 4, 
                    &g_origRemoveItemsPtr, g_origRemoveItemsBytes, 
                    patchRetTrue, sizeof(patchRetTrue), g_resourceBypassEnabled);

        PatchMethod("KGridItemSetUtil", "HasItem", 2, 
                    &g_origHasItemPtr, g_origHasItemBytes, 
                    patchRetTrue, sizeof(patchRetTrue), g_resourceBypassEnabled);

        static void* g_origGridGetAmountPtr = NULL;
        static unsigned char g_origGridGetAmountBytes[16];
        PatchMethod("KGridItemSetUtil", "GetAmount", 2, 
                    &g_origGridGetAmountPtr, g_origGridGetAmountBytes, 
                    patchRet999, sizeof(patchRet999), g_resourceBypassEnabled);


        static void* g_origBpDoCostPtr = NULL;
        static unsigned char g_origBpDoCostBytes[16];
        unsigned char patchRetVoid[] = { 0xC3 };

        PatchMethod("KConstructionBluePrint", "DoConstructCost", 1,
                    &g_origBpDoCostPtr, g_origBpDoCostBytes,
                    patchRetVoid, sizeof(patchRetVoid), g_resourceBypassEnabled);

        static void* g_origBpRemovePartPtr = NULL;
        static unsigned char g_origBpRemovePartBytes[16];

        PatchMethod("KConstructionBluePrint", "RemovePartIfMaterialNotEnough", 1,
                    &g_origBpRemovePartPtr, g_origBpRemovePartBytes,
                    patchRetVoid, sizeof(patchRetVoid), g_resourceBypassEnabled);


    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ModLog("  [CRASH] Resource Bypass exception: 0x%08X", GetExceptionCode());
    }
}

static BOOL g_flyModeEnabled = FALSE;

static void MainThread_ToggleFlyMode(void) {
    g_flyModeEnabled = !g_flyModeEnabled;
    ModLog("[F11] Fly Mode: %s (Use Arrow UP/DOWN to move)", g_flyModeEnabled ? "ON" : "OFF");
}

static void MainThread_DoFlyMove(int direction) {
    if (!g_apiResolved) return;
    
    __try {
        Il2CppClass* worldUtil = FIND_CLASS("World", "KWorldUtil");
        if (!worldUtil) worldUtil = FIND_CLASS("", "KWorldUtil");
        
        if (worldUtil) {
            IL2CPP.runtime_class_init(worldUtil);
            
            Il2CppObject* mainTransform = NULL;
            
            Il2CppMethodInfo* getTrans = IL2CPP.class_get_method_from_name(worldUtil, "get_MainEntityTransform", 0);
            if (getTrans) {
                Il2CppObject* exc = NULL;
                mainTransform = IL2CPP.runtime_invoke(getTrans, NULL, NULL, &exc);
            }
            
            if (!mainTransform) {
                 Il2CppFieldInfo* field = IL2CPP.class_get_field_from_name(worldUtil, "m_MainEntityTransform");
                 if (field) IL2CPP.field_static_get_value(field, &mainTransform);
            }

            if (mainTransform) {
                Il2CppClass* transClass = ((Il2CppObject*)mainTransform)->klass;
                
                Il2CppMethodInfo* getPos = IL2CPP.class_get_method_from_name(transClass, "get_Position", 0);
                Il2CppMethodInfo* teleport = IL2CPP.class_get_method_from_name(transClass, "Teleport", 1);
                
                if (getPos && teleport) {
                     Il2CppObject* exc = NULL;
                     Il2CppObject* posObj = IL2CPP.runtime_invoke(getPos, mainTransform, NULL, &exc);
                     if (posObj) {
                         Vector3 pos = *(Vector3*)IL2CPP.object_unbox(posObj);
                         
                         float moveAmt = 10.0f; 
                         if (direction == 2) moveAmt = -10.0f;
                         
                         pos.y += moveAmt;
                         
                         void* args[1] = { &pos };
                         IL2CPP.runtime_invoke(teleport, mainTransform, args, &exc);
                     }
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
    }
}

static LRESULT CALLBACK ModWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_MOD_TIMESCALE) {
        float scale;
        LONG bits = (LONG)lParam;
        memcpy(&scale, &bits, sizeof(float));
        MainThread_SetTimeScale(scale);
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
        MainThread_ToggleSuperBuild();
        MainThread_ToggleBuildLimits(g_superBuildEnabled);
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

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    DWORD wndPid = 0;
    GetWindowThreadProcessId(hwnd, &wndPid);
    if (wndPid == (DWORD)lParam && IsWindowVisible(hwnd)) {
        char cls[64] = {0};
        GetClassNameA(hwnd, cls, sizeof(cls));
        if (strcmp(cls, "UnityWndClass") == 0) {
            g_gameWnd = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

static void InstallMainThreadHook(void) {
    if (g_hookInstalled) return;

    g_gameWnd = FindWindowA("UnityWndClass", NULL);

    if (!g_gameWnd) {
        DWORD pid = GetCurrentProcessId();
        EnumWindows(EnumWindowsProc, (LPARAM)pid);
    }

    if (!g_gameWnd) {
        ModLog("  [ERROR] Cannot find Unity game window!");
        return;
    }

    g_origWndProc = (WNDPROC)SetWindowLongPtrA(g_gameWnd, GWLP_WNDPROC, (LONG_PTR)ModWndProc);
    if (g_origWndProc) {
        g_hookInstalled = TRUE;
        ModLog("  [OK] Window subclassed! hwnd=0x%p origProc=0x%p", g_gameWnd, g_origWndProc);
    } else {
        ModLog("  [ERROR] SetWindowLongPtr failed (err=%lu)", GetLastError());
    }
}

static void PostTimeScale(float scale) {
    if (!g_hookInstalled) InstallMainThreadHook();
    if (!g_hookInstalled || !g_gameWnd) {
        ModLog("  [ERROR] No main thread hook for time scale");
        return;
    }
    LONG bits;
    memcpy(&bits, &scale, sizeof(float));
    PostMessageA(g_gameWnd, WM_MOD_TIMESCALE, 0, (LPARAM)bits);
}

static void DoTimeControlToggle(void) {
    g_timeControlActive = !g_timeControlActive;
    if (g_timeControlActive) {
        ModLog("[F4] Time Control: ON (speed: %s)", g_timeSpeedNames[g_timeSpeedIndex]);
        ModLog("  PgUp/PgDn = speed, Left/Right = time of day (+/-1hr)");
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


static DWORD WINAPI IL2CppWorkerThread(LPVOID param) {
    while (1) {
        WaitForSingleObject(g_cmdEvent, INFINITE);
        int cmd = g_pendingCmd;
        __try {
            switch (cmd) {
            case CMD_DEVMODE:      DoDevMode(); break;
            case CMD_GMTOOL:       DoGMTool(); break;
            case CMD_TOGGLE_BUILD: DoToggleFreeBuild(); break;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            ModLog("[FATAL] Unhandled exception in command %d (code: 0x%08X)", cmd, GetExceptionCode());
        }
    }
    return 0;
}


static void TryResolveAPI(void) {
    if (g_apiResolved) return;
    if (IL2CPP_Init()) {
        g_apiResolved = TRUE;
        ModLog("[OK] GameAssembly.dll found at 0x%p", IL2CPP.hGameAssembly);
        ModLog("[OK] IL2CPP API resolved! (%d function pointers)",
            (int)(sizeof(IL2CPP) / sizeof(void*)) - 1);
    }
}


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


static DWORD WINAPI HotkeyThread(LPVOID param) {
    BOOL consoleShown = TRUE;

    while (1) {
        if (GetAsyncKeyState(VK_HOME) & 1) {
            HWND con = GetConsoleWindow();
            if (con) {
                consoleShown = !consoleShown;
                ShowWindow(con, consoleShown ? SW_SHOW : SW_HIDE);
            }
        }

        if (GetAsyncKeyState(VK_F1) & 1) {
            ModLog("F1 pressed - Enable Dev Mode");
            SendCommand(CMD_DEVMODE);
        }

        if (GetAsyncKeyState(VK_F2) & 1) {
            ModLog("F2 pressed - Give GM Tool");
            SendCommand(CMD_GMTOOL);
        }

        if (GetAsyncKeyState(VK_F4) & 1) {
            DoTimeControlToggle();
        }

        if (GetAsyncKeyState(VK_PRIOR) & 1) {
            DoTimeSpeedUp();
        }

        if (GetAsyncKeyState(VK_NEXT) & 1) {
            DoTimeSlowDown();
        }

        if (GetAsyncKeyState(VK_RIGHT) & 1) {
            if (g_timeControlActive) DoTimeForward(1);
        }

        if (GetAsyncKeyState(VK_LEFT) & 1) {
            if (g_timeControlActive) DoTimeBackward(1);
        }

        if (GetAsyncKeyState(VK_F6) & 1) {
            DoToggleBombBoost();
        }

        if (GetAsyncKeyState(VK_F7) & 1) {
            if (!g_hookInstalled) InstallMainThreadHook();
            if (g_hookInstalled && g_gameWnd) {
                PostMessageA(g_gameWnd, WM_MOD_SUPERBUILD, 0, 0);
            } else {
                ModLog("[F7] Error: No main thread hook active");
            }
        }

        if (GetAsyncKeyState(VK_F8) & 1) {
             if (!g_hookInstalled) InstallMainThreadHook();
            if (g_hookInstalled && g_gameWnd) {
                PostMessageA(g_gameWnd, WM_MOD_SUPERBUILD, 0, 0);
            } else {
                ModLog("[F8] Error: No main thread hook active");
            }
        }

        if (GetAsyncKeyState(VK_F10) & 1) {
            if (!g_hookInstalled) InstallMainThreadHook();
            if (g_hookInstalled && g_gameWnd) {
                PostMessageA(g_gameWnd, WM_MOD_RESOURCE_BYPASS, 0, 0);
            } else {
                ModLog("[F10] Error: No main thread hook active");
            }
        }

        if (GetAsyncKeyState(VK_F11) & 1) {
            if (!g_hookInstalled) InstallMainThreadHook();
            if (g_hookInstalled && g_gameWnd) {
                PostMessageA(g_gameWnd, WM_MOD_FLY_TOGGLE, 0, 0);
            }
        }

        if (GetAsyncKeyState(VK_F12) & 1) {
            if (!g_hookInstalled) InstallMainThreadHook();
            if (g_hookInstalled && g_gameWnd) {
                PostMessageA(g_gameWnd, WM_MOD_FORCE_BUILD, 0, 0);
            }
        }
        
        if (g_flyModeEnabled && g_hookInstalled && g_gameWnd) {
            if (GetAsyncKeyState(VK_UP) & 0x8000) {
                PostMessageA(g_gameWnd, WM_MOD_FLY_MOVE, 1, 0);
            }
            else if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
                PostMessageA(g_gameWnd, WM_MOD_FLY_MOVE, 2, 0);
            }
        }

        Sleep(50);
    }
    return 0;
}


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


static DWORD WINAPI ModThread(LPVOID param) {
    g_startTick = GetTickCount();
    InitializeCriticalSection(&g_logLock);

    char modDir[MAX_PATH];
    GetModuleFileNameA(NULL, modDir, MAX_PATH);
    char* lastSlash = strrchr(modDir, '\\');
    if (lastSlash) *(lastSlash + 1) = 0;
    snprintf(g_logPath, sizeof(g_logPath), "%sStarsandIslandMod\\mod_log.txt", modDir);
    char logDirPath[MAX_PATH];
    snprintf(logDirPath, sizeof(logDirPath), "%sStarsandIslandMod", modDir);
    CreateDirectoryA(logDirPath, NULL);
    g_logFile = fopen(g_logPath, "w");

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
    ModLog("  F2    = Give GM Tool");
    ModLog("  F4    = Toggle Time Control");
    ModLog("  F6    = Toggle Bomb Arrow Boost");
    ModLog("  PgUp  = Speed up time (Time Control on)");
    ModLog("  PgDn  = Slow down time (Time Control on)");
    ModLog("  Right = Advance +1 hour (Time Control on)");
    ModLog("  Left  = Rewind -1 hour (Time Control on)");
    ModLog("  F7/F8 = Toggle Super Free Build");
    ModLog("  F10   = Toggle Resource Bypass");
    ModLog("  F11   = Fly Mode (UP/DOWN to move)");
    ModLog("  F12   = Force Build Mode");
    ModLog("  HOME  = Hide/Show console");
    ModLog("");

    g_cmdEvent = CreateEventA(NULL, FALSE, FALSE, NULL);

    CreateThread(NULL, 0, HotkeyThread, NULL, 0, NULL);
    CreateThread(NULL, 0, HeartbeatThread, NULL, 0, NULL);

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
    ModLog(">>> Ready! Use hotkeys above. <<<");
    ModLog("");

    CreateThread(NULL, 0, IL2CppWorkerThread, NULL, 0, NULL);
    return 0;
}


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


static BOOL IsGameProcess(void) {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char* baseName = strrchr(exePath, '\\');
    if (baseName) baseName++; else baseName = exePath;
    return (_stricmp(baseName, "StarsandIsland.exe") == 0);
}


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

static BOOL g_configBackedUp = FALSE;
static float g_backupMaxDist, g_backupOverlapErr, g_backupFreeRay, g_backupFreeClamp, g_backupCamHeight;

static void MainThread_ForceBuildMode() {
    if (!g_apiResolved) return;
    ModLog("[FORCE] Attempting to force enter build mode via F12...");

    Il2CppClass* uiClass = FIND_CLASS("UI", "UIHudEntranceView");
    if (!uiClass) { ModLog("  [ERR] UIHudEntranceView class not found"); return; }
    
    Il2CppMethodInfo* getInst = IL2CPP.class_get_method_from_name(uiClass, "get_Instance", 0);
    if (!getInst) { ModLog("  [ERR] UIHudEntranceView.get_Instance not found"); return; }
    
    Il2CppObject* instance = IL2CPP.runtime_invoke(getInst, NULL, NULL, NULL);
    if (!instance) { ModLog("  [ERR] UIHudEntranceView.Instance is null (UI not initialized?)"); return; }

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
        PatchMethod("KTFMMatrixConstraint", "CheckConstraint", 2,
                    &g_origCheckConstraintPtr, g_origCheckConstraintBytes,
                    (unsigned char*)"\xB0\x01\xC3", 3, enable);

        PatchMethod("KConstructionUtil", "CheckRange", 4,
                    &g_origCheckRangePtr, g_origCheckRangeBytes,
                    (unsigned char*)"\x31\xC0\xC3", 3, enable);

        PatchMethod("KConstructionUtil", "IsReachingConstructionLimit", 2,
                    &g_origLimitCheckPtr, g_origLimitCheckBytes,
                    (unsigned char*)"\x31\xC0\xC3", 3, enable);

        PatchMethod("KConstructionUtil", "CheckChange", 2,
                    &g_origCheckChangePtr, g_origCheckChangeBytes,
                    (unsigned char*)"\xB0\x01\xC3", 3, enable);

        PatchMethod("KConstructionUtil", "IsConstructionUnlocked", 1,
                    &g_origIsUnlockedPtr, g_origIsUnlockedBytes,
                    (unsigned char*)"\xB0\x01\xC3", 3, enable);


        Il2CppClass* configClass = FIND_CLASS("Construction", "ConstructionConfig");
        if (configClass) {
            IL2CPP.runtime_class_init(configClass);
            Il2CppMethodInfo* getInst = IL2CPP.class_get_method_from_name(configClass, "get_Instance", 0);
            if (getInst) {
                Il2CppObject* inst = IL2CPP.runtime_invoke(getInst, NULL, NULL, NULL);
                if (inst) {
                    Il2CppFieldInfo* fMaxDist = IL2CPP.class_get_field_from_name(configClass, "MaxConstructionDistance");
                    Il2CppFieldInfo* fOverlap = IL2CPP.class_get_field_from_name(configClass, "OverlapError");
                    Il2CppFieldInfo* fFreeRay = IL2CPP.class_get_field_from_name(configClass, "FreeModeRayDistance");
                    Il2CppFieldInfo* fFreeClamp = IL2CPP.class_get_field_from_name(configClass, "FreeModeClampDistance");
                    Il2CppFieldInfo* fCamHeight = IL2CPP.class_get_field_from_name(configClass, "CameraAdditionalHeight");
                    
                    if (fMaxDist && fOverlap && fFreeRay && fFreeClamp && fCamHeight) {
                        if (enable) {
                            float currentMaxDist = 0;
                            IL2CPP.field_get_value(inst, fMaxDist, &currentMaxDist);
                            
                            if (currentMaxDist < 9000.0f) {
                                IL2CPP.field_get_value(inst, fMaxDist, &g_backupMaxDist);
                                IL2CPP.field_get_value(inst, fOverlap, &g_backupOverlapErr);
                                IL2CPP.field_get_value(inst, fFreeRay, &g_backupFreeRay);
                                IL2CPP.field_get_value(inst, fFreeClamp, &g_backupFreeClamp);
                                IL2CPP.field_get_value(inst, fCamHeight, &g_backupCamHeight);
                                g_configBackedUp = TRUE;
                            }
                            
                            float bigVal = 99999.0f;
                            float smallVal = 0.0f;
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
