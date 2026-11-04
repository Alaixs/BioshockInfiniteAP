#include "tears.h"

#include "ap_notify.h"
#include "log.h"
#include "ue3.h"

#include <windows.h>

#include <unordered_map>

namespace bsap::tears {

namespace {

bool g_locked = false;

// The use key always asks for a Tear first, then for the other uses (door, pickup...). The
// message is only for a Tear: not when Booker aims at an ordinary usable object.
bool AimsAtOrdinaryObject(const void* controller) {
    ue3::Property p;
    if (!ue3::FindProperty(controller, "ButtonUseTarget", &p)) return false;
    const void* target = ue3::ObjectField(controller, p);  // interface: the object comes first
    if (!target) return false;
    ue3::Property state;
    double value = 0;
    const bool tear = ue3::FindProperty(target, "ReplicatedSubstantiationState", &state) &&
                      ue3::ReadNumber(target, state, &value) && value != 0;
    Log("tears: use target %s, substantiation state %d", ue3::ObjectName(target).c_str(), static_cast<int>(value));
    return !tear;
}

}  // namespace

bool Command(const std::string& cmd, const std::string& arg, const Reply& reply) {
    if (cmd != "tears") return false;
    if (arg == "lock" || arg == "unlock") {
        if (g_locked != (arg == "lock")) Log("tears: %s", arg == "lock" ? "locked" : "unlocked");
        g_locked = arg == "lock";
    } else if (!arg.empty()) {
        reply("err usage: tears [lock|unlock]");
        return true;
    }
    reply(std::string("ok tears ") + (g_locked ? "locked" : "unlocked"));
    return true;
}

bool Intercept(const void* self, const void* func) {
    if (!g_locked) return false;
    static std::unordered_map<const void*, bool> isOpen;  // per UFunction
    auto it = isOpen.find(func);
    if (it == isOpen.end()) it = isOpen.emplace(func, ue3::ObjectName(func) == "XPerformSubstantiateObject").first;
    if (!it->second) return false;
    if (AimsAtOrdinaryObject(self)) return true;  // blocked, without the message
    static DWORD lastMessage = 0;
    const DWORD now = GetTickCount();
    if (!lastMessage || now - lastMessage >= 5000) {  // one message at a time while the key is held
        lastMessage = now;
        apnotify::ShowTearsLocked();
    }
    return true;
}

}  // namespace bsap::tears
