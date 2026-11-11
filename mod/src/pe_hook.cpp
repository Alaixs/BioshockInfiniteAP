#include "pe_hook.h"

#include "ap_menu.h"
#include "areas.h"
#include "game_build.h"
#include "lockpick.h"
#include "log.h"
#include "overlay.h"
#include "shop.h"
#include "tears.h"
#include "trace.h"
#include "traps.h"

#include "MinHook.h"

#include <windows.h>

#include <cstdint>
#include <exception>

namespace bsap::pehook {

namespace {

using ProcessEvent_t = void(__fastcall*)(void* self, void* edx, void* func, void* parms, void* result);
ProcessEvent_t g_original = nullptr;
DWORD g_gameThread = 0;
bool g_inside = false;  // our own ProcessEvent calls are not filtered again
int g_errors = 0;

// An exception must never unwind into the game: it would close it. Logged, at most a few times.
void LogError(const char* what) {
    if (g_errors++ < 20) Log("ERROR in a ProcessEvent filter: %s", what);
}

// True if the game must not see the call. On an error, the call goes through unfiltered.
bool Blocked(void* self, void* func, void* parms) {
    try {
        trace::Observe(self, func);
        return shop::Intercept(self, func) || traps::Intercept(self, func) || apmenu::Intercept(self, func) ||
               tears::Intercept(self, func) || areas::Intercept(self, func) || lockpick::Intercept(self, func, parms);
    } catch (const std::exception& e) {
        LogError(e.what());
    } catch (...) {
        LogError("unknown exception");
    }
    return false;
}

void After(void* self, void* func, void* parms) {
    try {
        overlay::AfterProcessEvent(self, func, parms);
    } catch (const std::exception& e) {
        LogError(e.what());
    } catch (...) {
        LogError("unknown exception");
    }
}

void __fastcall Detour(void* self, void* edx, void* func, void* parms, void* result) {
    if (!g_inside && GetCurrentThreadId() == g_gameThread) {
        g_inside = true;
        const bool blocked = Blocked(self, func, parms);
        g_inside = false;
        if (blocked) {
            return;  // the game never sees this call
        }
    }
    g_original(self, edx, func, parms, result);
    if (!g_inside && GetCurrentThreadId() == g_gameThread) {
        g_inside = true;  // the overlay's drawing calls don't come back here
        After(self, func, parms);
        g_inside = false;
    }
}

}  // namespace

bool EnsureInstalled() {
    static bool installed = false;
    static bool failed = false;
    g_gameThread = GetCurrentThreadId();
    if (installed || failed) {
        return installed;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("BioShockInfinite.exe"));
    void* target = reinterpret_cast<void*>(base + build::kRvaUObjectProcessEvent);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&Detour), reinterpret_cast<void**>(&g_original)) != MH_OK ||
        MH_EnableHook(target) != MH_OK) {
        Log("ERROR: cannot hook UObject::ProcessEvent");
        failed = true;
        return false;
    }
    Log("Hooked UObject::ProcessEvent");
    installed = true;
    return true;
}

}  // namespace bsap::pehook
