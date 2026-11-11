// Tests the Archipelago loop without the game: the real client (ap_client.cpp, ap_net.cpp) talks to
// a real server, and a fake game answers its text commands and makes purchases.
//
//   loop_test.exe Booker@localhost:38282 <apworld>/machines.py <server log> <DeathLink log> <server commands>
//
// run_loop_test.py runs a fake player (slot Finder) that logs each Booker death to the DeathLink
// log and answers with its own. Lines appended to the server commands file are typed into the
// server console.
//
// Needs a new seed (purchases are real checks). Deletes and writes BioShockAP.ini /
// BioShockAP_state.json next to the exe.

#include "../src/ap_client.h"
#include "../src/bridge.h"
#include "../src/log.h"

#include "nlohmann/json.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using namespace bsap;
using json = nlohmann::json;

namespace {
std::function<void(const std::string&)> g_sink;
std::deque<std::string> g_queue;
}  // namespace

namespace bsap::bridge {
void SetInternalSink(std::function<void(const std::string&)> sink) {
    g_sink = std::move(sink);
}
void Inject(const std::string& line) {
    g_queue.push_back(line);
}
}  // namespace bsap::bridge

struct Row {
    std::string machine;
    int price = 0;
    std::string name;
    bool sold = false;
};

struct FakeGame {
    std::vector<std::string> machines;
    std::map<std::string, int> voxophones;
    std::map<std::string, int> infusions;
    std::set<std::string> apInfusions;
    std::set<std::string> vigorLocks;
    std::set<std::string> vigorsOwned;
    std::set<std::string> apGear;
    std::vector<std::uint64_t> gear;  // Gear pieces given (ServerTransactLootItem, 64 bits)
    std::vector<int> onceGiven;
    std::vector<std::string> traps;
    std::vector<int> upgrades;       // items given (ServerTransactLootItem)
    std::map<std::string, int> dpv;  // save variables
    std::string apgiveVariable;
    int apgive = -1;
    std::set<std::string> locks;
    bool tearsLocked = false;
    bool lockpickLocked = false;
    std::set<std::string> passLocks;
    std::map<std::string, Row> shop;
    int money = 0;
    int moneyCalls = 0;
    int kills = 0;
    bool upgradesMissing = false;
    std::string pickToken;  // Infusion to choose (infusionpick)
    int pickChoice = -1;    // the "player's" choice (-1: not yet)
    bool loading = false;

    void Reply(const std::string& line) {
        if (apgive >= 0 && line.rfind("called ", 0) == 0) {
            dpv[apgiveVariable] = apgive;
            apgive = -1;
        }
        g_sink(line);
    }

