// Entry point and command dispatch. The game loads our xinput1_3.dll (xinput_proxy.cpp); once the
// exe is checked against game_build.h, GetPlayerViewPoint is hooked so the game calls us on its own
// thread once per rendered frame, with the PlayerController at hand. That is the mod's tick: client
// and bridge commands run there and every module updates.

#include "ap_client.h"
#include "ap_menu.h"
#include "ap_notify.h"
#include "ap_texture.h"
#include "areas.h"
#include "bridge.h"
#include "collect.h"
#include "engine_call.h"
#include "game_build.h"
#include "inventory.h"
#include "lockpick.h"
#include "log.h"
#include "overlay.h"
#include "pe_hook.h"
#include "savedata.h"
#include "search.h"
#include "shop.h"
#include "tears.h"
#include "trace.h"
#include "traps.h"
#include "ue3.h"
#include "ui_fix.h"
#include "vigors.h"
#include "world.h"

#include "MinHook.h"

#include <intrin.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <sstream>
#include <string>
#include <vector>

namespace bsap {

namespace {

struct Vec3 {
    float x, y, z;
};

// __thiscall as __fastcall with a dummy edx, so MinHook can hook it.
using GetPlayerViewPoint_t = void(__fastcall*)(void* self, void* edx, Vec3* outLoc, void* outRot);
GetPlayerViewPoint_t g_original = nullptr;
void* g_sceneViewCallSite = nullptr;

// Values watched with "watch" (debug): an event is sent when they change.
struct Watch {
    std::string path;
    std::string last;
};
std::vector<Watch> g_watches;
unsigned g_frame = 0;
Vec3 g_lastPos{};
bool g_wasDead = false;

// "apgive" in progress: a confirmed grant ("called ...") moves the saved counter forward.
const void* g_apgiveController = nullptr;
std::wstring g_apgiveVariable;
int g_apgiveCounter = -1;

void Reply(const std::string& s) {
    if (g_apgiveCounter >= 0 && s.rfind("called ", 0) == 0) {
        savedata::Set(g_apgiveController, g_apgiveVariable, g_apgiveCounter);
        g_apgiveCounter = -1;
    }
    bridge::Send(s);
}

std::string Describe(const void* obj) {
    return obj ? ue3::ClassName(obj) + " '" + ue3::ObjectName(obj) + "'" : "None";
}

const void* PawnOf(const void* controller) {
    ue3::Property p;
    std::string err;
    const void* owner = ue3::ResolvePath(controller, "Pawn", &p, &err);
    return owner ? ue3::ObjectField(owner, p) : nullptr;
}

// An object given by a path: "" = the controller, "Pawn", "Pawn.InvManager", "#1234".
const void* ResolveObject(const void* controller, const std::string& path) {
    if (path.empty()) {
        return controller;
    }
    if (path[0] == '#' && path.find('.') == std::string::npos) {
        const void* obj = ue3::GlobalObject(std::atoi(path.c_str() + 1));
        if (!obj) {
            Reply("err object " + path + " not found");
        }
        return obj;
    }
    ue3::Property p;
    std::string err;
    const void* owner = ue3::ResolvePath(controller, path, &p, &err);
    if (!owner) {
        Reply("err " + err);
        return nullptr;
    }
    const void* obj = ue3::ObjectField(owner, p);
    if (!obj) {
        Reply("err " + path + " is not an object");
    }
    return obj;
}

// "call Function args" on the controller, "call Pawn.Function args", "call #12.Function".
void CmdCall(const void* controller, const std::string& line) {
    std::istringstream in(line);
    std::string cmd, target;
    in >> cmd >> target;
    std::vector<std::string> args;
    for (std::string a; in >> a;) {
        args.push_back(a);
    }
    const std::size_t dot = target.rfind('.');
    const std::string objPath = dot == std::string::npos ? "" : target.substr(0, dot);
    const std::string func = dot == std::string::npos ? target : target.substr(dot + 1);
    if (func.empty()) {
        Reply("err usage: call [Path.]Function args...");
        return;
    }
    if (const void* obj = ResolveObject(controller, objPath)) {
        engine::Call(obj, func, args, Reply);
    }
}

// "props" without argument: the controller. "props Pawn": the object Pawn points to.
void CmdProps(const void* controller, const std::string& path) {
    const void* target = ResolveObject(controller, path);
    if (!target) {
        return;
    }
    Reply("props " + Describe(target));
    int count = 0;
    ue3::ForEachProperty(target, [&](const ue3::Property& p) {
        char head[160];
        std::snprintf(head, sizeof(head), "prop +0x%04X %-18s %-24s ", p.offset, p.kind.c_str(), p.owner.c_str());
        Reply(head + p.name + " = " + ue3::ValueToString(target, p));
        ++count;
    });
    Reply("end props " + std::to_string(count));
}

// apgive <variable> <counter> <command>: if the game confirms the command, the save variable
// becomes <counter> in the same frame, so a reloaded checkpoint rolls both back together.
void Execute(const void* controller, const std::string& line);

void CmdApgive(const void* controller, const std::string& line, const std::string& variable,
               const std::string& counter) {
    std::size_t rest = 0;
    for (int word = 0; word < 3 && rest != std::string::npos; ++word) {
        rest = line.find_first_not_of(' ', rest);
        rest = rest == std::string::npos ? rest : line.find(' ', rest);
    }
    if (rest != std::string::npos) rest = line.find_first_not_of(' ', rest);
    if (counter.empty() || rest == std::string::npos) {
        Reply("err usage: apgive <variable> <counter> <command>");
    } else if (shop::GrantsBlocked()) {
        Reply("err grant deferred: vending screen open");  // done again on the next cycle
    } else {
        g_apgiveController = controller;
        g_apgiveVariable.assign(variable.begin(), variable.end());
        g_apgiveCounter = std::atoi(counter.c_str());
        Execute(controller, line.substr(rest));
        g_apgiveCounter = -1;
    }
}

void Execute(const void* controller, const std::string& line) {
    std::istringstream in(line);
    std::string cmd, arg1, arg2;
    in >> cmd >> arg1 >> arg2;

    if (cmd == "hello") {
        Reply(std::string("hello BioShockAP " BSAP_VERSION " build=") + build::kName);
        Reply("controller " + Describe(controller));
    } else if (cmd == "kill") {
        if (!PawnOf(controller)) {
            Reply("err no character to kill");
        } else {
            engine::Call(controller, "Suicide", {}, Reply);
        }
    } else if (cmd == "apgive") {
        CmdApgive(controller, line, arg1, arg2);
    } else if (cmd == "call") {
        CmdCall(controller, line);
    } else if (world::Command(cmd, line, controller, Reply)) {
        // where, poi, tp, tpto, bounds
    } else if (savedata::Command(cmd, line, controller, Reply)) {
        // dpvget, dpvset, dpvmissing
    } else if (traps::Command(cmd, line, controller, Reply)) {
        // trap
    } else if (tears::Command(cmd, arg1, Reply)) {
        // tears
    } else if (lockpick::Command(cmd, arg1, Reply)) {
        // lockpick
    } else if (areas::Command(cmd, line, Reply)) {
        // pass
    } else if (vigors::Command(cmd, arg1, controller, Reply)) {
        // vigorlock, vigorown, vigors
    } else if (inventory::Command(cmd, arg1, controller, Reply)) {
        // giveitem, giveonce, infusionpick, itemcheck
    } else if (collect::Command(cmd, arg1, Reply)) {
        // voxophones, infusions, infusionap, gears, gearap
    } else if (shop::Command(cmd, line, Reply)) {
        // vend, vendstock, vendremove, lock, unlock, locks, lockclear, shopslot, shopsold, shops, shopclear
    } else if (cmd == "props") {  // ---- debug inspection ----
        CmdProps(controller, arg1);
    } else if (cmd == "get" || cmd == "set" || cmd == "watch") {
        ue3::Property p;
        std::string err;
        const void* owner = ue3::ResolvePath(controller, arg1, &p, &err);
        if (!owner || (cmd == "set" && !ue3::WriteValue(owner, p, arg2, &err))) {
            Reply("err " + err);
            return;
        }
        const std::string value = ue3::ValueToString(owner, p);
        if (cmd == "watch") {
            g_watches.push_back({arg1, value});
        }
        Reply((cmd == "watch" ? "watching " : "val ") + arg1 + " = " + value);
    } else if (cmd == "unwatch") {
        g_watches.clear();
        Reply("ok");
    } else if (cmd == "funcs") {
        // "funcs Pawn Money": the object's functions whose name contains the filter.
        if (const void* obj = ResolveObject(controller, arg1 == "-" ? "" : arg1)) {
            ue3::ForEachFunction(obj, arg2, [](const std::string& sig) { Reply("func " + sig); });
            Reply("end funcs");
        }
    } else if (cmd == "objects") {
        if (arg1.empty()) {
            Reply("err usage: objects <part of a class name>");
        } else {
            search::Objects(arg1, Reply);
        }
    } else if (cmd == "find" || cmd == "refine") {
        char* end = nullptr;
        const double value = std::strtod(arg1.c_str(), &end);
        if (arg1.empty() || *end != '\0') {
            Reply("err usage: " + cmd + " <number>");
        } else if (cmd == "find") {
            search::Find(value, Reply);
        } else {
            search::Refine(value, Reply);
        }
    } else if (cmd == "cands") {
        search::List(Reply);
    } else if (cmd == "trace") {
        trace::Command(arg1);
    } else if (cmd == "uifix") {
        uifix::Command(arg1, Reply);
    } else {
        Reply("err unknown command: " + cmd);
    }
}

// Game -> client events. Not every frame: walking the reflection is costly.
void PollEvents(const void* controller) {
    const void* pawn = PawnOf(controller);
    // Booker's death (DeathLink): zero health or respawning. One event per death: he must be
    // alive again (new health, respawn over) before the next one.
    ue3::Property health, respawning;
    float healthValue = 1.0f;
    std::uint32_t respawnBits = 0;
    if (pawn && ue3::FindProperty(pawn, "XHealth", &health)) {
        ue3::ReadBytes(static_cast<const std::uint8_t*>(pawn) + health.offset, &healthValue, sizeof(healthValue));
    }
    if (ue3::FindProperty(controller, "bIsRespawning", &respawning)) {
        ue3::ReadBytes(static_cast<const std::uint8_t*>(controller) + respawning.offset, &respawnBits,
                       sizeof(respawnBits));
    }
    const bool isRespawning = respawning.boolMask && (respawnBits & respawning.boolMask);
    const bool dead = (pawn && healthValue <= 0.0f) || isRespawning;
    if (dead && !g_wasDead) Reply("event death");
    if (pawn || isRespawning) g_wasDead = dead;  // no character (menu, loading): wait
    for (Watch& w : g_watches) {
        ue3::Property p;
        std::string err;
        const void* owner = ue3::ResolvePath(controller, w.path, &p, &err);
        const std::string now = owner ? ue3::ValueToString(owner, p) : "<unavailable>";
        if (now != w.last) {
            Reply("event changed " + w.path + " " + w.last + " -> " + now);
            w.last = now;
        }
    }
}

void Tick(void* controller, const Vec3* loc) {
    if (loc) {
        g_lastPos = *loc;
    }
    if (!ue3::NamesReady() || !ue3::IsObject(controller)) {
        return;
    }
    bridge::Command command;
    for (int i = 0; i < 8 && bridge::PopCommand(&command); ++i) {
        bridge::SetCurrentClient(command.client);  // replies go to the sender
        Execute(controller, command.line);
        bridge::SetCurrentClient(-1);
    }
    pehook::EnsureInstalled();
    static bool notifierSet = false;
    if (!notifierSet) {
        apclient::SetNotifier(&apnotify::Push);
        notifierSet = true;
    }
    apclient::Tick();
    apmenu::Tick(controller);
    apnotify::Tick(controller);
    overlay::Tick(controller);
    collect::Tick(controller);
    inventory::Tick(controller);
    vigors::Tick(controller);
    traps::Tick(controller);
    trace::Flush();
    shop::Tick(controller, g_lastPos.x, g_lastPos.y, g_lastPos.z);
    if (++g_frame % 15 == 0) {
        PollEvents(controller);
    }
}

void __fastcall Detour(void* self, void* edx, Vec3* outLoc, void* outRot) {
    g_original(self, edx, outLoc, outRot);
    // The function has dozens of callers per frame; only the rendering one matters.
    if (_ReturnAddress() != g_sceneViewCallSite) {
        return;
    }
    // An exception must never unwind into the game: it would close it. Logged, at most a few times.
    static int errors = 0;
    try {
        Tick(self, outLoc);
    } catch (const std::exception& e) {
        if (errors++ < 20) Log("ERROR in the tick: %s", e.what());
    } catch (...) {
        if (errors++ < 20) Log("ERROR in the tick: unknown exception");
    }
}

bool BuildMatches(std::uintptr_t base) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    Log("BioShockInfinite.exe: TimeDateStamp 0x%08X, SizeOfImage 0x%08X", nt->FileHeader.TimeDateStamp,
        nt->OptionalHeader.SizeOfImage);
    return nt->FileHeader.TimeDateStamp == build::kTimeDateStamp &&
           nt->OptionalHeader.SizeOfImage == build::kSizeOfImage;
}

DWORD WINAPI InitThread(LPVOID) {
    Log("BioShockAP " BSAP_VERSION " started");
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("BioShockInfinite.exe"));
    if (!base) {
        Log("ERROR: BioShockInfinite.exe not found in the process");
        return 0;
    }
    if (!BuildMatches(base)) {
        Log("ERROR: unknown build (expected %s). The mod stays inactive.", build::kName);
        return 0;
    }

