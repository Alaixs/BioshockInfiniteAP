#include "ap_client.h"

#include "ap_net.h"
#include "bridge.h"
#include "log.h"

#include "nlohmann/json.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <tuple>
#include <vector>

namespace bsap::apclient {

namespace {

using json = nlohmann::json;

constexpr char kGame[] = "BioShock Infinite";
constexpr DWORD kQueryMs = 2000;
constexpr DWORD kSyncDelayMs = 1000;  // resync once the replies are in
constexpr DWORD kReconnectMs = 5000;
constexpr int kStatusGoal = 30;         // ClientStatus.CLIENT_GOAL
constexpr DWORD kDeathGraceMs = 10000;  // a death we just caused is not sent back

State g_state = State::Idle;
Failure g_failure = Failure::None;
std::string g_failureDetail;
Address g_address;
std::string g_password;
DWORD g_reconnectAt = 0;

std::string g_seed;
int g_team = -1, g_slot = -1;
std::map<int, std::string> g_slotGame, g_alias;
std::map<std::string, std::map<std::int64_t, std::string>> g_itemNames;      // game -> id -> name
std::map<std::string, std::map<std::int64_t, std::string>> g_locationNames;  // game -> id -> name
json g_slotData;
std::vector<std::int64_t> g_items;  // received items, in the server's order
std::set<std::int64_t> g_checked, g_missing;
struct Scout {
    std::int64_t item;
    int player;
};
std::map<std::int64_t, Scout> g_scouts;

using ShopLine = std::tuple<std::string, std::string, int, std::string, bool>;  // id, machine, price, name, sold
bool g_inLevel = false;
std::vector<std::string> g_machines, g_incomingMachines;
std::optional<std::set<std::string>> g_sentLocks;  // nullopt: the mod's state is unknown
std::optional<bool> g_sentTearsLock;
std::optional<bool> g_sentLockpickLock;
std::map<std::string, bool> g_sentPassLocks;
std::optional<std::vector<ShopLine>> g_sentShop;
bool g_scouted = false;
bool g_goalSent = false;
// Goal sent (kept on disk): the server now collects and releases everything. None of it is given or
// announced, or dozens of Infusions would arrive at once.
bool g_gameOver = false;
DWORD g_nextQuery = 0, g_syncAt = 0;
// "seed:team:slot" of the AP rows in the game. Kept after a disconnect so an offline purchase goes
// to the right seed.
std::string g_sessionKey;
// Items given to the game (Silver Eagles, Infusions), not confirmed yet (index in g_items).
std::vector<std::size_t> g_currencyInFlight;
// Items already given in this playthrough, counted in the save (GivenVariable). -1 until read.
int g_given = -1;

// One save variable per seed: a save reused with another seed starts from zero.
std::string GivenVariable() {
    std::uint32_t hash = 2166136261u;  // FNV-1a
    for (unsigned char c : g_sessionKey) hash = (hash ^ c) * 16777619u;
    char name[32];
    std::snprintf(name, sizeof(name), "APGiven_%08x", hash);
    return name;
}
// Game id (lower case) -> location. Kept after a disconnect so an offline pickup still counts.
std::map<std::string, std::int64_t> g_voxophones;
std::map<std::string, std::int64_t> g_infusions;
std::map<std::string, std::int64_t> g_gear;
std::map<std::string, std::int64_t> g_checkpoints;
// Randomized Vigors: key -> location, and item -> (key, game object to give).
std::map<std::string, std::int64_t> g_vigorLocations;
struct VigorItem {
    std::string key;
    int lookup;
};
std::map<std::int64_t, VigorItem> g_vigorItems;
std::optional<std::string> g_sentVigorLocks;

std::function<void(const Notice&)> g_notifier;

void Notify(Notice::Kind kind, const std::string& item = {}, const std::string& player = {},
            const std::string& text = {}) {
    if (g_notifier) g_notifier({kind, item, player, text});
}

std::string g_pendingDeathSource;
bool g_pendingDeathAnnounced = false;
bool g_deathLink = false;
std::optional<std::string> g_pendingDeath;  // death received, applied as soon as in game
bool g_killInFlight = false;
DWORD g_killedAt = 0;

std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string Trim(std::string s) {
    const auto space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && space(s.back())) s.pop_back();
    while (!s.empty() && space(s.front())) s.erase(s.begin());
    return s;
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string DllDir() {
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&DllDir), &self);
    char path[MAX_PATH] = {};
    GetModuleFileNameA(self, path, MAX_PATH);
    std::string dir(path);
    return dir.substr(0, dir.find_last_of("\\/") + 1);
}