    void Run(const std::string& line) {
        std::istringstream in(line);
        std::string cmd;
        in >> cmd;
        if (cmd == "apgive") {
            std::string rest;
            in >> apgiveVariable >> apgive;
            std::getline(in >> std::ws, rest);
            Run(rest);
            apgive = -1;
        } else if (cmd == "dpvget") {
            std::string name;
            in >> name;
            if (loading)
                g_sink("err no XPlayerController");
            else
                g_sink("dpv " + name + " " + std::to_string(dpv[name]));
        } else if (cmd == "where") {
            g_sink(loading ? "where none" : "where 0 0 0 0.0 S_TWN_P");
        } else if (cmd == "poi") {
            int i = 0;
            for (const std::string& m : machines) {
                g_sink("poi #" + std::to_string(++i) + " XVendingMachine 0 0 0 0.0 " + m + " DollarBill");
            }
            g_sink("end poi " + std::to_string(machines.size()));
        } else if (cmd == "voxophones") {
            for (const auto& [id, state] : voxophones) g_sink("voxophone " + id + " " + std::to_string(state));
            g_sink("end voxophones " + std::to_string(voxophones.size()));
        } else if (cmd == "vigorlock") {
            std::string key;
            in >> key;
            if (key == "clear")
                vigorLocks.clear();
            else
                vigorLocks.insert(key);
            g_sink("ok vigorlock " + std::to_string(vigorLocks.size()));
        } else if (cmd == "vigorown") {
            std::string key;
            in >> key;
            if (key == "clear")
                vigorsOwned.clear();
            else
                vigorsOwned.insert(key);
            g_sink("ok vigorown " + std::to_string(vigorsOwned.size()));
        } else if (cmd == "infusions") {
            for (const auto& [id, state] : infusions) g_sink("infusion " + id + " " + std::to_string(state));
            g_sink("end infusions " + std::to_string(infusions.size()));
        } else if (cmd == "giveonce") {
            std::string arg;
            in >> arg;
            if (loading || upgradesMissing) {
                g_sink("giveonce wait " + arg);
            } else {
                if (dpv[arg] == 0) {
                    dpv[arg] = 1;
                    onceGiven.push_back(std::stoi(arg.substr(arg.rfind('_') + 1)));
                }
                g_sink("giveonce done " + arg);
            }
        } else if (cmd == "trap") {
            std::string rest;
            std::getline(in >> std::ws, rest);
            traps.push_back(rest);
            g_sink("ok trap " + rest);
        } else if (cmd == "gearap") {
            std::string id;
            in >> id;
            if (id == "clear")
                apGear.clear();
            else
                apGear.insert(id);
            g_sink("ok gearap " + std::to_string(apGear.size()));
        } else if (cmd == "infusionap") {
            std::string id;
            in >> id;
            if (id == "clear")
                apInfusions.clear();
            else
                apInfusions.insert(id);
            g_sink("ok infusionap " + std::to_string(apInfusions.size()));
        } else if (cmd == "dpvmissing") {
            std::string missing, name;
            while (in >> name) {
                if (dpv[name] <= 0) missing += " " + name;
            }
            if (loading)
                g_sink("err no XPlayerController");
            else
                g_sink("dpvmissing" + missing);
        } else if (cmd == "infusionpick") {
            // Like the mod: the token is the save variable "Infusion given".
            std::string token;
            in >> token;
            if (dpv[token] > 0) {
                if (token == pickToken) pickToken.clear();
                g_sink("infusionpick done " + token);
            } else if (token != pickToken) {
                pickToken = token;
                pickChoice = -1;
                g_sink("err Infusion choice pending");
            } else if (pickChoice < 0) {
                g_sink("err Infusion choice pending");
            } else if (loading || upgradesMissing) {
                g_sink("infusionpick wait " + token);
            } else {
                upgrades.push_back(std::vector<int>{276, 329, 235}[pickChoice]);
                dpv[token] = 1;
                pickToken.clear();
                g_sink("infusionpick done " + token);
            }
        } else if (cmd == "giveitem") {
            std::string id;
            in >> id;
            if (loading || upgradesMissing) {
                g_sink("err item " + id + " not in memory");
            } else {
                upgrades.push_back(std::stoi(id));
                Reply("called XPlayerController.ServerTransactLootItem(ItemToBeAwardedID=" + id +
                      ", ItemQuantity=1, CurrencyType=1, Cost=0)");
            }
        } else if (cmd == "tears") {
            std::string state;
            in >> state;
            tearsLocked = state == "lock";
            g_sink(std::string("ok tears ") + (tearsLocked ? "locked" : "unlocked"));
        } else if (cmd == "lockpick") {
            std::string state;
            in >> state;
            lockpickLocked = state == "lock";
            g_sink(std::string("ok lockpick ") + (lockpickLocked ? "locked" : "unlocked"));
        } else if (cmd == "pass") {
            std::string action, key;
            in >> action >> key;
            if (action == "lock")
                passLocks.insert(key);
            else
                passLocks.erase(key);
            g_sink("ok pass " + key + " " + action + "ed");
        } else if (cmd == "lockclear") {
            locks.clear();
        } else if (cmd == "lock" || cmd == "unlock") {
            std::string pattern;
            in >> pattern;
            if (cmd == "lock")
                locks.insert(pattern);
            else
                locks.erase(pattern);
        } else if (cmd == "shopclear") {
            shop.clear();
        } else if (cmd == "shopslot") {
            std::string id;
            Row r;
            in >> id >> r.machine >> r.price;
            std::getline(in >> std::ws, r.name);
            shop[id] = r;
        } else if (cmd == "shopsold") {
            std::string id;
            in >> id;
            shop[id].sold = true;
        } else if (cmd == "kill") {
            if (loading) {
                g_sink("err no character to kill");
            } else {
                ++kills;
                g_sink("called XPlayerController.Suicide()");
                g_sink("event death");  // like the real game: the death is seen afterwards
            }
        } else if (cmd == "call") {
            std::string func, type;
            int amount = 0;
            in >> func >> type >> amount;
            if (loading) {
                g_sink("err object controller not found");
            } else if (func == "ServerTransactLootItem") {
                gear.push_back(std::stoull(type));
                Reply("called XPlayerController.ServerTransactLootItem(ItemToBeAwardedID=" + type +
                      ", ItemQuantity=1, CurrencyType=1, Cost=0)");
            } else {
                money += amount;
                ++moneyCalls;
                Reply("called XPlayerController.ClientAdjustCurrency(CurrencyType=1, Amount=" + std::to_string(amount) +
                      ")");
            }
        } else {
            g_sink("err unknown command: " + cmd);
        }
    }