    void* target = reinterpret_cast<void*>(base + build::kRvaGetPlayerViewPoint);
    g_sceneViewCallSite = reinterpret_cast<void*>(base + build::kRvaSceneViewCallSite);
    if (MH_Initialize() != MH_OK ||
        MH_CreateHook(target, reinterpret_cast<void*>(&Detour), reinterpret_cast<void**>(&g_original)) != MH_OK ||
        MH_EnableHook(target) != MH_OK) {
        Log("ERROR: cannot hook GetPlayerViewPoint");
        return 0;
    }
    Log("Hooked GetPlayerViewPoint (%s)", build::kName);

    if (apclient::DebugEnabled()) {
        Log("Debug mode (debug=1 in BioShockAP.ini)");
        bridge::Start();
    }
    return 0;
}

}  // namespace

}  // namespace bsap

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);

        char path[MAX_PATH] = {};
        GetModuleFileNameA(module, path, MAX_PATH);
        std::string logPath(path);
        logPath = logPath.substr(0, logPath.find_last_of("\\/") + 1) + "BioShockAP.log";
        bsap::LogOpen(logPath.c_str());

        // Nothing heavy under the loader lock: the work happens on a separate thread.
        const HANDLE t = CreateThread(nullptr, 0, &bsap::InitThread, nullptr, 0, nullptr);
        if (t) {
            CloseHandle(t);
        }
    }
    return TRUE;
}
