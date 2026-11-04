#include "areas.h"

#include "ap_notify.h"
#include "log.h"
#include "ue3.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <unordered_map>

namespace bsap::areas {

namespace {

struct Gate {
    const char* key;
    const char* prompt;  // ue3::PathName of the prompt, lower case
    const wchar_t* pass;
};

const Gate kGates[] = {
    {"monumentisland", "s_twn_transition03.theworld.persistentlevel.main_sequence.xseqact_hudshowuseprompt_7",
     L"Monument Island Pass"},
    {"finkton", "s_bw_firstlady_nar.theworld.persistentlevel.main_sequence.xseqact_hudshowuseprompt_2",
     L"Finkton Pass"},
};

std::set<std::string> g_locked;

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

const Gate* FindGate(const std::string& key) {
    for (const Gate& g : kGates) {
        if (key == g.key) return &g;
    }
    return nullptr;
}

}  // namespace

bool Command(const std::string& cmd, const std::string& line, const Reply& reply) {
    if (cmd != "pass") return false;
    std::istringstream in(line);
    std::string word, action, key;
    in >> word >> action >> key;
    if (action.empty()) {
        std::string list;
        for (const std::string& k : g_locked) list += " " + k;
        reply("ok pass locked:" + (list.empty() ? std::string(" none") : list));
        return true;
    }
    if ((action != "lock" && action != "unlock") || !FindGate(key)) {
        reply("err usage: pass lock|unlock monumentisland|finkton");
        return true;
    }
    const bool lock = action == "lock";
    if (lock != (g_locked.count(key) > 0)) Log("areas: %s pass %s", key.c_str(), lock ? "locked" : "unlocked");
    if (lock)
        g_locked.insert(key);
    else
        g_locked.erase(key);
    reply("ok pass " + key + (lock ? " locked" : " unlocked"));
    return true;
}

bool Intercept(const void* self, const void* func) {
    if (g_locked.empty()) return false;
    static std::unordered_map<const void*, bool> isChoice;  // per UFunction
    auto it = isChoice.find(func);
    if (it == isChoice.end())
        it = isChoice.emplace(func, ue3::ObjectName(func) == "XActivateScriptedUsePromptChoice1").first;
    if (!it->second) return false;
    ue3::Property p;
    if (!ue3::FindProperty(self, "ScriptedUsePromptAction", &p)) return false;
    const std::string prompt = Lower(ue3::PathName(ue3::ObjectField(self, p)));
    for (const Gate& g : kGates) {
        if (prompt != g.prompt || !g_locked.count(g.key)) continue;
        static DWORD lastMessage = 0;
        const DWORD now = GetTickCount();
        if (!lastMessage || now - lastMessage >= 5000) {
            lastMessage = now;
            apnotify::ShowPassLocked(g.pass);
        }
        return true;
    }
    return false;
}

}  // namespace bsap::areas
