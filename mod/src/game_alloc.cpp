#include "game_alloc.h"

#include "engine_call.h"
#include "log.h"
#include "ue3.h"

#include <cstring>
#include <vector>

namespace bsap::alloc {

namespace {

// Object.Left(Str S @0x00, Int I @0x0C, Str ReturnValue @0x10)
struct LeftParms {
    FStringHeader s;
    std::int32_t i;
    FStringHeader returnValue;
};
static_assert(sizeof(LeftParms) == 0x1C, "layout of Object.Left's parameters");

}  // namespace

bool NewString(const void* context, const std::wstring& text, FStringHeader* out) {
    static const void* left = nullptr;
    if (!ue3::IsObject(left)) {
        left = ue3::FindFunction(context, "Left");
    }
    if (!left || text.empty()) {
        return false;
    }
    // The input stays ours: ProcessEvent does not destroy the caller's parameters.
    std::vector<wchar_t> input(text.begin(), text.end());
    input.push_back(0);
    LeftParms parms{};
    parms.s = {input.data(), static_cast<std::int32_t>(input.size()), static_cast<std::int32_t>(input.size())};
    parms.i = static_cast<std::int32_t>(text.size());
    if (!engine::CallRaw(context, left, &parms) || !parms.returnValue.data ||
        parms.returnValue.num != static_cast<std::int32_t>(input.size())) {
        Log("alloc: Object.Left failed (%d characters): data=%p num=%d max=%d", static_cast<int>(text.size()),
            static_cast<void*>(parms.returnValue.data), parms.returnValue.num, parms.returnValue.max);
        return false;
    }
    *out = parms.returnValue;
    return true;
}

void* NewBlock(const void* context, std::size_t bytes, std::size_t* capacity) {
    // 2(n+1) bytes with the terminating zero.
    const std::size_t chars = (bytes + 1) / 2;
    FStringHeader s{};
    if (!NewString(context, std::wstring(chars, L'x'), &s)) {
        return nullptr;
    }
    const std::size_t usable = static_cast<std::size_t>(s.max) * sizeof(wchar_t);
    std::memset(s.data, 0, usable);
    if (capacity) {
        *capacity = usable;
    }
    return s.data;
}

bool EnsureArrayCapacity(const void* context, const void* arrayAt, std::size_t elemSize, int extra) {
    ue3::ArrayHeader h{};
    if (!ue3::ReadBytes(arrayAt, &h, sizeof(h)) || h.num < 0 || h.num > h.max) {
        return false;
    }
    if (h.num + extra <= h.max) {
        return true;
    }
    std::size_t capacity = 0;
    void* block = NewBlock(context, (h.num + extra + 8) * elemSize, &capacity);
    if (!block) {
        return false;
    }
    std::vector<std::uint8_t> old(static_cast<std::size_t>(h.num) * elemSize);
    if (!old.empty() &&
        (!ue3::ReadBytes(h.data, old.data(), old.size()) || !ue3::WriteBytes(block, old.data(), old.size()))) {
        return false;
    }
    const ue3::ArrayHeader grown{static_cast<std::uint8_t*>(block), h.num,
                                 static_cast<std::int32_t>(capacity / elemSize)};
    Log("alloc: array grown %d -> %d elements", h.max, grown.max);
    return ue3::WriteBytes(const_cast<void*>(arrayAt), &grown, sizeof(grown));
}

}  // namespace bsap::alloc
