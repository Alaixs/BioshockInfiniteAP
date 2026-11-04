#include "inventory.h"

#include "ap_notify.h"
#include "engine_call.h"
#include "game_build.h"
#include "log.h"
#include "savedata.h"
#include "shop.h"
#include "world.h"

#include "MinHook.h"
#include "ue3.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace bsap::inventory {

namespace {

// XItemCompositeData names its game object; loaded on demand.
constexpr std::size_t kCompositeObjectOuter = 0x3Cu;  // FName: PreCoalescedItemAssets
constexpr std::size_t kCompositeObjectName = 0x4Cu;   // FName: Tonic_HP_Bonus (number 2 -> _1)

const std::uint8_t* DbEntry(int index) {
    static const void* db = nullptr;
    if (!ue3::IsObject(db) || ue3::ClassName(db) != "XItemDatabase") {
        db = nullptr;
        if (const void* cls = ue3::FindClass("XItemDatabase")) {
            ue3::ForEachObjectOfClass(cls, [&](int, const void* obj) {
                if (!db && ue3::ObjectName(obj).rfind("Default__", 0) != 0) db = obj;
            });
        }
    }
    ue3::Property p;
    ue3::ArrayHeader table{};
    if (!db || !ue3::FindProperty(db, "ItemLookupTable", &p) ||
        !ue3::ReadBytes(static_cast<const std::uint8_t*>(db) + p.offset, &table, sizeof(table)) || index < 0 ||
        index >= table.num) {
        return nullptr;
    }
    return table.data + static_cast<std::size_t>(index) * build::kSizeofItemCompositeData;
}

std::string NameAt(const std::uint8_t* at) {
    std::int32_t name[2] = {};
    if (!ue3::ReadBytes(at, name, sizeof(name))) return {};
    std::string text = ue3::NameString(name[0]);
    if (name[1] > 0) text += "_" + std::to_string(name[1] - 1);
    return text;
}

const void* FindLoaded(const void* cls, const std::string& outer, const std::string& name) {
    const void* found = nullptr;
    ue3::ForEachObjectOfClass(cls, [&](int, const void* obj) {
        if (!found && ue3::ObjectName(obj) == name && ue3::ObjectName(ue3::OuterOf(obj)) == outer) found = obj;
    });
    return found;
}

// The client's token and the player's choice (-1 until chosen).
std::string g_pickToken;
int g_pickChoice = -1;
constexpr int kInfusionChoices[] = {276, 329, 235};  // health, shield, salts (XUnstableSelectionScreen)
constexpr int kShieldChoice = 1;

// The Luteces give the shield with the story's first Infusion. Before that, giving a Shield
// Infusion crashes the game, so AP Infusions wait.
bool HasShield(const void* controller) {
    ue3::Property p;
    std::string err;
    const void* owner = ue3::ResolvePath(controller, "Pawn", &p, &err);
    const void* pawn = owner ? ue3::ObjectField(owner, p) : nullptr;
    if (!ue3::IsObject(pawn) || !ue3::FindProperty(pawn, "XShieldMax", &p)) return false;
    float max = 0;
    ue3::ReadBytes(static_cast<const std::uint8_t*>(pawn) + p.offset, &max, sizeof(max));
    return max > 0;
}

// The shield is refused until Booker has it (the choice is asked again).
bool AcceptChoice(const void* controller, int index) {
    if (index == kShieldChoice && !HasShield(controller)) {
        Log("inventory: Shield Infusion refused (no shield yet)");
        apnotify::ShowNoShield();
        return false;
    }
    g_pickChoice = index;
    apnotify::SetInfusionPrompt(false);
    return true;
}

const void* g_choiceContainer = nullptr;
DWORD g_nextScreenAt = 0;

std::uintptr_t Base() {
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
}

// Loot definitions of the three choices, in order: health, shield, salts.
bool ChoiceDefinitions(const void* out[3]) {
    static const void* cls = nullptr;
    if (!ue3::IsObject(cls)) cls = ue3::FindClass("XLootDropItemDefinition");
    for (int i = 0; i < 3; ++i) out[i] = nullptr;
    ue3::ForEachObjectOfClass(cls, [&](int, const void* def) {
        std::int64_t id = 0;
        ue3::ReadBytes(static_cast<const std::uint8_t*>(def) + 0x80, &id, sizeof(id));  // ItemLookupID
        for (int i = 0; i < 3; ++i) {
            if (!out[i] && id == kInfusionChoices[i] && ue3::ObjectName(def).rfind("Loot_StableTonic_", 0) == 0)
                out[i] = def;
        }
    });
    return out[0] && out[1] && out[2];
}

const void* FindArchetype(const char* name) {
    static const void* cls = nullptr;
    if (!ue3::IsObject(cls)) cls = ue3::FindClass("XLootContainerKActor");
    const void* found = nullptr;
    ue3::ForEachObjectOfClass(cls, [&](int, const void* obj) {
        if (!found && ue3::ObjectName(obj) == name && ue3::ClassName(ue3::OuterOf(obj)) != "Level") found = obj;
    });
    return found;
}

struct Parms {
    explicit Parms(const void* f) : func(f) {
        if (func) ue3::ForEachOwnProperty(func, [&](const ue3::Property& p) { props.push_back(p); });
    }
    template <typename T>
    void Set(const char* name, const T& value) {
        for (const ue3::Property& p : props) {
            if (p.name == name && p.offset + sizeof(T) <= sizeof(bytes))
                std::memcpy(bytes + p.offset, &value, sizeof(T));
        }
    }
    void SetBool(const char* name, bool value) {
        for (const ue3::Property& p : props) {
            if (p.name == name) {
                const std::uint32_t bits = value ? (p.boolMask ? p.boolMask : 1u) : 0u;
                std::memcpy(bytes + p.offset, &bits, sizeof(bits));
            }
        }
    }
    template <typename T>
    T Get(const char* name) const {
        T value{};
        for (const ue3::Property& p : props) {
            if (p.name == name) std::memcpy(&value, bytes + p.offset, sizeof(T));
        }
        return value;
    }
    const void* func;
    std::vector<ue3::Property> props;
    alignas(16) std::uint8_t bytes[512] = {};
};

bool CallBool(const void* obj, const char* funcName) {
    Parms p(ue3::FindFunction(obj, funcName));
    if (!p.func || !engine::CallRaw(obj, p.func, p.bytes)) return false;
    return p.Get<std::uint32_t>("ReturnValue") != 0;
}

// The game refuses Destroy on this container, so it is shut down like an emptied one (hidden, no
// collision or tick) and goes with the level.
void DestroyChoiceContainer() {
    if (ue3::IsObject(g_choiceContainer)) {
        Parms p(ue3::FindFunction(g_choiceContainer, "DestroyOrShutdown"));
        if (p.func) engine::CallRaw(g_choiceContainer, p.func, p.bytes);
    }
    g_choiceContainer = nullptr;
}

const void* SearchedContainer(const void* controller) {
    const void* searched = nullptr;
    ue3::ReadBytes(static_cast<const std::uint8_t*>(controller) + build::kControllerSearchedContainer, &searched,
                   sizeof(searched));
    return searched;
}

bool OpenChoiceScreen(const void* controller) {
    world::Position pos{};
    const void* defs[3];
    const void* archetype = FindArchetype("Pickup_Base_StableTonic");
    if (!world::PlayerPosition(controller, &pos) || !archetype || !ChoiceDefinitions(defs) || shop::AnyScreenOpen() ||
        SearchedContainer(controller) || CallBool(controller, "IsInPauseScreenFlow")) {
        return false;
    }
    Parms spawn(ue3::FindFunction(controller, "Spawn"));
    if (!spawn.func) return false;
    struct Vec3f {
        float x, y, z;
    };
    struct Rot {
        std::int32_t pitch, yaw, roll;
    };
    spawn.Set("SpawnClass", ue3::ClassOf(archetype));
    spawn.Set("SpawnLocation",
              Vec3f{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z) - 60.0f});
    spawn.Set("SpawnRotation", Rot{0, 0, 0});
    spawn.Set("ActorTemplate", archetype);
    spawn.SetBool("bNoCollisionFail", true);
    if (!engine::CallRaw(controller, spawn.func, spawn.bytes)) return false;
    const void* container = spawn.Get<const void*>("ReturnValue");
    if (!ue3::IsObject(container)) {
        Log("inventory: cannot create the choice screen's container");
        return false;
    }
    Parms hide(ue3::FindFunction(container, "SetHidden"));
    hide.SetBool("bNewHidden", true);
    if (hide.func) engine::CallRaw(container, hide.func, hide.bytes);
    ue3::WriteBytes(const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(container)) + build::kContainerChoices,
                    defs, sizeof(defs));
    g_choiceContainer = container;
    using Open_t = void(__fastcall*)(const void* pc, void* edx, const void* container);
    reinterpret_cast<Open_t>(Base() + build::kRvaOpenContainerSearchUI[1])(controller, nullptr, container);
    Log("inventory: Infusion choice screen open (container %s)", ue3::ObjectName(container).c_str());
    return true;
}

