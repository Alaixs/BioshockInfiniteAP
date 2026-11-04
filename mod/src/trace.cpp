#include "trace.h"

#include "bridge.h"
#include "ue3.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <vector>

namespace bsap::trace {

namespace {

enum class Mode { Off, Baseline, New, Filter };
Mode g_mode = Mode::Off;
DWORD g_baselineEnd = 0;
std::string g_filter;  // lower case

// Everything below is only touched from the game thread: no lock.
std::unordered_map<const void*, bool> g_seen;
std::unordered_map<const void*, std::string> g_names;  // function -> "Class.Function"
std::vector<std::string> g_pending;
DWORD g_windowStart = 0;
int g_sentInWindow = 0;
int g_dropped = 0;

constexpr int kMaxLinesPerSecond = 40;

const std::string& NameOf(const void* func) {
    auto it = g_names.find(func);
    if (it == g_names.end()) {
        it = g_names.emplace(func, ue3::FullName(func)).first;
    }
    return it->second;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

void Emit(const void* self, const void* func) {
    const DWORD now = GetTickCount();
    if (now - g_windowStart >= 1000) {
        if (g_dropped) {
            g_pending.push_back("trace: " + std::to_string(g_dropped) + " lines dropped (rate)");
        }
        g_windowStart = now;
        g_sentInWindow = 0;
        g_dropped = 0;
    }
    if (++g_sentInWindow > kMaxLinesPerSecond) {
        ++g_dropped;
        return;
    }
    g_pending.push_back("trace " + NameOf(func) + "   self=" + ue3::ClassName(self) + "'" + ue3::FullName(self) + "'");
}

}  // namespace

void Observe(const void* self, const void* func) {
    switch (g_mode) {
        case Mode::Baseline:
            if (GetTickCount() - g_baselineEnd < 0x80000000u) {  // baseline over
                g_mode = Mode::New;
                g_pending.push_back("trace: baseline done (" + std::to_string(g_seen.size()) +
                                    " functions ignored). Go!");
            } else {
                g_seen[func] = true;
            }
            break;
        case Mode::New:
            if (g_seen.emplace(func, false).second) {
                Emit(self, func);
            }
            break;
        case Mode::Filter:
            if (Lower(NameOf(func)).find(g_filter) != std::string::npos) {
                Emit(self, func);
            }
            break;
        case Mode::Off:
            break;
    }
}

void Command(const std::string& arg) {
    if (arg == "off") {
        g_mode = Mode::Off;
        bridge::Send("trace: stopped");
    } else if (arg.empty()) {
        g_seen.clear();
        g_baselineEnd = GetTickCount() + 3000;
        g_mode = Mode::Baseline;
        bridge::Send("trace: 3 s baseline, don't do anything...");
    } else {
        g_filter = Lower(arg);
        g_mode = Mode::Filter;
        bridge::Send("trace: every call containing '" + arg + "' ('trace off' to stop)");
    }
}

void Flush() {
    for (const std::string& line : g_pending) {
        bridge::Send(line);
    }
    g_pending.clear();
}

}  // namespace bsap::trace
