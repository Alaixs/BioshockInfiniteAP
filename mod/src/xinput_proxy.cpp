// xinput1_3.dll proxy: how the mod gets loaded. BioShockInfinite.exe imports XINPUT1_3.dll and
// Windows looks in the exe's folder first. Every function is forwarded to the system DLL so
// controllers keep working. Adapted from bioshock-infinite-headtracking (itsloopyo, MIT).

#include <windows.h>

#include <mutex>
#include <string>

namespace {

HMODULE g_real = nullptr;
std::once_flag g_loadOnce;

void LoadRealXInput() {
    char sysDir[MAX_PATH] = {};
    const UINT n = GetSystemDirectoryA(sysDir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return;
    }
    std::string path(sysDir, n);
    path += "\\xinput1_3.dll";
    g_real = LoadLibraryA(path.c_str());
}

// Resolved on first call, not in DllMain: LoadLibrary under the loader lock can hang the game.
FARPROC Resolve(const char* nameOrOrdinal) {
    std::call_once(g_loadOnce, LoadRealXInput);
    return g_real ? GetProcAddress(g_real, nameOrOrdinal) : nullptr;
}

constexpr DWORD kNotConnected = 1167;  // ERROR_DEVICE_NOT_CONNECTED

}  // namespace

extern "C" {

DWORD WINAPI XInputGetState(DWORD userIndex, void* state) {
    using Fn = DWORD(WINAPI*)(DWORD, void*);
    static const auto fn = reinterpret_cast<Fn>(Resolve("XInputGetState"));
    return fn ? fn(userIndex, state) : kNotConnected;
}

DWORD WINAPI XInputSetState(DWORD userIndex, void* vibration) {
    using Fn = DWORD(WINAPI*)(DWORD, void*);
    static const auto fn = reinterpret_cast<Fn>(Resolve("XInputSetState"));
    return fn ? fn(userIndex, vibration) : kNotConnected;
}

DWORD WINAPI XInputGetCapabilities(DWORD userIndex, DWORD flags, void* capabilities) {
    using Fn = DWORD(WINAPI*)(DWORD, DWORD, void*);
    static const auto fn = reinterpret_cast<Fn>(Resolve("XInputGetCapabilities"));
    return fn ? fn(userIndex, flags, capabilities) : kNotConnected;
}

void WINAPI XInputEnable(BOOL enable) {
    using Fn = void(WINAPI*)(BOOL);
    static const auto fn = reinterpret_cast<Fn>(Resolve("XInputEnable"));
    if (fn) {
        fn(enable);
    }
}

DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD userIndex, void* renderGuid, void* captureGuid) {
    using Fn = DWORD(WINAPI*)(DWORD, void*, void*);
    static const auto fn = reinterpret_cast<Fn>(Resolve("XInputGetDSoundAudioDeviceGuids"));
    return fn ? fn(userIndex, renderGuid, captureGuid) : kNotConnected;
}

DWORD WINAPI XInputGetBatteryInformation(DWORD userIndex, BYTE devType, void* battery) {
    using Fn = DWORD(WINAPI*)(DWORD, BYTE, void*);
    static const auto fn = reinterpret_cast<Fn>(Resolve("XInputGetBatteryInformation"));
    return fn ? fn(userIndex, devType, battery) : kNotConnected;
}

DWORD WINAPI XInputGetKeystroke(DWORD userIndex, DWORD reserved, void* keystroke) {
    using Fn = DWORD(WINAPI*)(DWORD, DWORD, void*);
    static const auto fn = reinterpret_cast<Fn>(Resolve("XInputGetKeystroke"));
    return fn ? fn(userIndex, reserved, keystroke) : kNotConnected;
}

// Ordinals 100-103: exported without a name by the system DLL, so resolved by number.
DWORD WINAPI XInputGetStateEx(DWORD userIndex, void* state) {
    using Fn = DWORD(WINAPI*)(DWORD, void*);
    static const auto fn = reinterpret_cast<Fn>(Resolve(MAKEINTRESOURCEA(100)));
    return fn ? fn(userIndex, state) : kNotConnected;
}

DWORD WINAPI XInputWaitForGuideButton(DWORD userIndex, DWORD flags, void* buffer) {
    using Fn = DWORD(WINAPI*)(DWORD, DWORD, void*);
    static const auto fn = reinterpret_cast<Fn>(Resolve(MAKEINTRESOURCEA(101)));
    return fn ? fn(userIndex, flags, buffer) : kNotConnected;
}

DWORD WINAPI XInputCancelGuideButtonWait(DWORD userIndex) {
    using Fn = DWORD(WINAPI*)(DWORD);
    static const auto fn = reinterpret_cast<Fn>(Resolve(MAKEINTRESOURCEA(102)));
    return fn ? fn(userIndex) : kNotConnected;
}

DWORD WINAPI XInputPowerOffController(DWORD userIndex) {
    using Fn = DWORD(WINAPI*)(DWORD);
    static const auto fn = reinterpret_cast<Fn>(Resolve(MAKEINTRESOURCEA(103)));
    return fn ? fn(userIndex) : kNotConnected;
}

}  // extern "C"
