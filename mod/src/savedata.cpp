#include "savedata.h"

#include "game_build.h"
#include "ue3.h"

#include <windows.h>

#include <cstdint>
#include <sstream>

namespace bsap::savedata {

namespace {

struct FName {
    std::int32_t index;
    std::int32_t number;
};

std::uintptr_t Base() {
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
}

// FName(name, FNAME_Add): the name is added to the game's table if missing.
FName MakeName(const std::wstring& name) {
    using Ctor_t = FName*(__fastcall*)(FName * self, void* edx, const wchar_t* text, int findType, int split);
    FName out{};
    reinterpret_cast<Ctor_t>(Base() + build::kRvaFNameConstruct)(&out, nullptr, name.c_str(), 1, 1);
    return out;
}

bool IsPlayerController(const void* controller) {
    return ue3::IsObject(controller) && ue3::ClassName(controller) == "XPlayerController";
}

}  // namespace

bool Get(const void* controller, const std::wstring& name, int* value) {
    if (!IsPlayerController(controller)) return false;
    const FName key = MakeName(name);
    using Has_t = int(__stdcall*)(const void* pc, const FName* name);
    using Get_t = int(__stdcall*)(const void* pc, const FName* name);
    if (!reinterpret_cast<Has_t>(Base() + build::kRvaHasDPV)(controller, &key)) {
        *value = 0;
        return true;
    }
    *value = reinterpret_cast<Get_t>(Base() + build::kRvaGetDPV)(controller, &key);
    return true;
}

bool Set(const void* controller, const std::wstring& name, int value) {
    if (!IsPlayerController(controller)) return false;
    const FName key = MakeName(name);
    using Set_t = void(__stdcall*)(const void* pc, const FName* name, int value);
    reinterpret_cast<Set_t>(Base() + build::kRvaSetDPV)(controller, &key, value);
    return true;
}

bool Command(const std::string& cmd, const std::string& line, const void* controller, const Reply& reply) {
    if (cmd == "dpvmissing") {
        std::istringstream in(line);
        std::string skip, name, missing;
        in >> skip;
        while (in >> name) {
            int value = 0;
            if (!Get(controller, std::wstring(name.begin(), name.end()), &value)) {
                reply("err no XPlayerController");
                return true;
            }
            if (value <= 0) missing += " " + name;
        }
        reply("dpvmissing" + missing);
        return true;
    }
    if (cmd != "dpvget" && cmd != "dpvset") return false;
    std::istringstream in(line);
    std::string skip, name;
    int value = 0;
    in >> skip >> name;
    if (name.empty() || (cmd == "dpvset" && !(in >> value))) {
        reply("err usage: dpvget <name> | dpvset <name> <value>");
        return true;
    }
    const std::wstring wide(name.begin(), name.end());
    if ((cmd == "dpvset" && !Set(controller, wide, value)) || !Get(controller, wide, &value)) {
        reply("err no XPlayerController");
        return true;
    }
    reply("dpv " + name + " " + std::to_string(value));
    return true;
}

}  // namespace bsap::savedata
