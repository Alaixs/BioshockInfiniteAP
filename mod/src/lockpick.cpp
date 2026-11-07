#include "lockpick.h"

#include "ap_notify.h"
#include "log.h"
#include "ue3.h"

#include <windows.h>

#include <unordered_map>

namespace bsap::lockpick {

namespace {

bool g_locked = false;

std::string Field(const void* obj, const char* name) {
    ue3::Property p;
    return ue3::FindProperty(obj, name, &p) ? ue3::ValueToString(obj, p) : std::string();
}

// A lock that costs lockpicks and is not picked yet.
bool IsPaidLock(const void* obj) {
    if (!obj || Field(obj, "IsLockpickingActor") != "1" || Field(obj, "HasBeenLockpicked") != "0") return false;
    ue3::Property p;
    double picks = 0;
    return ue3::FindProperty(obj, "NumLockpicksRequired", &p) && ue3::ReadNumber(obj, p, &picks) && picks > 0;
}

}  // namespace

bool Command(const std::string& cmd, const std::string& arg, const Reply& reply) {
    if (cmd != "lockpick") return false;
    if (arg == "lock" || arg == "unlock") {
        if (g_locked != (arg == "lock")) Log("lockpick: %s", arg == "lock" ? "locked" : "unlocked");
        g_locked = arg == "lock";
    } else if (!arg.empty()) {
        reply("err usage: lockpick [lock|unlock]");
        return true;
    }
    reply(std::string("ok lockpick ") + (g_locked ? "locked" : "unlocked"));
    return true;
}

bool Intercept(const void* self, const void* func, const void* parms) {
    (void)self;
    if (!g_locked || !parms) return false;
    static std::unordered_map<const void*, bool> isUse;  // per UFunction
    auto it = isUse.find(func);
    if (it == isUse.end()) it = isUse.emplace(func, ue3::ObjectName(func) == "ServerStartUsingButtonUseTarget").first;
    if (!it->second) return false;
    const void* target = *static_cast<const void* const*>(parms);  // UsableObject, first parameter
    if (!ue3::IsObject(target) || !IsPaidLock(target)) return false;
    static DWORD lastMessage = 0;
    const DWORD now = GetTickCount();
    if (!lastMessage || now - lastMessage >= 5000) {
        lastMessage = now;
        Log("lockpick: blocked %s", ue3::ObjectName(target).c_str());
        apnotify::ShowLockpickLocked();
    }
    return true;
}

}  // namespace bsap::lockpick
