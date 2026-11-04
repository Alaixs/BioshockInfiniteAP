#pragma once

// Minimal log: BioShockAP.log, next to the DLL (Binaries\Win32).
// Each line is flushed right away, so a crash does not lose it.

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace bsap {

inline std::FILE*& LogFile() {
    static std::FILE* f = nullptr;
    return f;
}

inline void LogOpen(const char* path) {
    LogFile() = std::fopen(path, "w");
}

inline void Log(const char* fmt, ...) {
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    std::FILE* f = LogFile();
    if (!f) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    std::vfprintf(f, fmt, args);
    va_end(args);
    std::fputc('\n', f);
    std::fflush(f);
}

}  // namespace bsap
