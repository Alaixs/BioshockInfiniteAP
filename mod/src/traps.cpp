#include "traps.h"

#include "ap_texture.h"
#include "game_build.h"
#include "log.h"
#include "ue3.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>

namespace bsap::traps {

namespace {

enum Kind { kDisarm, kJam, kNoVigor, kLizStrike, kWalk, kScream, kCount };

struct TrapInfo {
    const char* name;
    const wchar_t* label;
};
constexpr TrapInfo kTraps[kCount] = {
    {"disarm", L"Disarmed"},       {"jam", L"Jammed weapons"},
    {"novigor", L"Vigors locked"}, {"lizstrike", L"Elizabeth on strike"},
    {"walk", L"Walk only"},        {"scream", L"Scream"},
};
constexpr int kDefaultSeconds = 30;
constexpr float kNoToss = -1000.0f;
const char* const kTossBonuses[] = {"WeaponTossChanceBonus", "VigorAmmoTossChanceBonus", "HealthTossChanceBonus",
                                    "CoinTossChanceBonus"};

DWORD g_until[kCount] = {};

// Original state, restored when the trap ends.
bool g_disarmApplied = false;
bool g_disarmSaved = false;
std::map<const void*, std::map<std::string, float>> g_lizSaved;
// Walk only: JumpZ 0, and a sprint threshold above the maximum input (1). Sky-Lines still work.
const std::pair<const char*, float> kWalkValues[] = {{"JumpZ", 0.0f}, {"SprintEnterInputThreshold", 5.0f}};
std::map<const void*, std::map<std::string, float>> g_walkSaved;

bool Active(Kind k) {
    return g_until[k] != 0 && static_cast<int>(g_until[k] - GetTickCount()) > 0;
}

template <typename T>
bool Field(const void* obj, const char* name, T* value, ue3::Property* prop = nullptr) {
    ue3::Property p;
    if (!ue3::FindProperty(obj, name, &p)) return false;
    if (prop) *prop = p;
    return ue3::ReadBytes(static_cast<const std::uint8_t*>(obj) + p.offset, value, sizeof(T));
}

template <typename T>
void SetField(const void* obj, const ue3::Property& p, T value) {
    ue3::WriteBytes(const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(obj)) + p.offset, &value, sizeof(T));
}

bool SetBool(const void* obj, const char* name, bool on, bool* before) {
    ue3::Property p;
    std::uint32_t bits = 0;
    if (!Field(obj, name, &bits, &p) || !p.boolMask) return false;
    if (before) *before = (bits & p.boolMask) != 0;
    bits = on ? (bits | p.boolMask) : (bits & ~p.boolMask);
    SetField(obj, p, bits);
    return true;
}

void ApplyDisarm(const void* controller, bool on) {
    if (on && !g_disarmApplied) {
        g_disarmApplied = SetBool(controller, "bWeaponInputDisabledByScript", true, &g_disarmSaved);
    } else if (on) {
        SetBool(controller, "bWeaponInputDisabledByScript", true, nullptr);  // a script may have reset it
    } else if (g_disarmApplied) {
        SetBool(controller, "bWeaponInputDisabledByScript", g_disarmSaved, nullptr);
        g_disarmApplied = false;
    }
}

void ApplyLizStrike(bool on) {
    static const void* cls = nullptr;
    if (!ue3::IsObject(cls)) cls = ue3::FindClass("XAIElizabethController");
    if (on) {
        ue3::ForEachObjectOfClass(cls, [](int, const void* liz) {
            if (ue3::ClassName(ue3::OuterOf(liz)) != "Level") return;
            auto& saved = g_lizSaved[liz];
            for (const char* name : kTossBonuses) {
                ue3::Property p;
                float value = 0;
                if (!Field(liz, name, &value, &p)) continue;
                saved.try_emplace(name, value);
                SetField(liz, p, kNoToss);
            }
        });
        return;
    }
    for (const auto& [liz, saved] : g_lizSaved) {
        if (!ue3::IsObject(liz) || ue3::ClassOf(liz) != cls) continue;  // level unloaded in the meantime
        for (const auto& [name, value] : saved) {
            ue3::Property p;
            if (ue3::FindProperty(liz, name, &p)) SetField(liz, p, value);
        }
    }
    g_lizSaved.clear();
}

void Override(const void* obj, const std::pair<const char*, float>* values, std::size_t count,
              std::map<const void*, std::map<std::string, float>>* saved) {
    auto& mine = (*saved)[obj];
    for (std::size_t i = 0; i < count; ++i) {
        ue3::Property p;
        float value = 0;
        if (!Field(obj, values[i].first, &value, &p)) continue;
        mine.try_emplace(values[i].first, value);
        SetField(obj, p, values[i].second);
    }
}

void Restore(std::map<const void*, std::map<std::string, float>>* saved) {
    for (const auto& [obj, values] : *saved) {
        if (!ue3::IsObject(obj)) continue;
        for (const auto& [name, value] : values) {
            ue3::Property p;
            if (ue3::FindProperty(obj, name, &p)) SetField(obj, p, value);
        }
    }
    saved->clear();
}

void ApplyWalk(const void* controller, bool on) {
    if (!on) {
        if (!g_walkSaved.empty()) Restore(&g_walkSaved);
        return;
    }
    ue3::Property p;
    std::string err;
    const void* owner = ue3::ResolvePath(controller, "Pawn", &p, &err);
    const void* pawn = owner ? ue3::ObjectField(owner, p) : nullptr;
    if (ue3::IsObject(pawn)) Override(pawn, kWalkValues, std::size(kWalkValues), &g_walkSaved);
}

// Boys of Silence scream. The Comstock House guards' scream (first sound of Silence_Scream_Turret)
// is only in their levels' Wwise banks: load the Atrium's, play it six times in phase (much louder)
// on a source that follows the listener, then unload the bank.
constexpr char kScreamBank[] = "S_DCOM_ATRIUM_GAME";
constexpr unsigned long kScreamEvents[] = {805312184u, 805312184u, 805312184u, 805312184u, 805312184u, 805312184u};
constexpr unsigned int kScreamObject = 0xB0510E5Eu;  // our sound source (any free id)
constexpr DWORD kScreamBankMs = 20000;

struct AkVector {
    float x, y, z;
};
struct AkListenerPosition {
    AkVector front, top, position;
};
struct AkSoundPosition {
    AkVector position, orientation;
};

std::uintptr_t Base() {
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
}

bool g_screamBankLoaded = false;
DWORD g_screamerUntil = 0;
constexpr DWORD kScreamerFullMs = 300;   // full picture...
constexpr DWORD kScreamerFadeMs = 1200;  // ...then fade
const void* g_screamerTexture = nullptr;
DWORD g_screamUnloadAt = 0;

void FollowListener() {
    using GetListener_t = int(__cdecl*)(unsigned long, AkListenerPosition*);
    using SetPosition_t = int(__cdecl*)(unsigned int, const AkSoundPosition*, unsigned long);
    AkListenerPosition listener{};
    if (reinterpret_cast<GetListener_t>(Base() + build::kRvaAkGetListenerPosition)(0, &listener) != 1) return;
    const AkSoundPosition at{listener.position, listener.front};
    reinterpret_cast<SetPosition_t>(Base() + build::kRvaAkSetPosition)(kScreamObject, &at, 0xFFFFFFFFu);
}

void PlaySounds(const unsigned long* events, std::size_t count) {
    using LoadBank_t = int(__cdecl*)(const char*, long, unsigned long*);
    using Register_t = int(__cdecl*)(unsigned int, const char*);
    using PostEvent_t = unsigned long(__cdecl*)(unsigned long, unsigned int, unsigned long, void*, void*, unsigned long,
                                                void*, unsigned long);
    if (!g_screamBankLoaded) {
        unsigned long bank = 0;
        const int loaded = reinterpret_cast<LoadBank_t>(Base() + build::kRvaAkLoadBank)(kScreamBank, -1, &bank);
        Log("traps: scream, LoadBank(%s) = %d (bank %lu)", kScreamBank, loaded, bank);
        g_screamBankLoaded = loaded == 1;  // AK_Success
    }
    static bool registered = false;
    if (!registered) {
        const int r =
            reinterpret_cast<Register_t>(Base() + build::kRvaAkRegisterGameObj)(kScreamObject, "BioShockAP scream");
        Log("traps: scream, RegisterGameObj = %d", r);
        registered = r == 1;
    }
    FollowListener();
    for (std::size_t i = 0; i < count; ++i) {
        const unsigned long event = events[i];
        const unsigned long playing = reinterpret_cast<PostEvent_t>(Base() + build::kRvaAkPostEvent)(
            event, kScreamObject, 0, nullptr, nullptr, 0, nullptr, 0);
        Log("traps: scream, PostEvent(%lu) = %lu", event, playing);
    }
    g_screamUnloadAt = GetTickCount() + kScreamBankMs;
}

void Scream() {
    PlaySounds(kScreamEvents, std::size(kScreamEvents));
    g_screamerUntil = (GetTickCount() + kScreamerFullMs + kScreamerFadeMs) | 1;
}

void TickScream() {
    if (!g_screamBankLoaded) return;
    if (static_cast<int>(GetTickCount() - g_screamUnloadAt) < 0) {
        FollowListener();
        return;
    }
    using UnloadBank_t = int(__cdecl*)(const char*, long*);
    const int r = reinterpret_cast<UnloadBank_t>(Base() + build::kRvaAkUnloadBank)(kScreamBank, nullptr);
    Log("traps: scream, UnloadBank = %d", r);
    g_screamBankLoaded = false;
}

}  // namespace