// BioShockAP.ini: address=, password=, autoconnect=, uuid=, debug= (UTF-8).
std::map<std::string, std::string> ReadConfig() {
    std::map<std::string, std::string> values;
    std::ifstream f(DllDir() + "BioShockAP.ini", std::ios::binary);
    for (std::string line; std::getline(f, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t eq = line.find('=');
        if (eq != std::string::npos) values[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return values;
}

void WriteConfig(const std::map<std::string, std::string>& values) {
    std::ofstream f(DllDir() + "BioShockAP.ini", std::ios::binary | std::ios::trunc);
    for (const auto& [key, value] : values) f << key << "=" << value << "\r\n";
}

std::string Uuid() {
    auto config = ReadConfig();
    if (config["uuid"].empty()) {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "bsap-%08lx%08lx", static_cast<unsigned long>(GetTickCount64()),
                      static_cast<unsigned long>(GetCurrentProcessId() * 2654435761u));
        config["uuid"] = buf;
        WriteConfig(config);
    }
    return config["uuid"];
}

void Send(const json& packets) {
    if (!apnet::Send(packets.dump())) Log("[ap] cannot send (not connected)");
}

// Name shown on a shop row: the game shows Latin-1 (French accents...); the rest is decomposed
// (e.g. é -> e) or replaced with "?". 60 characters at most.
std::string ShopText(const std::string& utf8) {
    std::wstring out;
    for (const wchar_t c : Wide(utf8)) {
        if (c >= 0x20 && c <= 0xFF && c != 0x7F) {
            out.push_back(c);
            continue;
        }
        wchar_t decomposed[8] = {};
        const int n = NormalizeString(NormalizationKD, &c, 1, decomposed, 8);
        bool kept = false;
        for (int i = 0; i < n; ++i) {
            if (decomposed[i] >= 0x20 && decomposed[i] < 0x7F) {
                out.push_back(decomposed[i]);
                kept = true;
            }
        }
        if (!kept && c >= 0x20) out.push_back(L'?');
    }
    while (!out.empty() && out.back() == L' ') out.pop_back();
    if (out.size() > 60) out.resize(60);
    return out.empty() ? "Archipelago Item" : Utf8(out);
}

std::string ItemName(std::int64_t item, int player) {
    const auto game = g_slotGame.find(player);
    if (game != g_slotGame.end()) {
        const auto names = g_itemNames.find(game->second);
        if (names != g_itemNames.end()) {
            const auto it = names->second.find(item);
            if (it != names->second.end()) return it->second;
        }
    }
    return "Item " + std::to_string(item);
}

std::string LocationName(std::int64_t location, int player) {
    const auto game = g_slotGame.find(player);
    if (game != g_slotGame.end()) {
        const auto names = g_locationNames.find(game->second);
        if (names != g_locationNames.end()) {
            const auto it = names->second.find(location);
            if (it != names->second.end()) return it->second;
        }
    }
    return "Location " + std::to_string(location);
}

// Machine path (lower case) -> locations it sells. Every version of a machine sells the same ones.
std::map<std::string, std::vector<std::int64_t>> ShopMachines() {
    std::map<std::string, std::vector<std::int64_t>> machines;
    if (g_slotData.contains("machines")) {
        for (const auto& [path, ids] : g_slotData["machines"].items()) {
            machines[Lower(path)] = ids.get<std::vector<std::int64_t>>();
        }
    }
    return machines;
}

std::vector<std::int64_t> ShopLocations() {
    std::set<std::int64_t> all;
    for (const auto& [path, ids] : ShopMachines()) all.insert(ids.begin(), ids.end());
    return {all.begin(), all.end()};
}

void SyncLocks() {
    const std::set<std::int64_t> received(g_items.begin(), g_items.end());
    std::set<std::string> wanted;
    if (g_slotData.value("lock_vending_items", true) && g_slotData.contains("unlocks")) {
        for (const auto& [id, patterns] : g_slotData["unlocks"].items()) {
            if (!received.count(std::stoll(id))) {
                for (const auto& p : patterns) wanted.insert(p.get<std::string>());
            }
        }
    }
    // Randomized Vigor not received yet: not sold in the Veni! Vidi! Vigor! (the mod would remove it).
    const json vigors = g_slotData.value("vigors", json::object());  // no items() on a temporary
    for (const auto& [key, v] : vigors.items()) {
        if (v.contains("loot") && !received.count(v.value("item", std::int64_t{0})))
            wanted.insert(v["loot"].get<std::string>());
    }
    const json locked = g_slotData.value("locked_loot", json::array());
    for (const auto& loot : locked) wanted.insert(loot.get<std::string>());
    if (!g_sentLocks) {
        bridge::Inject("lockclear");
        g_sentLocks.emplace();
    }
    for (const std::string& p : wanted) {
        if (!g_sentLocks->count(p)) bridge::Inject("lock " + p);
    }
    for (const std::string& p : *g_sentLocks) {
        if (!wanted.count(p)) bridge::Inject("unlock " + p);
    }
    g_sentLocks = wanted;
    const bool tearsLocked =
        g_slotData.contains("tears_item") && !received.count(g_slotData["tears_item"].get<std::int64_t>());
    if (g_sentTearsLock != tearsLocked) {
        bridge::Inject(tearsLocked ? "tears lock" : "tears unlock");
        g_sentTearsLock = tearsLocked;
    }
    const bool lockpickLocked =
        g_slotData.contains("lockpick_item") && !received.count(g_slotData["lockpick_item"].get<std::int64_t>());
    if (g_sentLockpickLock != lockpickLocked) {
        bridge::Inject(lockpickLocked ? "lockpick lock" : "lockpick unlock");
        g_sentLockpickLock = lockpickLocked;
    }
    const json passes = g_slotData.value("passes", json::object());
    for (const auto& [id, key] : passes.items()) {
        const bool passLocked = !received.count(std::stoll(id));
        const auto sent = g_sentPassLocks.find(key.get<std::string>());
        if (sent == g_sentPassLocks.end() || sent->second != passLocked) {
            bridge::Inject(std::string("pass ") + (passLocked ? "lock " : "unlock ") + key.get<std::string>());
            g_sentPassLocks[key.get<std::string>()] = passLocked;
        }
    }
}

// BioShockAP_state.json, per seed: checks made in game (resent on every connection, so a purchase
// made offline isn't lost) and the received items already announced.

std::string StatePath() {
    return DllDir() + "BioShockAP_state.json";
}

json LoadState() {
    std::ifstream f(StatePath(), std::ios::binary);
    json state = f ? json::parse(f, nullptr, false) : json::object();
    return state.is_object() ? state : json::object();
}

json SessionState() {
    const json entry = LoadState().value(g_sessionKey, json::object());
    if (entry.is_number()) return {{"currency", entry}};  // old format: the counter alone
    return entry.is_object() ? entry : json::object();
}

// Written to a temporary file then renamed: a crash never leaves a half-written state.
void SaveSessionState(const json& entry) {
    json state = LoadState();
    state[g_sessionKey] = entry;
    const std::string tmp = StatePath() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << state.dump();
        if (!f) return;
    }
    MoveFileExA(tmp.c_str(), StatePath().c_str(), MOVEFILE_REPLACE_EXISTING);
}

std::vector<std::int64_t> LocalChecks() {
    return SessionState().value("checks", std::vector<std::int64_t>{});
}

void AddLocalCheck(std::int64_t loc) {
    json entry = SessionState();
    auto checks = entry.value("checks", std::vector<std::int64_t>{});
    if (std::find(checks.begin(), checks.end(), loc) != checks.end()) return;
    checks.push_back(loc);
    entry["checks"] = checks;
    SaveSessionState(entry);
}

// Silver Eagles and Gear go out in order through "apgive <i+1> <command>": once the game confirms,
// the mod moves the counter in the save forward. Reloading a checkpoint rolls the counter back, so
// those items are given again.
int SlotDataNumber(const char* table, std::size_t index) {
    const auto it = g_slotData.find(table);
    if (it == g_slotData.end() || !it->is_object()) return 0;
    return it->value(std::to_string(g_items[index]), 0);
}

// Bridge command giving the item to the game; empty if the item gives nothing (unlock...).
std::string GrantCommand(std::size_t index) {
    if (const int amount = SlotDataNumber("currency", index)) {
        return "call ClientAdjustCurrency 1 " + std::to_string(amount);
    }
    if (const int lookup = SlotDataNumber("upgrades", index)) {
        return "giveitem " + std::to_string(lookup);
    }
    if (const auto it = g_slotData.find("gear_items"); it != g_slotData.end() && it->is_object()) {
        // The item database is always loaded: Gear can be given in any level.
        if (const auto piece = it->find(std::to_string(g_items[index])); piece != it->end()) {
            return "call ServerTransactLootItem " + std::to_string(piece->get<std::uint64_t>()) + " 1 1 0";
        }
    }
    return {};
}

bool IsGranted(std::size_t index) {
    return !GrantCommand(index).empty();
}

void GiveCurrency() {
    g_currencyInFlight.clear();
    if (g_given < 0 || g_gameOver) return;  // not read yet, or seed finished
    for (std::size_t i = static_cast<std::size_t>(g_given); i < g_items.size(); ++i) {
        if (const std::string cmd = GrantCommand(i); !cmd.empty()) {
            bridge::Inject("apgive " + GivenVariable() + " " + std::to_string(i + 1) + " " + cmd);
            g_currencyInFlight.push_back(i);
            // A grant may be refused (no Booker, object not in memory: "err"): nothing after it,
            // otherwise the next confirmation would be attributed to it.
            if (cmd.rfind("giveitem ", 0) == 0 || cmd.rfind("call ServerTransactLootItem ", 0) == 0) {
                break;
            }
        }
    }
}

// Items given outside the queue: the game only has them in memory in some levels (an Infusion
// doesn't exist at the start of Town Center), they must not hold back the next ones.
// Each has its own variable in the save ("given"); asked again every cycle, because a reloaded
// checkpoint erases the variable along with the item.
//   upgrades and Vigors (unique): giveonce <variable>_<item>, given as soon as it is in memory;
//   Infusions (the same item several times): one variable per receipt, <variable>_i<index>; the
//   mod tells which ones are missing (dpvmissing), the player chooses for the first (infusionpick).
void GiveOnce() {
    if (g_given < 0 || g_gameOver) return;
    const json table = g_slotData.value("upgrade_items", json::object());
    const std::int64_t infusion = g_slotData.value("infusion_item", std::int64_t{-1});
    std::set<int> lookups;
    std::string infusions;
    for (std::size_t i = 0; i < g_items.size(); ++i) {
        if (const auto it = table.find(std::to_string(g_items[i])); it != table.end()) lookups.insert(it->get<int>());
        if (const auto it = g_vigorItems.find(g_items[i]); it != g_vigorItems.end()) lookups.insert(it->second.lookup);
        if (g_items[i] == infusion) infusions += " " + GivenVariable() + "_i" + std::to_string(i);
    }
    for (const int lookup : lookups) bridge::Inject("giveonce " + GivenVariable() + "_" + std::to_string(lookup));
    if (!infusions.empty()) bridge::Inject("dpvmissing" + infusions);
}

void OnCurrencyGiven() {
    if (g_currencyInFlight.empty()) return;
    const std::size_t i = g_currencyInFlight.front();
    g_currencyInFlight.erase(g_currencyInFlight.begin());
    Log("[ap] item given to the game: %s", ItemName(g_items[i], g_slot).c_str());
    g_given = std::max(g_given, static_cast<int>(i + 1));  // the mod wrote the same value in the save
}

void SyncShop() {
    const std::vector<std::int64_t> all = ShopLocations();
    if (!g_scouted && !all.empty()) {
        Send(json::array({{{"cmd", "LocationScouts"}, {"locations", all}, {"create_as_hint", 0}}}));
        g_scouted = true;
    }
    const auto machines = ShopMachines();
    const json prices = g_slotData.value("prices", json::object());
    std::vector<std::string> loaded;
    for (const std::string& path : g_machines) {
        if (machines.count(Lower(path))) loaded.push_back(path);
    }
    std::sort(loaded.begin(), loaded.end());
    std::vector<ShopLine> wanted;
    std::set<std::int64_t> placed;  // two versions of a machine loaded: rows on the first one
    for (const std::string& path : loaded) {
        for (const std::int64_t loc : machines.at(Lower(path))) {
            if (!placed.insert(loc).second) continue;
            const std::string id = std::to_string(loc);
            std::string name = "Archipelago Item";
            if (const auto s = g_scouts.find(loc); s != g_scouts.end()) {
                name = ItemName(s->second.item, s->second.player);
                if (s->second.player != g_slot) name += " (" + g_alias[s->second.player] + ")";
            }
            wanted.emplace_back(id, path, prices.value(id, 10), ShopText(name), g_checked.count(loc) > 0);
        }
    }
    if (g_sentShop && *g_sentShop == wanted) return;
    bridge::Inject("shopclear");
    for (const auto& [id, machine, price, name, sold] : wanted) {
        bridge::Inject("shopslot " + id + " " + machine + " " + std::to_string(price) + " " + name);
        if (sold) bridge::Inject("shopsold " + id);
    }
    g_sentShop = wanted;
}

// Goal (slot_data "goal"): "siphon" = Songbird destroys the Siphon (seen by the mod, kept on disk
// if it happens offline); "shop" (default of old seeds) = the whole shop bought.
void CheckGoal() {
    if (g_goalSent) return;
    if (g_slotData.value("goal", std::string("shop")) == "siphon") {
        if (!SessionState().value("goal_reached", false)) return;
        Log("[ap] Siphon destroyed: goal complete");
    } else {
        const std::vector<std::int64_t> all = ShopLocations();
        if (all.empty() ||
            std::any_of(all.begin(), all.end(), [](std::int64_t loc) { return g_missing.count(loc) > 0; })) {
            return;
        }
        Log("[ap] the whole shop is bought: goal complete");
    }
    Send(json::array({{{"cmd", "StatusUpdate"}, {"status", kStatusGoal}}}));
    g_goalSent = true;
    if (!g_gameOver) {
        g_gameOver = true;
        json entry = SessionState();
        entry["goal_done"] = true;
        SaveSessionState(entry);
        Notify(Notice::Kind::Goal);
    }
}

// Locked Vigors: those whose item is not received yet (the mod removes them if they land in the
// inventory). Sent before the grants: a received Vigor is unlocked before being given.
void SyncVigorLocks() {
    const std::set<std::int64_t> received(g_items.begin(), g_items.end());
    std::string locked, owned;
    for (const auto& [item, v] : g_vigorItems) {
        if (!received.count(item))
            locked += v.key + " ";
        else
            owned += v.key + ":" + std::to_string(v.lookup) + " ";
    }
    if (g_sentVigorLocks && *g_sentVigorLocks == locked + "|" + owned) return;
    bridge::Inject("vigorlock clear");
    std::istringstream keys(locked);
    for (std::string key; keys >> key;) bridge::Inject("vigorlock " + key);
    // Received: the mod gives them again if a game script removes them (a chapter's loadout).
    bridge::Inject("vigorown clear");
    std::istringstream pairs(owned);
    for (std::string pair; pairs >> pair;) bridge::Inject("vigorown " + pair);
    g_sentVigorLocks = locked + "|" + owned;
}

void Sync() {
    if (g_slotData.is_null() || g_slot < 0) return;
    SyncLocks();
    SyncVigorLocks();
    if (!g_inLevel) return;
    GiveCurrency();
    GiveOnce();
    SyncShop();
    CheckGoal();
}

double UnixTime() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    const auto t = (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return static_cast<double>(t - 116444736000000000ULL) / 1e7;
}

void OnDeath() {
    if (!g_deathLink) return;
    if (g_killedAt && GetTickCount() - g_killedAt < kDeathGraceMs) return;
    const std::string cause = g_address.name + " died in Columbia.";
    Send(json::array({{{"cmd", "Bounce"},
                       {"tags", json::array({"DeathLink"})},
                       {"data", {{"time", UnixTime()}, {"source", g_address.name}, {"cause", cause}}}}}));
    Log("[ap] DeathLink: death sent to the other players");
}

void ApplyPendingDeath() {
    if (!g_pendingDeath || g_killInFlight || !g_inLevel) return;
    Log("[ap] DeathLink: %s", g_pendingDeath->c_str());
    if (!g_pendingDeathAnnounced) {  // announced once, even if the game must retry
        Notify(Notice::Kind::DeathLink, {}, g_pendingDeathSource, *g_pendingDeath);
        g_pendingDeathAnnounced = true;
    }
    g_killInFlight = true;
    g_killedAt = GetTickCount();
    bridge::Inject("kill");
}

// Kept on disk, sent now or on the next connection.
void CheckLocation(std::int64_t loc) {
    AddLocalCheck(loc);
    g_checked.insert(loc);
    g_missing.erase(loc);
    if (g_state == State::Connected) {
        Send(json::array({{{"cmd", "LocationChecks"}, {"locations", {loc}}}}));
    } else {
        Log("[ap] location %lld kept for the next connection", static_cast<long long>(loc));
    }
}

// "voxophone" / "infusion" (words of the mod's lines) -> matching location table.
const std::map<std::string, std::int64_t>* PickupsOf(const std::string& kind) {
    if (kind == "voxophone") return &g_voxophones;
    if (kind == "infusion") return &g_infusions;
    if (kind == "vigor") return &g_vigorLocations;
    if (kind == "gear") return &g_gear;
    if (kind == "checkpoint") return &g_checkpoints;
    return nullptr;
}

void OnPickedUp(const std::string& kind, const std::string& id, std::int64_t loc) {
    if (g_checked.count(loc)) return;
    Log("[ap] %s picked up: %s", kind.c_str(), id.c_str());
    CheckLocation(loc);
}

bool IsPickupLocation(std::int64_t loc) {
    for (const auto* pickups : {&g_voxophones, &g_infusions, &g_vigorLocations, &g_gear, &g_checkpoints}) {
        for (const auto& [id, l] : *pickups) {
            if (l == loc) return true;
        }
    }
    return false;
}

void LoadPickups(const char* key, std::map<std::string, std::int64_t>* out) {
    out->clear();
    const json table = g_slotData.value(key, json::object());
    for (const auto& [id, loc] : table.items()) (*out)[Lower(id)] = loc.get<std::int64_t>();
}

void OnModLine(const std::string& line) {
    std::istringstream in(line);
    std::vector<std::string> parts;
    for (std::string w; in >> w;) parts.push_back(w);
    if (parts.empty()) return;
    if (parts[0] == "event" && parts.size() >= 3 && parts[1] == "goal" && !g_sessionKey.empty()) {
        json entry = SessionState();
        if (!entry.value("goal_reached", false)) {
            entry["goal_reached"] = true;
            SaveSessionState(entry);
            Log("[ap] end of the game reached (%s)", parts[2].c_str());
        }
        return;
    }
    if (parts[0] == "event" && parts.size() >= 3 && parts[1] == "shopbuy" && !g_sessionKey.empty()) {
        CheckLocation(std::stoll(parts[2]));
        g_sentShop.reset();  // the bought row must be "sold" everywhere
        return;
    }
    if (parts[0] == "event" && parts.size() >= 3 && !g_sessionKey.empty()) {
        if (const auto* pickups = PickupsOf(parts[1])) {
            const auto it = pickups->find(Lower(parts[2]));
            if (it == pickups->end()) {
                Log("[ap] %s %s: not a location of this seed", parts[1].c_str(), parts[2].c_str());
            } else {
                OnPickedUp(parts[1], parts[2], it->second);
            }
            return;
        }
    }
    // Voxophone loaded in the level, picked up (state 4): checked if not done yet. Not for
    // Infusions: some bottles are hidden in that state from the load, the mod itself reports the
    // real pickups ("event infusion").
    if (parts.size() >= 3 && !g_sessionKey.empty() && parts[0] == "voxophone") {
        if (const auto* pickups = PickupsOf(parts[0])) {
            const auto it = pickups->find(Lower(parts[1]));
            if (parts[2] == "4" && it != pickups->end()) OnPickedUp(parts[0], parts[1], it->second);
            return;
        }
    }
    if (g_state != State::Connected) return;
    if (parts[0] == "called" && (line.find(".ClientAdjustCurrency(") != std::string::npos ||
                                 line.find(".ServerTransactLootItem(") != std::string::npos)) {
        OnCurrencyGiven();
    } else if (parts[0] == "dpvmissing") {
        if (parts.size() >= 2 && !g_gameOver) bridge::Inject("infusionpick " + parts[1]);
    } else if (parts[0] == "dpv" && parts.size() >= 3 && parts[1] == GivenVariable()) {
        g_given = std::atoi(parts[2].c_str());
    } else if (parts[0] == "where") {
        g_inLevel = parts.size() > 1 && parts[1] != "none";
    } else if (parts[0] == "poi" && parts.size() >= 9) {
        g_incomingMachines.push_back(parts[7]);
    } else if (line.rfind("end poi", 0) == 0) {
        std::vector<std::string> a = g_incomingMachines, b = g_machines;
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        if (a != b) g_sentShop.reset();
        g_machines = std::move(g_incomingMachines);
        g_incomingMachines.clear();
    } else if (parts[0] == "event" && parts.size() >= 2 && parts[1] == "shopfail") {
        Log("[ap] not enough Silver Eagles for this row");
    } else if (parts[0] == "event" && parts.size() >= 2 && parts[1] == "death") {
        OnDeath();
    } else if (g_killInFlight && parts[0] == "called" && line.find(".Suicide(") != std::string::npos) {
        g_killInFlight = false;
        g_killedAt = GetTickCount();
        g_pendingDeath.reset();
    } else if (g_killInFlight && line.rfind("err no character", 0) == 0) {
        g_killInFlight = false;  // loading: try again once back in game
        g_inLevel = false;
    }
}

json ConnectPacket() {
    return {{"cmd", "Connect"},
            {"password", g_password},
            {"game", kGame},
            {"name", g_address.name},
            {"uuid", Uuid()},
            {"version", {{"major", 0}, {"minor", 6}, {"build", 7}, {"class", "Version"}}},
            {"items_handling", 0b111},  // every item comes from the server, ours included
            {"tags", json::array({"AP"})},
            {"slot_data", true}};
}

void OnPacket(const json& p) {
    const std::string cmd = p.value("cmd", "");
    if (cmd == "RoomInfo") {
        g_seed = p.value("seed_name", "");
        Send(json::array({{{"cmd", "GetDataPackage"}, {"games", p.value("games", json::array())}}, ConnectPacket()}));
    } else if (cmd == "DataPackage") {
        for (const auto& [game, data] : p["data"]["games"].items()) {
            auto& names = g_itemNames[game];
            // Copy kept in a variable: items() of a temporary would point to a destroyed object.
            const json table = data.value("item_name_to_id", json::object());
            for (const auto& [name, id] : table.items()) {
                names[id.get<std::int64_t>()] = name;
            }
            auto& locations = g_locationNames[game];
            const json locationTable = data.value("location_name_to_id", json::object());
            for (const auto& [name, id] : locationTable.items()) {
                locations[id.get<std::int64_t>()] = name;
            }
        }
    } else if (cmd == "Connected") {
        g_team = p.value("team", 0);
        g_slot = p.value("slot", -1);
        for (const auto& player : p.value("players", json::array())) {
            g_alias[player.value("slot", 0)] = player.value("alias", "");
        }
        const json slotInfo = p.value("slot_info", json::object());
        for (const auto& [slot, info] : slotInfo.items()) {
            g_slotGame[std::stoi(slot)] = info.value("game", "");
        }
        const auto checked = p.value("checked_locations", std::vector<std::int64_t>{});
        const auto missing = p.value("missing_locations", std::vector<std::int64_t>{});
        g_checked = {checked.begin(), checked.end()};
        g_missing = {missing.begin(), missing.end()};
        g_slotData = p.value("slot_data", json::object());
        g_items.clear();
        g_scouts.clear();
        g_sentLocks.reset();
        g_sentTearsLock.reset();
        g_sentPassLocks.clear();
        g_sentLockpickLock.reset();
        g_sentShop.reset();
        g_scouted = false;
        g_goalSent = false;
        g_gameOver = false;
        g_currencyInFlight.clear();
        g_deathLink = g_slotData.value("death_link", false);
        LoadPickups("voxophones", &g_voxophones);
        LoadPickups("infusions", &g_infusions);
        LoadPickups("gear", &g_gear);
        LoadPickups("checkpoints", &g_checkpoints);
        g_vigorLocations.clear();
        g_vigorItems.clear();
        g_sentVigorLocks.reset();
        const json vigors = g_slotData.value("vigors", json::object());
        for (const auto& [key, v] : vigors.items()) {
            g_vigorLocations[key] = v.value("location", std::int64_t{0});
            g_vigorItems[v.value("item", std::int64_t{0})] = {key, v.value("lookup", 0)};
        }
        bridge::Inject("infusionap clear");
        for (const auto& [id, loc] : g_infusions) bridge::Inject("infusionap " + id);
        bridge::Inject("gearap clear");
        for (const auto& [id, loc] : g_gear) bridge::Inject("gearap " + id);
        g_pendingDeath.reset();
        g_killInFlight = false;
        if (g_deathLink) {
            Send(json::array({{{"cmd", "ConnectUpdate"}, {"tags", json::array({"AP", "DeathLink"})}}}));
            Log("[ap] DeathLink on");
        }
        g_sessionKey = g_seed + ":" + std::to_string(g_team) + ":" + std::to_string(g_slot);
        g_gameOver = SessionState().value("goal_done", false);
        std::vector<std::int64_t> unsent;
        for (const std::int64_t loc : LocalChecks()) {
            if (!g_checked.count(loc)) unsent.push_back(loc);
        }
        if (!unsent.empty()) {
            Send(json::array({{{"cmd", "LocationChecks"}, {"locations", unsent}}}));
            for (const std::int64_t loc : unsent) {
                g_checked.insert(loc);
                g_missing.erase(loc);
            }
            Log("[ap] %zu purchase(s) made offline sent", unsent.size());
        }
        if (g_state == State::Reconnecting) Notify(Notice::Kind::Reconnected);
        g_state = State::Connected;
        g_failure = Failure::None;
        auto config = ReadConfig();
        config["address"] = g_address.name + "@" +
                            (g_address.scheme == 1   ? "wss://"
                             : g_address.scheme == 2 ? "ws://"
                                                     : "") +
                            g_address.HostPort();
        config["password"] = g_password;
        WriteConfig(config);
        Log("[ap] connected: slot %d (%s), team %d, %zu locations left", g_slot, g_address.name.c_str(), g_team,
            g_missing.size());
    } else if (cmd == "ConnectionRefused") {
        std::string errors;
        g_failure = Failure::Refused;
        for (const auto& e : p.value("errors", json::array())) {
            const std::string code = e.get<std::string>();
            errors += (errors.empty() ? "" : ", ") + code;
            if (code == "InvalidSlot")
                g_failure = Failure::InvalidSlot;
            else if (code == "InvalidGame")
                g_failure = Failure::InvalidGame;
            else if (code == "InvalidPassword")
                g_failure = Failure::InvalidPassword;
            else if (code == "IncompatibleVersion")
                g_failure = Failure::IncompatibleVersion;
        }
        g_failureDetail = errors;
        Log("[ap] connection refused: %s", errors.c_str());
        apnet::Close();
        g_state = State::Idle;
    } else if (cmd == "ReceivedItems") {
        const std::size_t index = p.value("index", std::size_t{0});
        if (index == 0) g_items.clear();
        if (index != g_items.size()) {  // gap in the sequence: ask for everything again
            Send(json::array({{{"cmd", "Sync"}}}));
            return;
        }
        // Every connection replays the whole history: only announce what was never announced.
        json state = SessionState();
        const std::size_t announced = state.value("announced", std::size_t{0});
        for (const auto& item : p.value("items", json::array())) {
            const std::int64_t id = item.value("item", std::int64_t{0});
            g_items.push_back(id);
            if (g_items.size() > announced) {
                const int sender = item.value("player", 0);
                Log("[ap] received: %s (from %s)", ItemName(id, g_slot).c_str(), g_alias[sender].c_str());
                // Traps fire once, on receipt (or on reconnection if they came in offline).
                if (!g_gameOver) {
                    const json traps = g_slotData.value("traps", json::object());
                    if (const auto trap = traps.find(std::to_string(id)); trap != traps.end()) {
                        bridge::Inject("trap " + trap->get<std::string>());
                    }
                }
                // Our own item found in a Dollar Bill: the purchase is enough, no announcement.
                if (sender != g_slot && !g_gameOver)
                    Notify(Notice::Kind::ItemReceived, ItemName(id, g_slot), g_alias[sender]);
            }
        }
        if (g_items.size() > announced) {
            state["announced"] = g_items.size();
            SaveSessionState(state);
        }
        if (index == 0) Log("[ap] %zu item(s) received since the start of the seed", g_items.size());
    } else if (cmd == "LocationInfo") {
        for (const auto& l : p.value("locations", json::array())) {
            g_scouts[l.value("location", std::int64_t{0})] = {l.value("item", std::int64_t{0}), l.value("player", 0)};
        }
        g_sentShop.reset();
    } else if (cmd == "RoomUpdate") {
        for (const std::int64_t loc : p.value("checked_locations", std::vector<std::int64_t>{})) {
            g_checked.insert(loc);
            g_missing.erase(loc);
            g_sentShop.reset();
        }
        for (const auto& player : p.value("players", json::array())) {
            g_alias[player.value("slot", 0)] = player.value("alias", "");
        }
    } else if (cmd == "Bounced") {
        const json tags = p.value("tags", json::array());
        const json data = p.value("data", json::object());
        if (g_deathLink && std::find(tags.begin(), tags.end(), "DeathLink") != tags.end()) {
            const std::string source = data.value("source", "");
            if (source == g_address.name) return;  // the echo of our own death
            g_pendingDeath = data.value("cause", "");
            g_pendingDeathSource = source;
            g_pendingDeathAnnounced = false;
        }
    } else if (cmd == "PrintJSON") {
        std::string text;
        for (const auto& part : p.value("data", json::array())) {
            const std::string piece = part.value("text", "");
            if (part.value("type", "") == "player_id")
                text += g_alias[std::atoi(piece.c_str())];
            else if (part.value("type", "") == "item_id")
                text += ItemName(std::stoll(piece), part.value("player", 0));
            else if (part.value("type", "") == "location_id")
                text += LocationName(std::stoll(piece), part.value("player", 0));
            else
                text += piece;
        }
        Log("[ap] %s", text.c_str());
        const json item = p.value("item", json::object());
        const int receiving = p.value("receiving", -1);
        if (g_gameOver) {
        } else if (p.value("type", "") == "ItemSend" && item.value("player", -1) == g_slot && receiving >= 0 &&
                   receiving != g_slot) {
            Notify(Notice::Kind::ItemSent, ItemName(item.value("item", std::int64_t{0}), receiving),
                   g_alias[receiving]);
        } else if (p.value("type", "") == "ItemSend" && item.value("player", -1) == g_slot && receiving == g_slot &&
                   IsPickupLocation(item.value("location", std::int64_t{0}))) {
            // Our item, in a Voxophone or a bottle: nothing else tells what it contained (a Dollar
            // Bill row already shows its item).
            Notify(Notice::Kind::ItemFound, ItemName(item.value("item", std::int64_t{0}), g_slot));
        }
    }
}

void OnNetEvent(const apnet::Event& e) {
    switch (e.type) {
        case apnet::EventType::Open:
            break;  // the server speaks first (RoomInfo)
        case apnet::EventType::Message:
            try {
                const json packets = json::parse(e.text);
                for (const json& p : packets) {
                    try {  // a failed packet must not lose the next ones
                        OnPacket(p);
                    } catch (const std::exception& ex) {
                        Log("[ap] unreadable packet %s: %s", p.value("cmd", "?").c_str(), ex.what());
                    }
                }
            } catch (const std::exception& ex) {
                Log("[ap] unreadable message: %s", ex.what());
            }
            break;
        case apnet::EventType::Close:
            Log("[ap] connection closed: %s", e.text.c_str());
            if (g_state == State::Connecting) {
                g_state = State::Idle;
                g_failure = Failure::Unreachable;
                g_failureDetail = e.text;
            } else if (g_state == State::Connected || g_state == State::Reconnecting) {
                if (g_state == State::Connected) Notify(Notice::Kind::ConnectionLost);
                g_state = State::Reconnecting;
                g_reconnectAt = GetTickCount() + kReconnectMs;
            }
            break;
    }
}

void OpenSocket() {
    const apnet::Scheme scheme = g_address.scheme == 1   ? apnet::Scheme::Secure
                                 : g_address.scheme == 2 ? apnet::Scheme::Plain
                                                         : apnet::Scheme::Auto;
    apnet::Open(Wide(g_address.host), g_address.port, scheme);
}

}  // namespace

