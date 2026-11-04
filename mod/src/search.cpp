#include "search.h"

#include "ue3.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <vector>

namespace bsap::search {

namespace {

// Per class, computed once per find: its numeric fields, and whether it is metadata to skip.
struct ClassInfo {
    std::string name;
    bool skip = false;
    std::vector<ue3::Property> numeric;
};

struct Candidate {
    int index;
    const void* obj;
    const ue3::Property* prop;  // points into g_classes, stable until it is cleared
};

std::unordered_map<const void*, ClassInfo> g_classes;
std::vector<Candidate> g_candidates;

constexpr std::size_t kMaxCandidates = 500000;
constexpr std::size_t kMaxListed = 40;

bool EndsWith(const std::string& s, const char* suffix) {
    const std::size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

const ClassInfo& Info(const void* cls) {
    auto it = g_classes.find(cls);
    if (it != g_classes.end()) {
        return it->second;
    }
    ClassInfo info;
    info.name = ue3::ObjectName(cls);
    info.skip = info.name == "Class" || info.name == "Function" || info.name == "State" ||
                info.name == "ScriptStruct" || info.name == "Enum" || info.name == "Const" || info.name == "Package" ||
                EndsWith(info.name, "Property");
    if (!info.skip) {
        ue3::ForEachClassProperty(cls, [&](const ue3::Property& p) {
            if (p.kind == "IntProperty" || p.kind == "FloatProperty" || p.kind == "ByteProperty") {
                info.numeric.push_back(p);
            }
        });
    }
    return g_classes.emplace(cls, std::move(info)).first->second;
}

bool IsArchetype(const void* obj) {
    return ue3::ObjectName(obj).rfind("Default__", 0) == 0;
}

std::string Describe(const Candidate& c) {
    double v = 0;
    ue3::ReadNumber(c.obj, *c.prop, &v);
    char value[64];
    std::snprintf(value, sizeof(value), "%g", v);
    return "cand #" + std::to_string(c.index) + "." + c.prop->name + " = " + value + "   [" + ue3::ClassName(c.obj) +
           " " + ue3::FullName(c.obj) + "]";
}

void Report(const Reply& reply) {
    reply("found " + std::to_string(g_candidates.size()) + " candidate(s)");
    if (g_candidates.size() <= kMaxListed) {
        for (const Candidate& c : g_candidates) {
            reply(Describe(c));
        }
    } else {
        reply("too many to list: change the value in game, then 'refine <new value>'");
    }
}

}  // namespace

void Find(double value, const Reply& reply) {
    g_candidates.clear();
    g_classes.clear();
    const int count = ue3::GlobalObjectCount();
    for (int i = 0; i < count && g_candidates.size() < kMaxCandidates; ++i) {
        const void* obj = ue3::GlobalObject(i);
        if (!obj) {
            continue;
        }
        const ClassInfo& info = Info(ue3::ClassOf(obj));
        if (info.skip) {
            continue;
        }
        bool checkedArchetype = false;
        for (const ue3::Property& p : info.numeric) {
            double v = 0;
            if (!ue3::ReadNumber(obj, p, &v) || v != value) {
                continue;
            }
            // The name is only read on a match: reading it for 90,000 objects is costly.
            if (!checkedArchetype) {
                checkedArchetype = true;
                if (IsArchetype(obj)) {
                    break;  // a class's default values, not the game state
                }
            }
            g_candidates.push_back({i, obj, &p});
        }
    }
    Report(reply);
}

void Refine(double value, const Reply& reply) {
    std::vector<Candidate> kept;
    for (const Candidate& c : g_candidates) {
        double v = 0;
        // The object may be gone (level load).
        if (ue3::GlobalObject(c.index) == c.obj && ue3::ReadNumber(c.obj, *c.prop, &v) && v == value) {
            kept.push_back(c);
        }
    }
    g_candidates.swap(kept);
    Report(reply);
}

void List(const Reply& reply) {
    const std::size_t n = std::min(g_candidates.size(), kMaxListed);
    for (std::size_t i = 0; i < n; ++i) {
        reply(Describe(g_candidates[i]));
    }
    reply("end cands " + std::to_string(g_candidates.size()));
}

void Objects(const std::string& classFilter, const Reply& reply) {
    std::string filter = classFilter;
    std::transform(filter.begin(), filter.end(), filter.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::unordered_map<const void*, std::string> lowerNames;
    int listed = 0, total = 0;
    const int count = ue3::GlobalObjectCount();
    for (int i = 0; i < count; ++i) {
        const void* obj = ue3::GlobalObject(i);
        if (!obj) {
            continue;
        }
        const void* cls = ue3::ClassOf(obj);
        auto it = lowerNames.find(cls);
        if (it == lowerNames.end()) {
            std::string name = ue3::ObjectName(cls);
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            it = lowerNames.emplace(cls, std::move(name)).first;
        }
        if (it->second.find(filter) == std::string::npos || IsArchetype(obj)) {
            continue;
        }
        ++total;
        if (listed < 60) {
            reply("obj #" + std::to_string(i) + " " + ue3::ClassName(obj) + " '" + ue3::FullName(obj) + "'");
            ++listed;
        }
    }
    reply("end objects " + std::to_string(total));
}

}  // namespace bsap::search