bool Command(const std::string& cmd, const std::string& line, const void* controller, const Reply& reply) {
    if (cmd != "trap") return false;
    std::istringstream in(line);
    std::string skip, type;
    int seconds = kDefaultSeconds;
    in >> skip >> type >> seconds;
    if (type == "stop") {
        for (DWORD& until : g_until) until = 0;
        Tick(controller);
        reply("ok trap stop");
        return true;
    }
    if (type == "sound") {  // debug: trap sound <Wwise event of the scream bank>
        std::istringstream ids(line);
        std::string a, b;
        unsigned long event = 0;
        ids >> a >> b >> event;
        PlaySounds(&event, 1);
        reply("ok trap sound " + std::to_string(event));
        return true;
    }
    if (type == "scream") {  // instant
        Scream();
        reply("ok trap scream");
        return true;
    }
    for (int k = 0; k < kCount; ++k) {
        if (type != kTraps[k].name) continue;
        if (seconds <= 0) seconds = kDefaultSeconds;
        const DWORD now = GetTickCount();
        const DWORD start = Active(static_cast<Kind>(k)) ? g_until[k] : now;
        g_until[k] = start + static_cast<DWORD>(seconds) * 1000u;
        if (g_until[k] == 0) g_until[k] = 1;
        Log("traps: %s for %d s", kTraps[k].name, seconds);
        reply("ok trap " + type + " " + std::to_string(seconds));
        return true;
    }
    reply("err usage: trap disarm|jam|novigor|lizstrike|walk [seconds] | trap scream | trap stop");
    return true;
}