bool ParseAddress(const std::string& text, Address* out) {
    const std::string s = Trim(text);
    const std::size_t at = s.rfind('@');
    if (at == std::string::npos) return false;
    Address a;
    a.name = Trim(s.substr(0, at));
    std::string rest = Lower(Trim(s.substr(at + 1)));
    for (const auto& [prefix, scheme] : {std::pair<const char*, int>{"wss://", 1}, {"ws://", 2}}) {
        if (rest.rfind(prefix, 0) == 0) {
            rest = rest.substr(std::strlen(prefix));
            a.scheme = scheme;
        }
    }
    while (!rest.empty() && rest.back() == '/') rest.pop_back();
    const std::size_t colon = rest.rfind(':');
    a.host = rest.substr(0, colon);
    if (colon != std::string::npos) {
        const std::string port = rest.substr(colon + 1);
        if (port.empty() || port.size() > 5 || !std::all_of(port.begin(), port.end(), ::isdigit)) return false;
        a.port = std::atoi(port.c_str());
    }
    if (a.name.empty() || a.host.empty() || a.port < 1 || a.port > 65535) return false;
    *out = a;
    return true;
}

void Connect(const Address& address, const std::string& password) {
    apnet::Close();
    g_address = address;
    g_password = password;
    g_state = State::Connecting;
    g_failure = Failure::None;
    g_failureDetail.clear();
    g_slot = -1;
    g_slotData = json();
    Log("[ap] connecting to %s as %s", address.HostPort().c_str(), address.name.c_str());
    OpenSocket();
}

