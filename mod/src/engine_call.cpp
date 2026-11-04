#include "engine_call.h"

#include "game_build.h"
#include "log.h"
#include "ue3.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace bsap::engine {

bool CallRaw(const void* obj, const void* func, void* parms) {
    if (!ue3::IsObject(obj) || !ue3::IsObject(func)) {
        return false;
    }
    using ProcessEvent_t = void(__fastcall*)(const void* self, void* edx, const void* func, void* parms, void* result);
    void* const* vtable = *static_cast<void* const* const*>(obj);
    // ProcessEvent silently skips functions with a native number (Left = 128): zero it for the
    // call, like the usual UE3 SDKs.
    auto* iNative = reinterpret_cast<std::uint16_t*>(const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(func)) +
                                                     build::kFunctionNativeIndex);
    const std::uint16_t saved = *iNative;
    *iNative = 0;
    reinterpret_cast<ProcessEvent_t>(vtable[build::kProcessEventSlot])(obj, nullptr, func, parms, nullptr);
    *iNative = saved;
    return true;
}

void Call(const void* obj, const std::string& funcName, const std::vector<std::string>& args, const Reply& reply) {
    const void* func = ue3::FindFunction(obj, funcName);
    if (!func) {
        reply("err function not found: " + funcName + " (on " + ue3::ClassName(obj) + ")");
        return;
    }
    std::vector<ue3::Property> params;
    ue3::ForEachOwnProperty(func, [&](const ue3::Property& p) { params.push_back(p); });

    alignas(16) std::uint8_t parms[1024] = {};
    std::size_t next = 0;
    for (const ue3::Property& p : params) {
        if (p.offset + 8 > sizeof(parms)) {
            reply("err parameters too large");
            return;
        }
        if (p.name == "ReturnValue" || next >= args.size()) {
            continue;  // parameters not given stay zero
        }
        const std::string& a = args[next++];
        std::uint8_t* dst = parms + p.offset;
        if (p.kind == "IntProperty") {
            const auto v = static_cast<std::int32_t>(std::strtol(a.c_str(), nullptr, 0));
            std::memcpy(dst, &v, 4);
        } else if (p.kind == "QwordProperty") {
            const auto v = static_cast<std::int64_t>(std::strtoll(a.c_str(), nullptr, 0));
            std::memcpy(dst, &v, 8);
        } else if (p.kind == "FloatProperty") {
            const float v = std::strtof(a.c_str(), nullptr);
            std::memcpy(dst, &v, 4);
        } else if (p.kind == "ByteProperty") {
            *dst = static_cast<std::uint8_t>(std::strtoul(a.c_str(), nullptr, 0));
        } else if (p.kind == "BoolProperty") {
            const std::uint32_t v = (a == "0" || a == "false") ? 0 : p.boolMask;
            std::memcpy(dst, &v, 4);
        } else if (p.kind == "ObjectProperty" && (a == "None" || a[0] == '#')) {
            const void* v = a == "None" ? nullptr : ue3::GlobalObject(std::atoi(a.c_str() + 1));
            std::memcpy(dst, &v, 4);
        } else {
            reply("err unsupported parameter type: " + p.kind + " " + p.name);
            return;
        }
    }

    Log("call %s.%s", ue3::ClassName(obj).c_str(), funcName.c_str());
    CallRaw(obj, func, parms);

    std::string line = "called " + ue3::ClassName(obj) + "." + ue3::ObjectName(func) + "(";
    for (std::size_t i = 0; i < params.size(); ++i) {
        line += (i ? ", " : "") + params[i].name + "=" + ue3::ValueToString(parms, params[i]);
    }
    reply(line + ")");
}

}  // namespace bsap::engine