using Accept_t = int(__fastcall*)(void* screen, void* edx);
Accept_t g_originalAccept = nullptr;

// For our container, the choice is recorded and the screen closes as after a real choice;
// infusionpick gives the item.
int __fastcall AcceptDetour(void* screen, void* edx) {
    using Controller_t = const void*(__fastcall*)(void* self, void* edx);
    const void* pc = reinterpret_cast<Controller_t>(Base() + build::kRvaScreenOwnerController)(screen, nullptr);
    if (!g_choiceContainer || !pc || SearchedContainer(pc) != g_choiceContainer) return g_originalAccept(screen, edx);
    using FindWidget_t = void*(__fastcall*)(void* self, void* edx, const void* name);
    using WidgetInt_t = int(__fastcall*)(void* self, void* edx);
    void* widget = reinterpret_cast<FindWidget_t>(Base() + build::kRvaScreenFindWidget)(
        screen, nullptr, reinterpret_cast<const void*>(Base() + build::kRvaSelectionWidgetName));
    if (!widget || !reinterpret_cast<WidgetInt_t>(Base() + build::kRvaWidgetIsValid)(widget, nullptr)) return 0;
    const int index = reinterpret_cast<WidgetInt_t>(Base() + build::kRvaWidgetSelectedIndex)(widget, nullptr);
    if (index < 0 || index > 2) return 0;
    if (!AcceptChoice(pc, index)) return 1;  // the screen closes, the choice will be asked again
    Log("inventory: Infusion chosen on screen: %d (item %d)", index, kInfusionChoices[index]);
    return 1;
}

