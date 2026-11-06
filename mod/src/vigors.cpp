#include "vigors.h"

#include "bridge.h"
#include "engine_call.h"
#include "log.h"
#include "ue3.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <vector>

namespace bsap::vigors {

namespace {

// Key -> start of the archetype name of the Vigor's XWeapon.
struct VigorKind {
    const char* key;
    const char* prefixes[2];
};
const VigorKind kVigors[] = {
    {"devilskiss", {"plasmid_devilskiss", "plasmid_incinerate"}},
    {"murderofcrows", {"plasmid_murderofcrows", nullptr}},
    {"buckingbronco", {"plasmid_buckingbronco", "plasmid_rollingthunder"}},
    {"charge", {"plasmid_charge", nullptr}},
    {"undertow", {"plasmid_undertow", nullptr}},
    {"returntosender", {"plasmid_returntosender", nullptr}},
};
constexpr char kLoanedSuffix[] = "_twn_fair";  // Bucking Bronco lent by the Fair booth

// XInventoryManager.InventoryItems: fixed array, up to PreviousWeaponState.
constexpr std::size_t kMaxInventorySlots = 36;

std::set<std::string> g_locked;
// Received Vigors (key -> item of the bottle), and since when each has been missing.
std::map<std::string, int> g_owned;
std::map<std::string, DWORD> g_missingSince;

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

const char* KeyOf(const std::string& archetype) {
    const std::string name = Lower(archetype);
    if (name.size() >= sizeof(kLoanedSuffix) - 1 &&
        name.compare(name.size() - (sizeof(kLoanedSuffix) - 1), std::string::npos, kLoanedSuffix) == 0) {
        return nullptr;
    }
    for (const VigorKind& v : kVigors) {
        for (const char* prefix : v.prefixes) {
            if (prefix && name.rfind(prefix, 0) == 0) return v.key;
        }
    }
    return nullptr;
}

const void* ObjectAt(const void* obj, const char* prop) {
    ue3::Property p;
    return ue3::FindProperty(obj, prop, &p) ? ue3::ObjectField(obj, p) : nullptr;
}

std::string ArchetypeName(const void* obj) {
    const void* archetype = nullptr;
    ue3::ReadBytes(static_cast<const std::uint8_t*>(obj) + 0x24, &archetype, sizeof(archetype));  // ObjectArchetype
    return ue3::ObjectName(archetype);
}

bool ReadSlots(const void* manager, const void* (&slots)[kMaxInventorySlots]) {
    ue3::Property p;
    if (!manager || !ue3::FindProperty(manager, "InventoryItems", &p)) return false;
    ue3::ReadBytes(static_cast<const std::uint8_t*>(manager) + p.offset, slots, sizeof(slots));
    for (const void*& item : slots) {
        if (item && !ue3::IsObject(item)) item = nullptr;
    }
    return true;
}

std::vector<const void*> Inventory(const void* pawn) {
    std::vector<const void*> items;
    const void* slots[kMaxInventorySlots] = {};
    if (!ReadSlots(ObjectAt(pawn, "InvManager"), slots)) return items;
    for (const void* item : slots) {
        if (item) items.push_back(item);
    }
    return items;
}

void CallNoArgs(const void* obj, const char* func) {
    const void* f = ue3::FindFunction(obj, func);
    alignas(16) std::uint8_t parms[64] = {};
    if (f) engine::CallRaw(obj, f, parms);
}

bool IsVigor(const void* item) {
    return item && Lower(ArchetypeName(item)).rfind("plasmid_", 0) == 0;
}

// Left hand Vigor. CurrentLHWeapon stays None, this is what counts.
const void* LeftHandVigor(const void* pawn) {
    const void* f = ue3::FindFunction(pawn, "GetLHEquippedXWeapon");
    alignas(16) std::uint8_t parms[64] = {};
    if (!f || !engine::CallRaw(pawn, f, parms)) return nullptr;
    const void* weapon = nullptr;
    std::memcpy(&weapon, parms, sizeof(weapon));
    return ue3::IsObject(weapon) ? weapon : nullptr;
}

void SetEquippedPlasmidIndex(const void* manager, std::int32_t index) {
    const void* f = ue3::FindFunction(manager, "ClientSetEquippedPlasmidIndex");
    alignas(16) std::uint8_t parms[64] = {};
    std::memcpy(parms, &index, sizeof(index));
    if (f) engine::CallRaw(manager, f, parms);
}

// The Vigor key fires inventory slot EquippedPlasmidIndex. If that slot holds something else (a
// picked up weapon, an old save), it is reset to "no Vigor". Setting it to a Vigor doesn't equip
// it, so never do that.
void RepairEquippedPlasmid(const void* pawn) {
    const void* manager = ObjectAt(pawn, "InvManager");
    const void* slots[kMaxInventorySlots] = {};
    ue3::Property p;
    if (!ReadSlots(manager, slots) || !ue3::FindProperty(manager, "EquippedPlasmidIndex", &p)) return;
    std::int32_t index = -1;
    ue3::ReadBytes(static_cast<const std::uint8_t*>(manager) + p.offset, &index, sizeof(index));
    if (index < 0 || index >= static_cast<int>(kMaxInventorySlots) || !slots[index] || IsVigor(slots[index])) return;
    SetEquippedPlasmidIndex(manager, -1);
    Log("vigors: the Vigor key pointed to %s (slot %d): reset to no Vigor", ArchetypeName(slots[index]).c_str(), index);
}

void Destroy(const void* pawn, const void* weapon, const char* key) {
    const void* destroy = ue3::FindFunction(pawn, "DestroyInventoryItem");
    if (!destroy) return;
    alignas(16) std::uint8_t parms[64] = {};
    std::memcpy(parms, &weapon, sizeof(weapon));
    Log("vigors: %s locked, removed (%s)", key, ArchetypeName(weapon).c_str());
    engine::CallRaw(pawn, destroy, parms);
    bridge::Send(std::string("event vigor ") + key);
}

// A locked Vigor in hand (the game re-equips the last one on load) can't just be destroyed: the
// left hand would break. NextPlasmid switches first, and only works once the Vigor is really
// equipped (bEquippedAndReadyToBeUsed), which takes a moment after a load or a bottle. With no
// other Vigor, it is destroyed and the Vigor key reset.
DWORD g_inHandSince = 0;
DWORD g_swapAt = 0;
int g_swapTries = 0;
constexpr DWORD kEquipWaitMs = 8000;
constexpr DWORD kSwapWaitMs = 1000;
constexpr int kMaxSwapTries = 5;

bool ReadyToUse(const void* weapon) {
    ue3::Property p;
    std::uint32_t bits = 0;
    return ue3::FindProperty(weapon, "bEquippedAndReadyToBeUsed", &p) && p.boolMask &&
           ue3::ReadBytes(static_cast<const std::uint8_t*>(weapon) + p.offset, &bits, sizeof(bits)) &&
           (bits & p.boolMask);
}

void RemoveInHand(const void* pawn, const void* weapon, const char* key, bool otherVigor) {
    const void* manager = ObjectAt(pawn, "InvManager");
    const DWORD now = GetTickCount();
    if (!g_inHandSince) g_inHandSince = now;
    if (otherVigor && manager && g_swapTries < kMaxSwapTries) {
        if (!ReadyToUse(weapon) && now - g_inHandSince < kEquipWaitMs) return;  // still equipping
        if (now - g_swapAt < kSwapWaitMs) return;                               // still switching
        CallNoArgs(manager, "NextPlasmid");
        Log("vigors: %s in hand, switching to the next Vigor before removing it", key);
        g_swapAt = now;
        ++g_swapTries;
        return;
    }
    // No other Vigor, or the game didn't switch: remove it anyway, and free the Vigor key's slot so
    // a picked up weapon can't take it.
    g_inHandSince = 0;
    g_swapTries = 0;
    Destroy(pawn, weapon, key);
    if (manager) SetEquippedPlasmidIndex(manager, -1);
}

// Each chapter's loadout script restores the story's Vigors, so one received early (Charge before
// Finkton) vanishes on a level change. Give it again once it has been missing a while, unless
// Booker has no Vigor at all (a story scene took them).
constexpr DWORD kMissingMs = 10000;

void RegiveOwned(const void* (&slots)[kMaxInventorySlots]) {
    if (g_owned.empty() || !std::any_of(std::begin(slots), std::end(slots), IsVigor)) {
        g_missingSince.clear();
        return;
    }
    const DWORD now = GetTickCount();
    for (const auto& [key, lookup] : g_owned) {
        const bool present = std::any_of(std::begin(slots), std::end(slots), [&](const void* item) {
            const char* k = item ? KeyOf(ArchetypeName(item)) : nullptr;
            return k && key == k;
        });
        if (present) {
            g_missingSince.erase(key);
            continue;
        }
        const auto [it, added] = g_missingSince.try_emplace(key, now);
        if (added || now - it->second < kMissingMs) continue;
        it->second = now;  // next try in kMissingMs (item not in memory yet, screen open...)
        Log("vigors: %s received but missing from the inventory, giving it again (item %d)", key.c_str(), lookup);
        bridge::Inject("giveitem " + std::to_string(lookup));
        return;  // one at a time
    }
}

}  // namespace

void Tick(const void* controller) {
    static DWORD next = 0;
    const DWORD now = GetTickCount();
    if (static_cast<int>(now - next) < 0) return;
    next = now + 250;
    const void* pawn = ObjectAt(controller, "Pawn");
    if (!pawn) return;
    RepairEquippedPlasmid(pawn);
    const void* manager = ObjectAt(pawn, "InvManager");
    const void* slots[kMaxInventorySlots] = {};
    ue3::Property p;
    if (!ReadSlots(manager, slots) || !ue3::FindProperty(manager, "EquippedPlasmidIndex", &p)) return;
    std::int32_t equipped = -1;
    ue3::ReadBytes(static_cast<const std::uint8_t*>(manager) + p.offset, &equipped, sizeof(equipped));
    const void* inHand = LeftHandVigor(pawn);
    const void* lockedInHand = nullptr;
    const char* lockedInHandKey = nullptr;
    for (int k = 0; k < static_cast<int>(kMaxInventorySlots); ++k) {
        const char* key = slots[k] ? KeyOf(ArchetypeName(slots[k])) : nullptr;
        if (!key || !g_locked.count(key)) continue;
        if (slots[k] == inHand || k == equipped) {  // in hand, or about to be
            lockedInHand = slots[k];
            lockedInHandKey = key;
            continue;
        }
        Destroy(pawn, slots[k], key);  // stowed: right away
        return;                        // the inventory changed: the rest on the next pass
    }
    if (lockedInHand) {
        const bool otherVigor = std::any_of(std::begin(slots), std::end(slots),
                                            [&](const void* item) { return item != lockedInHand && IsVigor(item); });
        RemoveInHand(pawn, lockedInHand, lockedInHandKey, otherVigor);
        return;
    }
    g_inHandSince = 0;
    g_swapTries = 0;
    RegiveOwned(slots);
}

bool Command(const std::string& cmd, const std::string& arg, const void* controller, const Reply& reply) {
    if (cmd == "vigorlock") {
        if (arg == "clear")
            g_locked.clear();
        else if (!arg.empty())
            g_locked.insert(Lower(arg));
        reply("ok vigorlock " + std::to_string(g_locked.size()));
    } else if (cmd == "vigorown") {
        // vigorown <key>:<item> | clear
        const std::size_t colon = arg.find(':');
        if (arg == "clear")
            g_owned.clear();
        else if (colon != std::string::npos)
            g_owned[Lower(arg.substr(0, colon))] = std::atoi(arg.c_str() + colon + 1);
        reply("ok vigorown " + std::to_string(g_owned.size()));
    } else if (cmd == "vigors") {
        const void* pawn = ObjectAt(controller, "Pawn");
        int count = 0;
        if (pawn) {
            for (const void* item : Inventory(pawn)) {
                const std::string archetype = ArchetypeName(item);
                if (Lower(archetype).rfind("plasmid_", 0) != 0) continue;
                const char* key = KeyOf(archetype);
                reply("vigor " + archetype + (key ? std::string(" ") + key : std::string()));
                ++count;
            }
        }
        reply("end vigors " + std::to_string(count));
    } else {
        return false;
    }
    return true;
}

}  // namespace bsap::vigors