void Tick(const void* controller) {
    TickScream();
    // Flash texture prepared in advance (its GPU resource takes a moment to be ready).
    if (!ue3::IsObject(g_screamerTexture) && ue3::IsObject(controller))
        g_screamerTexture = aptexture::ScreamerTexture(controller);
    for (int k = 0; k < kCount; ++k) {
        if (g_until[k] != 0 && !Active(static_cast<Kind>(k))) {
            g_until[k] = 0;
            Log("traps: end of %s", kTraps[k].name);
        }
    }
    if (ue3::IsObject(controller)) {
        ApplyDisarm(controller, Active(kDisarm));
        ApplyWalk(controller, Active(kWalk));
    }
    static DWORD nextLiz = 0;
    if (Active(kLizStrike) && static_cast<int>(GetTickCount() - nextLiz) >= 0) {
        nextLiz = GetTickCount() + 1000;  // an Elizabeth loaded after the start is caught too
        ApplyLizStrike(true);
    } else if (!Active(kLizStrike) && !g_lizSaved.empty()) {
        ApplyLizStrike(false);
    }
}

bool Intercept(const void* self, const void* func) {
    (void)self;
    if (!Active(kJam) && !Active(kNoVigor) && !Active(kLizStrike)) return false;
    static std::unordered_map<const void*, int> kinds;  // function -> trap blocking it (-1: none)
    auto it = kinds.find(func);
    if (it == kinds.end()) {
        const std::string name = ue3::ObjectName(func);
        it = kinds
                 .emplace(func, name == "StartFireWeapon"              ? kJam
                                : name == "StartFirePlasmid"           ? kNoVigor
                                : name == "XPerformSubstantiateObject" ? kLizStrike  // Tears too
                                                                       : -1)
                 .first;
    }
    return it->second >= 0 && Active(static_cast<Kind>(it->second));
}

const void* ScreamerImage(float* opacity) {
    const int left = static_cast<int>(g_screamerUntil - GetTickCount());
    if (!g_screamerUntil || left <= 0 || !ue3::IsObject(g_screamerTexture)) return nullptr;
    *opacity = left >= static_cast<int>(kScreamerFadeMs) ? 1.0f : static_cast<float>(left) / kScreamerFadeMs;
    return g_screamerTexture;
}

std::wstring ActiveSummary() {
    std::wstring out;
    for (int k = 0; k < kCount; ++k) {
        if (!Active(static_cast<Kind>(k))) continue;
        const int left = static_cast<int>((g_until[k] - GetTickCount() + 999) / 1000);
        if (!out.empty()) out += L", ";
        out += std::wstring(kTraps[k].label) + L" " + std::to_wstring(left) + L" s";
    }
    return out;
}

}  // namespace bsap::traps