    void Frame() {
        for (int i = 0; i < 64 && !g_queue.empty(); ++i) {
            const std::string line = g_queue.front();
            g_queue.pop_front();
            Run(line);
        }
    }

    void Buy(const std::string& id) {
        std::printf("  buying %s: %s\n", id.c_str(), shop[id].name.c_str());
        g_sink("event shopbuy " + id);
    }
};

FakeGame g_game;
std::size_t g_dollarBillCount = 0;
int g_failures = 0;
std::vector<apclient::Notice> g_notices;

bool Announced(apclient::Notice::Kind kind, const std::string& item, const std::string& player) {
    return std::any_of(g_notices.begin(), g_notices.end(), [&](const apclient::Notice& n) {
        return n.kind == kind && n.item.find(item) != std::string::npos && n.player == player;
    });
}

void Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "[OK]  " : "[FAIL]", what);
    if (!ok) ++g_failures;
}

bool Pump(
    DWORD timeoutMs, const std::function<bool()>& done = [] { return false; }) {
    const DWORD end = GetTickCount() + timeoutMs;
    while (static_cast<int>(GetTickCount() - end) < 0) {
        apclient::Tick();
        g_game.Frame();
        if (g_queue.empty() && done()) return true;  // not in the middle of a batch of commands
        Sleep(16);
    }
    return false;
}

std::string ExeDir() {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string dir(path);
    return dir.substr(0, dir.find_last_of("\\/") + 1);
}

std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

bool Named(const Row& r) {
    return r.name != "Archipelago Item";
}

bool Ours(const Row& r) {  // item of our own world: no "(Player)" at the end
    return Named(r) && r.name.back() != ')';
}

std::string FindRow(const std::function<bool(const Row&)>& pred) {
    for (const auto& [id, r] : g_game.shop) {
        if (!r.sold && pred(r)) return id;
    }
    return {};
}

bool Sold(const std::string& id) {
    const auto it = g_game.shop.find(id);
    return it != g_game.shop.end() && it->second.sold;
}

bool Reconnect(apclient::Address addr) {
    apclient::Connect(addr, "");
    return Pump(10000, [] { return apclient::GetState() == apclient::State::Connected; });
}