void Disconnect() {
    apnet::Close();
    g_state = State::Idle;
    g_slot = -1;
    g_slotData = json();
    Log("[ap] disconnected");
}

namespace {

LocationStatus PickupStatus(const std::map<std::string, std::int64_t>& pickups, const std::string& id) {
    const auto it = pickups.find(Lower(id));
    if (it == pickups.end()) return LocationStatus::NotALocation;
    return g_checked.count(it->second) ? LocationStatus::Done : LocationStatus::ToDo;
}

}  // namespace

LocationStatus VoxophoneStatus(const std::string& id) {
    return PickupStatus(g_voxophones, id);
}

LocationStatus InfusionStatus(const std::string& id) {
    return PickupStatus(g_infusions, id);
}

LocationStatus GearStatus(const std::string& id) {
    return PickupStatus(g_gear, id);
}

int MachineRemaining(const std::string& path) {
    if (g_slotData.is_null()) return -1;
    const auto machines = ShopMachines();
    const auto it = machines.find(Lower(path));
    if (it == machines.end()) return -1;
    return static_cast<int>(
        std::count_if(it->second.begin(), it->second.end(), [](std::int64_t loc) { return !g_checked.count(loc); }));
}

std::size_t CheckedCount() {
    return g_checked.size();
}

std::size_t MissingCount() {
    return g_missing.size();
}