void EnsureAcceptHook() {
    static bool done = false;
    if (done) return;
    done = true;
    void* target = reinterpret_cast<void*>(Base() + build::kRvaUnstableAccept);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&AcceptDetour), reinterpret_cast<void**>(&g_originalAccept)) !=
            MH_OK ||
        MH_EnableHook(target) != MH_OK) {
        Log("ERROR: cannot hook the Infusion choice validation");
    }
}

bool GameHasFocus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// What the inventory code will find for this item (nullptr: it would crash), using the game's own
// lookup.
const void* ResolveForInventory(int id) {
    using Resolve_t = const void*(__fastcall*)(void* manager, void* edx, std::uint32_t low, std::uint32_t high);
    void* manager = nullptr;
    if (!ue3::ReadBytes(reinterpret_cast<const void*>(Base() + build::kRvaItemManager), &manager, sizeof(manager)) ||
        !manager) {
        return nullptr;
    }
    __try {
        return reinterpret_cast<Resolve_t>(Base() + build::kRvaItemResolve)(manager, nullptr,
                                                                            static_cast<std::uint32_t>(id), 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void GiveItem(const void* controller, int id, const Reply& reply) {
    const std::uint8_t* entry = DbEntry(id);
    if (!entry) {
        reply("err item " + std::to_string(id) + " not in the database");
        return;
    }
    const void* cls = nullptr;
    ue3::ReadBytes(entry + build::kCompositeItemClass, &cls, sizeof(cls));
    const std::string outer = NameAt(entry + kCompositeObjectOuter);
    const std::string name = NameAt(entry + kCompositeObjectName);
    if (!ue3::IsObject(cls) || name.empty()) {
        reply("err item " + std::to_string(id) + " without class or name");
        return;
    }
    if (!FindLoaded(cls, outer, name)) {
        reply("err item " + std::to_string(id) + " (" + outer + "." + name + ") not in memory");
        return;
    }
    // An object can be in memory before the game can find it (a later level's Vigor, the shield
    // before the Luteces): wait, or the game crashes.
    if (!ResolveForInventory(id)) {
        reply("err item " + std::to_string(id) + " (" + outer + "." + name + ") not usable yet");
        return;
    }
    engine::Call(controller, "ServerTransactLootItem", {std::to_string(id), "1", "1", "0"}, reply);
}

}  // namespace

void Tick(const void* controller) {
    EnsureAcceptHook();
    if (g_choiceContainer && SearchedContainer(controller) != g_choiceContainer) {
        DestroyChoiceContainer();
        g_nextScreenAt = GetTickCount() + 20000;  // cancelled: ask again later
    }
    if (g_pickToken.empty() || g_pickChoice >= 0) return;
    // Not during a load: under the loading screen the choice blocks it until a key is pressed.
    // Booker must have been in game a few seconds.
    static DWORD playingSince = 0;
    world::Position pos{};
    if (!world::PlayerPosition(controller, &pos) || CallBool(controller, "IsLoadingMovieShowing")) {
        playingSince = 0;
        return;
    }
    if (!playingSince) playingSince = GetTickCount() | 1;
    if (GetTickCount() - playingSince < 5000) return;
    if (!g_choiceContainer && static_cast<int>(GetTickCount() - g_nextScreenAt) >= 0) {
        g_nextScreenAt = GetTickCount() + 3000;
        apnotify::SetInfusionPrompt(!OpenChoiceScreen(controller));
    }
    static bool down[3] = {};
    const int keys[3] = {VK_F9, VK_F10, VK_F11};
    const bool focus = GameHasFocus();
    for (int i = 0; i < 3; ++i) {
        const bool now = focus && (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
        if (now && !down[i] && g_pickChoice < 0 && AcceptChoice(controller, i)) {
            Log("inventory: Infusion chosen: %d (item %d)", i, kInfusionChoices[i]);
        }
        down[i] = now;
    }
}

bool Command(const std::string& cmd, const std::string& arg, const void* controller, const Reply& reply) {
    if ((cmd == "infusionpick" || cmd == "giveonce") && shop::GrantsBlocked()) {
        reply(cmd + " wait " + arg);
        return true;
    }
    if (cmd == "giveitem" && shop::GrantsBlocked()) {
        reply("err grant deferred: vending screen open");
        return true;
    }
    if (cmd == "infusionpick") {
        const std::wstring variable(arg.begin(), arg.end());
        int done = 0;
        if (arg.empty()) {
            reply("err usage: infusionpick <token>");
        } else if (savedata::Get(controller, variable, &done) && done > 0) {
            if (arg == g_pickToken) {
                g_pickToken.clear();
                g_pickChoice = -1;
            }
            reply("infusionpick done " + arg);
        } else if (!HasShield(controller)) {
            // Before the Luteces' Infusion the game has no usable choice screen: received Infusions
            // wait.
            if (arg == g_pickToken) {
                g_pickToken.clear();
                g_pickChoice = -1;
            }
            apnotify::SetInfusionPrompt(false);
            reply("infusionpick wait " + arg);
        } else if (arg != g_pickToken) {
            g_pickToken = arg;  // new Infusion (or first time): ask (Tick)
            g_pickChoice = -1;
            g_nextScreenAt = GetTickCount();
            reply("err Infusion choice pending");
        } else if (g_pickChoice < 0) {
            reply("err Infusion choice pending");
        } else if (g_pickChoice == kShieldChoice && !HasShield(controller)) {
            g_pickChoice = -1;
            g_nextScreenAt = GetTickCount();
            apnotify::ShowNoShield();
            reply("err Infusion choice pending");
        } else {
            bool given = false;
            GiveItem(controller, kInfusionChoices[g_pickChoice],
                     [&](const std::string& line) { given = line.rfind("called ", 0) == 0; });
            if (given) {
                savedata::Set(controller, variable, 1);
                Log("inventory: Infusion given (%s)", arg.c_str());
                g_pickToken.clear();  // given: the client moves on
                g_pickChoice = -1;
            }
            // Never pass on the game's "called" line: the client would take it for another grant.
            reply(std::string(given ? "infusionpick done " : "infusionpick wait ") + arg);
        }
        return true;
    }
    if (cmd == "giveonce") {
        // giveonce <variable>_<id>: gives <id> unless that save variable is set, then sets it.
        const std::size_t sep = arg.rfind('_');
        const int id = sep == std::string::npos ? 0 : std::atoi(arg.c_str() + sep + 1);
        const std::wstring variable(arg.begin(), arg.end());
        int done = 0;
        if (id <= 0) {
            reply("err usage: giveonce <variable>_<item id>");
        } else if (!savedata::Get(controller, variable, &done)) {
            reply("giveonce wait " + arg);
        } else if (done > 0) {
            reply("giveonce done " + arg);
        } else {
            bool given = false;
            GiveItem(controller, id, [&](const std::string& line) { given = line.rfind("called ", 0) == 0; });
            if (given) {
                savedata::Set(controller, variable, 1);
                Log("inventory: item %d given once (%s)", id, arg.c_str());
            }
            reply(std::string(given ? "giveonce done " : "giveonce wait ") + arg);
        }
        return true;
    }
    if (cmd == "itemcheck") {
        // itemcheck <id> (debug): would the inventory code find this item?
        const int id = std::atoi(arg.c_str());
        const void* obj = ResolveForInventory(id);
        reply("itemcheck " + std::to_string(id) + " = " + (obj ? ue3::PathName(obj) : std::string("not found")));
        return true;
    }
    if (cmd != "giveitem") return false;
    char* end = nullptr;
    const long id = std::strtol(arg.c_str(), &end, 10);
    if (arg.empty() || *end != '\0') {
        reply("err usage: giveitem <item id>");
    } else {
        GiveItem(controller, static_cast<int>(id), reply);
    }
    return true;
}

}  // namespace bsap::inventory
