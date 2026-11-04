#pragma once

// Calling the game's (UnrealScript) functions by name, through UObject::ProcessEvent.
// The cleanest way to "give an item": the game itself handles the UI, sounds and saving.
// Game thread only.

#include <functional>
#include <string>
#include <vector>

namespace bsap::engine {

using Reply = std::function<void(const std::string&)>;

// Calls obj.funcName(args...) and replies "called Class.Function(Param=value, ...)".
// Supported argument types: Int, Qword, Float, Byte, Bool, Object ("#index" or "None").
void Call(const void* obj, const std::string& funcName, const std::vector<std::string>& args, const Reply& reply);

// Raw call: parms is the already filled parameter block (offsets from the UFunction).
bool CallRaw(const void* obj, const void* func, void* parms);

}  // namespace bsap::engine
