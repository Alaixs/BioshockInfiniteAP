#pragma once

// UnrealScript call tracer (fed by the ProcessEvent hook, pe_hook.cpp). Debug bridge command:
//
//   trace          -> 3 s of silent baseline, then prints each function NEVER seen during the
//                     baseline, the first time it appears. Answers "what does the game do when
//                     I buy an item?".
//   trace Filter   -> prints every call whose "Class.Function" contains Filter (rate limited).
//   trace off

#include <string>

namespace bsap::trace {

void Command(const std::string& arg);
// Called by the hook for each UnrealScript call (game thread).
void Observe(const void* self, const void* func);
// Called on every tick (game thread): flushes the queued lines.
void Flush();

}  // namespace bsap::trace