int main(int argc, char** argv) {
    if (argc < 6) {
        std::printf(
            "usage: loop_test name@host:port machines.py server_log.txt deathlink_log.txt server_commands.txt\n");
        return 2;
    }
    LogFile() = stdout;
    DeleteFileA((ExeDir() + "BioShockAP.ini").c_str());
    DeleteFileA((ExeDir() + "BioShockAP_state.json").c_str());

    std::string machinesPy = ReadFile(argv[2]);
    const std::string dollarBillsPy = machinesPy;
    {
        std::string upgradesPy = argv[2];
        upgradesPy = upgradesPy.substr(0, upgradesPy.find_last_of("/\\") + 1) + "upgrades.py";
        machinesPy += ReadFile(upgradesPy);
    }
    const std::regex pathRe("[\"']([^\"']+\\.TheWorld\\.PersistentLevel\\.XVendingMachine_\\d+)[\"']");
    for (std::sregex_iterator it(machinesPy.begin(), machinesPy.end(), pathRe), end; it != end; ++it) {
        g_game.machines.push_back((*it)[1]);
    }
    g_dollarBillCount = static_cast<std::size_t>(std::distance(
        std::sregex_iterator(dollarBillsPy.begin(), dollarBillsPy.end(), pathRe), std::sregex_iterator()));
    std::printf("%zu Dollar Bills, %zu machines in all\n", g_dollarBillCount, g_game.machines.size());

    apclient::Address addr;
    Check(apclient::ParseAddress(argv[1], &addr), "address parsed");

    apclient::SetNotifier([](const apclient::Notice& n) {
        std::printf("  notice %d: %s / %s / %s\n", static_cast<int>(n.kind), n.item.c_str(), n.player.c_str(),
                    n.text.c_str());
        g_notices.push_back(n);
    });

    // A save already used with other seeds: their counters don't count here.
    g_game.dpv["ArchipelagoItemsGiven"] = 500;
    g_game.dpv["APGiven_00000000"] = 500;

    std::printf("\n== Connection ==\n");
    Check(Reconnect(addr), "connected");

    std::printf("\n== AP rows and locks ==\n");
    const bool rows = Pump(10000, [] {
        if (g_game.shop.empty()) return false;
        for (const auto& [id, r] : g_game.shop) {
            if (!Named(r)) return false;
        }
        return true;
    });
    Check(rows, "AP rows placed, all named (LocationScouts)");
    std::printf("  %zu rows, %zu locked patterns\n", g_game.shop.size(), g_game.locks.size());
    {
        std::set<std::string> dollarBills(g_game.machines.begin(), g_game.machines.begin() + g_dollarBillCount);
        const auto onDollarBills = std::count_if(g_game.shop.begin(), g_game.shop.end(), [&](const auto& kv) {
            return dollarBills.count(kv.second.machine) > 0;
        });
        Check(onDollarBills == 2 * static_cast<long long>(g_dollarBillCount),
              "2 rows per Dollar Bill (items_per_machine: 2)");
        Check(g_game.shop.size() - onDollarBills == 54, "1 row per upgrade machine (items_per_upgrade_machine: 1)");
    }
    Check(g_game.locks.size() >= 16, "the 16 base wares are locked");
    Check(g_game.tearsLocked, "Elizabeth's Tears locked until the item is received");
    Check(g_game.lockpickLocked, "Lockpicking locked until the item is received");
    Check(g_game.passLocks.count("monumentisland") && g_game.passLocks.count("finkton"),
          "area passes locked until their item is received");

    std::printf("\n== Silver Eagles received ==\n");
    {
        // Our own Silver Eagles are never in our shops (finalize_multiworld), so some come through
        // the server console.
        Check(
            FindRow([](const Row& r) { return Ours(r) && r.name.find("Silver Eagles") != std::string::npos; }).empty(),
            "none of our Silver Eagles in our shops");
        const int before = g_game.money;
        std::ofstream(argv[5], std::ios::app) << "/send Booker 25 Silver Eagles\n";
        Check(Pump(15000, [&] { return g_game.money > before; }), "money paid");
    }

    std::printf("\n== Buying an item for another player ==\n");
    const std::string foreign = FindRow([](const Row& r) { return Named(r) && !Ours(r); });
    Check(!foreign.empty(), "a row for another player exists");
    if (!foreign.empty()) {
        g_game.Buy(foreign);
        Check(Pump(5000, [] { return Announced(apclient::Notice::Kind::ItemSent, "", "Finder"); }),
              "notice: item sent to Finder");
    }

    std::printf("\n== Buying an unlock item (ours) ==\n");
    const std::string unlock = FindRow([](const Row& r) {
        return Ours(r) && r.name.find("Silver Eagles") == std::string::npos &&
               r.name.find("Infusion") == std::string::npos && r.name.rfind("Vigor: ", 0) != 0 &&
               r.name.rfind("Gear: ", 0) != 0 && r.name.rfind("Upgrade: ", 0) != 0 &&
               r.name.find(" Trap") == std::string::npos && r.name != "Elizabeth's Tears" && r.name != "Lockpicking" &&
               r.name.find(" Pass") == std::string::npos;
    });
    Check(!unlock.empty(), "an unlock row exists");
    if (!unlock.empty()) {
        const std::size_t before = g_game.locks.size();
        const std::string name = g_game.shop[unlock].name;
        g_game.Buy(unlock);
        Check(Pump(10000, [&] { return g_game.locks.size() < before; }), "a ware unlocked");
        Check(Pump(5000, [&] { return Sold(unlock); }), "row marked sold");
        Check(!Announced(apclient::Notice::Kind::ItemReceived, name, ""), "no notice for our own item");
    }

    std::printf("\n== Money during a load (no confirmation) ==\n");
    {
        const int before = g_game.money;
        g_game.loading = true;
        std::ofstream(argv[5], std::ios::app) << "/send Booker 10 Silver Eagles\n";
        Pump(6000);
        Check(g_game.money == before, "nothing paid during the load");
        g_game.loading = false;
        Check(Pump(10000, [&] { return g_game.money > before; }), "paid after the load");
        const int after = g_game.money;
        Pump(5000);
        Check(g_game.money == after, "paid once");
    }

    std::printf("\n== Checkpoint reloaded (given items lost) ==\n");
    {
        Pump(3000);
        const std::string variable = g_game.apgiveVariable;
        Check(variable.rfind("APGiven_", 0) == 0, "counter specific to the seed (APGiven_...)");
        int& counter = g_game.dpv[variable];
        const int given = counter;
        const int money = g_game.money;
        Check(given > 0, "counter of items given written in the save (apgive)");
        counter = 0;       // the player reloads a checkpoint from before all these grants...
        g_game.money = 0;  // ... and loses the money received since
        Check(Pump(10000, [&] { return counter == given; }), "items given again up to the same counter");
        Check(g_game.money == money, "the same money given again, no more, no less");
        Pump(5000);
        Check(counter == given && g_game.money == money, "nothing more afterwards");
    }

    std::printf("\n== Offline purchase ==\n");
    apclient::Disconnect();
    const std::string offline = FindRow([](const Row&) { return true; });
    g_game.Buy(offline);
    Pump(500);
    Check(ReadFile(ExeDir() + "BioShockAP_state.json").find(offline) != std::string::npos, "purchase kept on disk");
    const int moneyBefore = g_game.money;
    Check(Reconnect(addr), "reconnected");
    Pump(5000);
    Check(g_game.money == moneyBefore || Ours(g_game.shop[offline]), "no money paid twice on reconnection");
    const auto receivedCount = [] {
        return std::count_if(g_notices.begin(), g_notices.end(),
                             [](const apclient::Notice& n) { return n.kind == apclient::Notice::Kind::ItemReceived; });
    };
    const auto receivedBefore = receivedCount();

    // Forget the local purchase and reconnect: the row only shows sold if the server has the check.
    apclient::Disconnect();
    {
        const std::string path = ExeDir() + "BioShockAP_state.json";
        json state = json::parse(ReadFile(path));
        for (auto& [key, entry] : state.items()) entry.erase("checks");
        std::ofstream(path, std::ios::binary | std::ios::trunc) << state.dump();
    }
    g_game.shop.clear();
    Check(Reconnect(addr), "reconnected without the local purchase state");
    Check(Pump(10000, [&] { return Sold(offline); }), "the offline purchase is known by the server");
    Check(receivedCount() == receivedBefore, "no new notice of items already received on reconnection");
    for (const std::string& id : {foreign, unlock}) {
        if (!id.empty()) Check(Sold(id), "previous purchases stay sold");
    }

    std::printf("\n== Voxophones ==\n");
    {
        const std::string serverLog = argv[3];
        const auto serverKnows = [&](const char* location) {
            return ReadFile(serverLog).find(location) != std::string::npos;
        };
        g_sink("event voxophone jf_a_fink_solution");
        Check(Pump(5000, [&] { return serverKnows("Voxophone - Solution to Your Problems"); }),
              "Voxophone picked up: check received by the server");
        apclient::Disconnect();
        g_sink("event voxophone ZHC_Grace");
        Pump(500);
        Check(Reconnect(addr), "reconnected");
        Check(Pump(5000, [&] { return serverKnows("Voxophone - Undeserving"); }),
              "Voxophone picked up offline: sent on reconnection");
        g_game.voxophones = {{"PED_A_New_Hunt", 1}, {"LC_Love_The_Sinner", 1}};
        Pump(3000);
        Check(!serverKnows("Voxophone - A New Hunt"), "Voxophone not picked up yet: no check");
        g_game.voxophones["PED_A_New_Hunt"] = 4;
        Check(Pump(5000, [&] { return serverKnows("Voxophone - A New Hunt"); }),
              "Voxophone picked up in the level: check sent");
        Check(!serverKnows("Voxophone - Love the Sinner"), "the other Voxophone, not picked up, has no check");
    }

    std::printf("\n== Infusions ==\n");
    {
        const std::string serverLog = argv[3];
        const auto serverKnows = [&](const char* location) {
            return ReadFile(serverLog).find(location) != std::string::npos;
        };
        Check(g_game.apInfusions.size() == 24, "the 24 bottles are handed to the mod (infusionap)");
        Check(g_game.apInfusions.count("s_twn_lodge_int_loot.xlootcontainerkactor_70") == 1, "package.object id");
        g_sink("infusion S_TWN_Lodge_Int_Loot.XLootContainerKActor_70 4");  // hidden from the load
        Pump(3000);
        Check(!serverKnows("Infusion - The Fraternal Order of the Raven"), "bottle already in state 4: no check");
        g_sink("event infusion S_TWN_Lodge_Int_Loot.XLootContainerKActor_70");
        Check(Pump(5000, [&] { return serverKnows("Infusion - The Fraternal Order of the Raven"); }),
              "bottle picked up: check sent");
        const std::string upgrade =
            FindRow([](const Row& r) { return Ours(r) && r.name.find("Infusion") != std::string::npos; });
        if (upgrade.empty()) {
            std::printf("  (no Infusion of ours in the shop: step skipped)\n");
        } else {
            const std::size_t before = g_game.upgrades.size();
            g_game.Buy(upgrade);
            Check(Pump(5000, [] { return !g_game.pickToken.empty(); }),
                  "Infusion received: the player is asked to choose");
            Pump(3000);
            Check(g_game.upgrades.size() == before, "nothing given before the choice");
            // The pending Infusion doesn't hold back the items received after it.
            const std::string eagles =
                FindRow([](const Row& r) { return Ours(r) && r.name.find("Silver Eagles") != std::string::npos; });
            if (!eagles.empty()) {
                const int money = g_game.money;
                g_game.Buy(eagles);
                Check(Pump(10000, [&] { return g_game.money > money; }),
                      "pending Infusion: the next Silver Eagles are given");
            }
            const auto choose = [] {
                if (!g_game.pickToken.empty()) g_game.pickChoice = 1;
            };
            Check(Pump(10000,
                       [&] {
                           choose();
                           return g_game.upgrades.size() > before;
                       }),
                  "shield chosen: given to the game");
            Check(g_game.upgrades[before] == 329, "the right item (329, shield)");
            Pump(8000, [&] {
                choose();
                return false;
            });
            Pump(3000);  // a choice made just before is still given
            const std::size_t after = g_game.upgrades.size();
            Pump(5000);
            Check(g_game.upgrades.size() == after, "each Infusion given once");
        }
    }

    std::printf("\n== Vigors ==\n");
    {
        const std::string serverLog = argv[3];
        Check(!g_game.vigorLocks.empty() && g_game.vigorLocks.size() <= 6,
              "randomized Vigors not received yet are locked");
        Check(g_game.vigorLocks.size() + g_game.vigorsOwned.size() == 6,
              "every randomized Vigor is either locked or owned");
        const std::map<std::string, std::string> places = {
            {"devilskiss", "Vigor - Devil's Kiss (Raffle Square)"},
            {"murderofcrows", "Vigor - Murder of Crows (Comstock Center Rooftops)"},
            {"buckingbronco", "Vigor - Bucking Bronco (Hall of Heroes)"},
            {"charge", "Vigor - Charge (Finkton Proper)"},
            {"undertow", "Vigor - Undertow (The Factory)"},
            {"returntosender", "Vigor - Return to Sender (Downtown Emporia)"}};
        const std::string locked = g_game.vigorLocks.empty() ? "charge" : *g_game.vigorLocks.begin();
        g_sink("event vigor " + locked);
        Check(Pump(5000, [&] { return ReadFile(serverLog).find(places.at(locked)) != std::string::npos; }),
              "Vigor found in the game: check sent");
        const std::string vigor = FindRow([](const Row& r) { return Ours(r) && r.name.rfind("Vigor: ", 0) == 0; });
        if (vigor.empty()) {
            std::printf("  (no Vigor of ours in the shop: step skipped)\n");
        } else {
            const std::size_t locks = g_game.vigorLocks.size();
            const std::size_t given = g_game.onceGiven.size();
            g_game.Buy(vigor);
            Check(Pump(10000, [&] { return g_game.vigorLocks.size() == locks - 1; }), "Vigor received: unlocked");
            // Given outside the queue: an Infusion waiting for the player's choice doesn't hold it back.
            Check(Pump(10000, [&] { return g_game.onceGiven.size() > given; }),
                  "Vigor received: given to the game (giveonce)");
        }
    }

    std::printf("\n== Upgrades ==\n");
    {
        Check(g_game.locks.count("Loot_Upgrade_Pistol_ClipSize") &&
                  g_game.locks.count("Loot_StableTonic_DevilsKiss_Cluster"),
              "original upgrades locked in the machines");
        const std::string upgrade = FindRow([](const Row& r) { return Ours(r) && r.name.rfind("Upgrade: ", 0) == 0; });
        if (upgrade.empty()) {
            std::printf("  (no upgrade of ours in the shop: step skipped)\n");
        } else {
            // Not in memory in this level: waits without blocking the other grants.
            g_game.upgradesMissing = true;
            const std::size_t before = g_game.onceGiven.size();
            g_game.Buy(upgrade);
            Pump(5000);
            Check(g_game.onceGiven.size() == before, "upgrade not in memory: waiting");
            g_game.upgradesMissing = false;
            Check(Pump(10000, [&] { return g_game.onceGiven.size() > before; }), "upgrade received: given to the game");
            const int lookup = g_game.onceGiven.back();
            Pump(5000);
            Check(std::count(g_game.onceGiven.begin(), g_game.onceGiven.end(), lookup) == 1, "given once");
            // Checkpoint reloaded from before the grant: the mark goes away with the upgrade.
            for (auto& [name, value] : g_game.dpv) {
                if (name.size() > 4 && name.substr(name.rfind('_') + 1) == std::to_string(lookup)) value = 0;
            }
            Check(
                Pump(10000, [&] { return std::count(g_game.onceGiven.begin(), g_game.onceGiven.end(), lookup) == 2; }),
                "checkpoint reloaded: upgrade given again");
        }
    }

    std::printf("\n== Story checkpoints ==\n");
    {
        const std::string serverLog = argv[3];
        g_sink("event checkpoint BW_Vending");  // fine step: not a location with checkpoint_checks: chapters
        g_sink("event checkpoint TC_PreLottery");
        Check(Pump(5000, [&] { return ReadFile(serverLog).find("Chapter - Raffle Square") != std::string::npos; }),
              "chapter reached: check sent");
        Check(ReadFile(serverLog).find("Checkpoint - The Arcade") == std::string::npos,
              "fine step not in the seed: nothing");
    }

    std::printf("\n== Traps ==\n");
    {
        // Our own traps are never in our shops, so one comes through the server console. "Upgrade:
        // Crows Trap Aid" isn't a trap: traps end with "Trap".
        Check(FindRow([](const Row& r) {
                  return Ours(r) && r.name.size() > 5 && r.name.compare(r.name.size() - 5, 5, " Trap") == 0;
              }).empty(),
              "none of our traps in our shops");
        // Only Disarm counts: another trap of ours (found at a checkpoint) may arrive at the same time.
        const auto disarms = [] {
            return std::count_if(g_game.traps.begin(), g_game.traps.end(),
                                 [](const std::string& t) { return t.rfind("disarm", 0) == 0; });
        };
        const auto before = disarms();
        std::ofstream(argv[5], std::ios::app) << "/send Booker Disarm Trap\n";
        Check(Pump(15000, [&] { return disarms() > before; }), "trap received: triggered in the game");
        Pump(5000);
        Check(disarms() == before + 1, "trap triggered once");
    }

    std::printf("\n== Gear ==\n");
    {
        const std::string serverLog = argv[3];
        Check(g_game.apGear.size() == 32, "the 32 Gear packages are handed to the mod (gearap)");
        g_sink("event gear S_TWN_Aviary_Loot.XLootContainerKActor_1");
        Check(Pump(5000, [&] { return ReadFile(serverLog).find("Gear - Town Center 1") != std::string::npos; }),
              "Gear package picked up: check sent");
        const std::string piece = FindRow([](const Row& r) { return Ours(r) && r.name.rfind("Gear: ", 0) == 0; });
        if (piece.empty()) {
            std::printf("  (no Gear of ours in the shop: step skipped)\n");
        } else {
            const std::size_t before = g_game.gear.size();
            g_game.Buy(piece);
            // An Infusion received earlier waits for the player's choice and holds the next items back.
            const auto choose = [] {
                if (!g_game.pickToken.empty()) g_game.pickChoice = 0;
            };
            Check(Pump(15000,
                       [&] {
                           choose();
                           return g_game.gear.size() > before;
                       }),
                  "Gear piece received: given to the game");
            Check(g_game.gear.size() > before && (g_game.gear[before] >> 48) == 1, "the game's Gear number (64 bits)");
            Pump(5000, [&] {
                choose();
                return false;
            });
            const std::size_t after = g_game.gear.size();
            Pump(5000);
            Check(g_game.gear.size() == after, "each piece given once");
        }
    }

    std::printf("\n== DeathLink ==\n");
    const std::string deathLog = argv[4];
    const auto deathsSeen = [&] {
        const std::string text = ReadFile(deathLog);
        return static_cast<int>(std::count(text.begin(), text.end(), '\n'));
    };
    g_sink("event death");
    Check(Pump(10000, [&] { return deathsSeen() == 1; }), "death sent to the other players");
    Check(Pump(10000, [&] { return g_game.kills == 1; }), "another player's death received: Booker killed");
    Check(std::any_of(g_notices.begin(), g_notices.end(),
                      [](const apclient::Notice& n) {
                          return n.kind == apclient::Notice::Kind::DeathLink && n.player == "Finder" &&
                                 n.text == "Finder clicked a bomb.";
                      }),
          "notice: DeathLink death, with its cause");
    Pump(5000);
    Check(deathsSeen() == 1, "the death caused by DeathLink is not sent back");
    // Another player's death during a load: applied once Booker is in game.
    Pump(6000);  // end of the grace period after the received death
    g_game.loading = true;
    g_sink("event death");
    Check(Pump(10000, [&] { return deathsSeen() == 2; }), "new death of Booker sent");
    Pump(4000);
    Check(g_game.kills == 1, "no death applied during the load");
    g_game.loading = false;
    Check(Pump(10000, [&] { return g_game.kills == 2; }), "death received during the load applied afterwards");

    std::printf("\n== Goal: Siphon destroyed ==\n");
    const std::string serverLog = argv[3];
    const auto goalDone = [&] { return ReadFile(serverLog).find("has completed their goal") != std::string::npos; };
    for (std::string id; !(id = FindRow([](const Row&) { return true; })).empty();) {
        g_sink("event shopbuy " + id);
        g_game.shop[id].sold = true;  // avoids buying it again before the next sync
    }
    Pump(6000);
    Check(!goalDone(), "the whole shop bought: not the goal (goal: siphon)");
    // Songbird destroys the Siphon during an outage: the goal goes out on reconnection.
    apclient::Disconnect();
    Pump(500);
    g_sink("event goal siphon");
    Pump(1000);
    Check(!goalDone(), "offline: nothing sent");
    Check(Reconnect(addr), "reconnected");
    const std::size_t noticesBefore = g_notices.size();
    Check(Pump(15000, goalDone), "the server announces the goal");
    Pump(1500);  // a grant started before the goal (item received before) ends normally
    const auto givenBefore = std::make_tuple(g_game.money, g_game.upgrades.size(), g_game.gear.size());
    Check(Announced(apclient::Notice::Kind::Goal, "", ""), "notice: goal complete");
    // The server now sends all our remaining items (collect): nothing is given or announced
    // anymore.
    Pump(8000, [] {
        if (!g_game.pickToken.empty()) g_game.pickChoice = 0;
        return false;
    });
    Check(std::make_tuple(g_game.money, g_game.upgrades.size(), g_game.gear.size()) == givenBefore,
          "seed over: remaining items not given to the game");
    Check(std::none_of(g_notices.begin() + noticesBefore, g_notices.end(),
                       [](const apclient::Notice& n) {
                           return n.kind == apclient::Notice::Kind::ItemReceived ||
                                  n.kind == apclient::Notice::Kind::ItemFound ||
                                  n.kind == apclient::Notice::Kind::ItemSent;
                       }),
          "seed over: no item notices");

    apclient::Disconnect();
    std::printf("\n%s (%d failure(s))\n", g_failures ? "FAILED" : "ALL GOOD", g_failures);
    return g_failures ? 1 : 0;
}
