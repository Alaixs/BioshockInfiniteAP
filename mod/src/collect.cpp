#include "collect.h"

#include "bridge.h"
#include "game_build.h"
#include "log.h"

#include "MinHook.h"
#include "ue3.h"
#include "world.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>

namespace bsap::collect {

namespace {

constexpr char kVoxophoneArchetype[] = "Pickup_AudioLog_";
constexpr char kVoxophoneLoot[] = "Loot_Consumable_AudioLog_";
constexpr char kInfusionArchetype[] = "Pickup_UnstableTonic_Upgrades";
constexpr char kGearArchetype[] = "Pickup_Gear_";

// Bottles and packages that are locations (lower case ids, from the client). They keep their loot
// list: the game hides an emptied bottle on the next load as already looted, which blocks scripts
// (the Blue Ribbon door waits for the Season Pass bottles).
std::set<std::string> g_apInfusions;
std::set<std::string> g_apGear;

void EnsureOpenSearchHook();

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool StartsWith(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

// Recording id: the first Loot_Consumable_AudioLog_<id> in LootDefinitionList, else the archetype
// name.
std::string RecordingOf(const void* actor, const std::string& archetype) {
    ue3::Property p;
    ue3::ArrayHeader list{};
    if (ue3::FindProperty(actor, "LootDefinitionList", &p) &&
        ue3::ReadBytes(static_cast<const std::uint8_t*>(actor) + p.offset, &list, sizeof(list))) {
        for (int i = 0; i < list.num && i < 8; ++i) {
            const void* loot = nullptr;
            ue3::ReadBytes(list.data + i * sizeof(void*), &loot, sizeof(loot));
            const std::string name = ue3::ObjectName(loot);
            if (StartsWith(name, kVoxophoneLoot)) return name.substr(sizeof(kVoxophoneLoot) - 1);
        }
    }
    return archetype.substr(sizeof(kVoxophoneArchetype) - 1);
}

int LootableState(const void* actor) {
    ue3::Property state;
    std::uint8_t value = 0;
    if (ue3::FindProperty(actor, "CurrentLootableState", &state)) {
        ue3::ReadBytes(static_cast<const std::uint8_t*>(actor) + state.offset, &value, sizeof(value));
    }
    return value;
}

std::string ArchetypeName(const void* obj) {
    const void* archetype = nullptr;
    ue3::ReadBytes(static_cast<const std::uint8_t*>(obj) + build::kObjectArchetype, &archetype, sizeof(archetype));
    return ue3::ObjectName(archetype);
}

// Containers placed in the loaded levels (not archetypes), with their archetype name.
template <typename Fn>
void ForEachPlacedContainer(Fn&& fn) {
    static const void* cls = nullptr;
    if (!ue3::IsObject(cls)) cls = ue3::FindClass("XLootContainerKActor");
    ue3::ForEachObjectOfClass(cls, [&](int, const void* actor) {
        if (ue3::ClassName(ue3::OuterOf(actor)) != "Level") return;
        fn(actor, ArchetypeName(actor));
    });
}

void List(const char* kind, const std::vector<Pickup>& all, const Reply& reply) {
    for (const Pickup& p : all) reply(std::string(kind) + " " + p.id + " " + std::to_string(p.state));
    reply(std::string("end ") + kind + "s " + std::to_string(all.size()));
}

}  // namespace

std::vector<Pickup> Voxophones() {
    std::vector<Pickup> out;
    ForEachPlacedContainer([&](const void* actor, const std::string& archetype) {
        if (StartsWith(archetype, kVoxophoneArchetype)) {
            out.push_back({actor, RecordingOf(actor, archetype), LootableState(actor)});
        }
    });
    return out;
}

std::string PlacedId(const void* actor) {
    const std::string path =
        ue3::PathName(actor);  // S_TWN_Float_B_Loot.TheWorld.PersistentLevel.XLootContainerKActor_43
    return path.substr(0, path.find('.')) + "." + ue3::ObjectName(actor);
}

std::vector<Pickup> Infusions() {
    std::vector<Pickup> out;
    ForEachPlacedContainer([&](const void* actor, const std::string& archetype) {
        if (archetype == kInfusionArchetype) out.push_back({actor, PlacedId(actor), LootableState(actor)});
    });
    return out;
}

std::vector<Pickup> Gear() {
    std::vector<Pickup> out;
    ForEachPlacedContainer([&](const void* actor, const std::string& archetype) {
        if (StartsWith(archetype, kGearArchetype)) out.push_back({actor, PlacedId(actor), LootableState(actor)});
    });
    return out;
}

namespace {

// Siphon goal: Songbird's destruction of the Siphon sends the "TowerDestroyed" remote event to
// S_CHU_LizTower_Gam, so its TriggerCount becomes 1. One "event goal siphon" per load of the level.
void WatchSiphon() {
    static DWORD next = 0;
    static bool sent = false;
    const DWORD now = GetTickCount();
    if (static_cast<int>(now - next) < 0) return;
    next = now + 1000;
    static const void* cls = nullptr;
    if (!ue3::IsObject(cls)) cls = ue3::FindClass("XSeqEvent_RemoteEvent");
    bool loaded = false, destroyed = false;
    ue3::ForEachObjectOfClass(cls, [&](int, const void* event) {
        if (destroyed || !StartsWith(ue3::PathName(event), "S_CHU_LizTower_Gam.")) return;
        ue3::Property name, count;
        std::int32_t fname = 0, triggers = 0;
        if (!ue3::FindProperty(event, "EventName", &name) || !ue3::FindProperty(event, "TriggerCount", &count)) return;
        ue3::ReadBytes(static_cast<const std::uint8_t*>(event) + name.offset, &fname, sizeof(fname));
        if (ue3::NameString(fname) != "TowerDestroyed") return;
        loaded = true;
        ue3::ReadBytes(static_cast<const std::uint8_t*>(event) + count.offset, &triggers, sizeof(triggers));
        destroyed = triggers > 0;
    });
    if (!loaded) sent = false;
    if (destroyed && !sent) {
        sent = true;
        Log("collect: Siphon destroyed (TowerDestroyed): goal");
        bridge::Send("event goal siphon");
    }
}

// A checkpoint (XSeqAct_Checkpoint) that ran has ActivateCount 1. Loading a save recreates the
// level without running it again.
void WatchCheckpoints() {
    static DWORD next = 0;
    static std::set<const void*> sent;
    const DWORD now = GetTickCount();
    if (static_cast<int>(now - next) < 0) return;
    next = now + 1000;
    static const void* cls = nullptr;
    if (!ue3::IsObject(cls)) cls = ue3::FindClass("XSeqAct_Checkpoint");
    std::set<const void*> alive;
    ue3::ForEachObjectOfClass(cls, [&](int, const void* action) {
        if (ue3::ObjectName(action).rfind("Default__", 0) == 0) return;
        alive.insert(action);
        ue3::Property count, name;
        std::int32_t activations = 0, fname = 0;
        if (sent.count(action) || !ue3::FindProperty(action, "ActivateCount", &count) ||
            !ue3::FindProperty(action, "CheckpointDisplayInfoStructLookupName", &name)) {
            return;
        }
        ue3::ReadBytes(static_cast<const std::uint8_t*>(action) + count.offset, &activations, sizeof(activations));
        if (activations <= 0) return;
        ue3::ReadBytes(static_cast<const std::uint8_t*>(action) + name.offset, &fname, sizeof(fname));
        sent.insert(action);
        const std::string step = ue3::NameString(fname);
        Log("collect: checkpoint reached: %s", step.c_str());
        bridge::Send("event checkpoint " + step);
    });
    for (auto it = sent.begin(); it != sent.end();) it = alive.count(*it) ? std::next(it) : sent.erase(it);
}

}  // namespace

void Tick(const void* controller) {
    EnsureOpenSearchHook();
    WatchSiphon();
    WatchCheckpoints();
    static DWORD next = 0;
    // State 4 also means hidden by the game (bStartHidden, Season Pass, loading a save...): only a
    // change to 4 with Booker next to the object is a pickup.
    static std::map<std::string, int> seen;
    constexpr double kPickupReach = 400.0;
    const DWORD now = GetTickCount();
    if ((g_apInfusions.empty() && g_apGear.empty()) || static_cast<int>(now - next) < 0) return;
    next = now + 250;
    world::Position player{};
    const bool hasPlayer = world::PlayerPosition(controller, &player);
    const auto watch = [&](const Pickup& pickup, const char* kind) {
        const auto it = seen.find(pickup.id);
        world::Position pos{};
        if (hasPlayer && it != seen.end() && it->second != kLootableStateLooted &&
            pickup.state == kLootableStateLooted && world::ActorPosition(pickup.actor, &pos)) {
            const double dx = pos.x - player.x, dy = pos.y - player.y, dz = pos.z - player.z;
            if (dx * dx + dy * dy + dz * dz <= kPickupReach * kPickupReach) {
                bridge::Send(std::string("event ") + kind + " " + pickup.id);
            }
        }
        seen[pickup.id] = pickup.state;
    };
    if (!g_apInfusions.empty()) {
        for (const Pickup& bottle : Infusions()) watch(bottle, "infusion");
    }
    if (!g_apGear.empty()) {
        for (const Pickup& package : Gear()) watch(package, "gear");
    }
}

namespace {

using OpenSearch_t = void(__fastcall*)(void* controller, void* edx, void* container);
OpenSearch_t g_originalOpenSearch[3] = {};

bool SkipSearch(const void* container) {
    if (g_apInfusions.empty() && g_apGear.empty()) return false;
    if (!ue3::IsObject(container) || ue3::ClassName(container) != "XLootContainerKActor") return false;
    const std::string archetype = ArchetypeName(container);
    const std::set<std::string>* ap = nullptr;
    const char* kind = nullptr;
    if (archetype == kInfusionArchetype) {
        ap = &g_apInfusions;
        kind = "infusion";
    } else if (StartsWith(archetype, kGearArchetype)) {
        ap = &g_apGear;
        kind = "gear";
    }
    if (!ap) return false;
    const std::string id = PlacedId(container);
    if (!ap->count(Lower(id))) return false;
    using SetLooted_t = void(__fastcall*)(const void* self, void* edx);
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
    reinterpret_cast<SetLooted_t>(base + build::kRvaReactiveActorSetLooted)(container, nullptr);
    Log("collect: AP %s looted without screen: %s", kind, id.c_str());
    bridge::Send(std::string("event ") + kind + " " + id);
    return true;
}

template <int N>
void __fastcall OpenSearchDetour(void* controller, void* edx, void* container) {
    if (!SkipSearch(container)) g_originalOpenSearch[N](controller, edx, container);
}

// From the game thread: hooking at process start freezes the game.
void EnsureOpenSearchHook() {
    static bool done = false;
    if (done) return;
    done = true;
    void* const detours[] = {reinterpret_cast<void*>(&OpenSearchDetour<0>),
                             reinterpret_cast<void*>(&OpenSearchDetour<1>),
                             reinterpret_cast<void*>(&OpenSearchDetour<2>)};
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
    for (int i = 0; i < 3; ++i) {
        void* target = reinterpret_cast<void*>(base + build::kRvaOpenContainerSearchUI[i]);
        if (MH_CreateHook(target, detours[i], reinterpret_cast<void**>(&g_originalOpenSearch[i])) != MH_OK ||
            MH_EnableHook(target) != MH_OK) {
            Log("ERROR: cannot hook container search screen %d", i);
        }
    }
    Log("Hooked the container search screens");
}

}  // namespace

bool Command(const std::string& cmd, const std::string& arg, const Reply& reply) {
    if (cmd == "voxophones") {
        List("voxophone", Voxophones(), reply);
    } else if (cmd == "infusions") {
        List("infusion", Infusions(), reply);
    } else if (cmd == "infusionap") {
        if (arg == "clear")
            g_apInfusions.clear();
        else if (!arg.empty())
            g_apInfusions.insert(Lower(arg));
        reply("ok infusionap " + std::to_string(g_apInfusions.size()));
    } else if (cmd == "gears") {
        List("gear", Gear(), reply);
    } else if (cmd == "gearap") {
        if (arg == "clear")
            g_apGear.clear();
        else if (!arg.empty())
            g_apGear.insert(Lower(arg));
        reply("ok gearap " + std::to_string(g_apGear.size()));
    } else {
        return false;
    }
    return true;
}

}  // namespace bsap::collect