void SetNotifier(std::function<void(const Notice&)> notifier) {
    g_notifier = std::move(notifier);
}

State GetState() {
    return g_state;
}

Failure LastFailure() {
    return g_failure;
}

std::string FailureDetail() {
    return g_failureDetail;
}

const Address& CurrentAddress() {
    return g_address;
}

void Saved(std::string* address, std::string* password) {
    auto config = ReadConfig();
    *address = config["address"];
    *password = config["password"];
}

bool ConnectSaved() {
    std::string text, password;
    Saved(&text, &password);
    Address address;
    if (text.empty() || !ParseAddress(text, &address)) return false;
    Connect(address, password);
    return true;
}

bool AutoConnectEnabled() {
    return ReadConfig()["autoconnect"] != "0";
}

void SetAutoConnect(bool enabled) {
    auto config = ReadConfig();
    config["autoconnect"] = enabled ? "1" : "0";
    WriteConfig(config);
    Log("[ap] auto-connect %s", enabled ? "on" : "off");
}

bool DebugEnabled() {
    static const bool debug = ReadConfig()["debug"] == "1";
    return debug;
}

void Tick() {
    static bool sinkInstalled = false;
    if (!sinkInstalled) {
        bridge::SetInternalSink(&OnModLine);
        sinkInstalled = true;
    }
    for (apnet::Event e; apnet::Poll(&e);) OnNetEvent(e);

    const DWORD now = GetTickCount();
    if (g_state == State::Reconnecting && static_cast<int>(now - g_reconnectAt) >= 0) {
        g_reconnectAt = now + kReconnectMs * 3;  // a Close will push the next attempt back
        Log("[ap] connection attempt");
        OpenSocket();
    }
    if (g_state != State::Connected) return;
    if (static_cast<int>(now - g_nextQuery) >= 0) {
        bridge::Inject("where");
        g_given = -1;  // read again from the loaded playthrough (a reloaded checkpoint lowers it)
        bridge::Inject("dpvget " + GivenVariable());
        bridge::Inject("poi XVendingMachine");
        if (!g_voxophones.empty()) bridge::Inject("voxophones");
        g_nextQuery = now + kQueryMs;
        g_syncAt = now + kSyncDelayMs;
    }
    ApplyPendingDeath();
    if (g_syncAt && static_cast<int>(now - g_syncAt) >= 0) {
        g_syncAt = 0;
        try {
            Sync();
        } catch (const std::exception& ex) {  // a logic error must not kill the connection
            Log("[ap] error during the sync: %s", ex.what());
        }
    }
}

}  // namespace bsap::apclient
