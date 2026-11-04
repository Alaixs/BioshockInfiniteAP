#pragma once

// Area passes: the prompts that take Booker to a new story area do nothing until the area's pass
// (an Archipelago item) is received.
//
// Leaving for another area is a scripted use prompt ("[E] Leave ..."): an XSeqAct_HUDShowUsePrompt
// with the area load input context. The use key calls XPlayerController.XActivateScriptedUsePromptChoice1;
// XPlayerController.ScriptedUsePromptAction is then the prompt shown. Only two prompts cross into
// a new area (the others stay in the same area; the remaining areas start with a cutscene):
//   monumentisland  S_TWN_Transition03 ... XSeqAct_HUDShowUsePrompt_7  (gondola to Monument Island)
//   finkton         S_BW_FirstLady_nar ... XSeqAct_HUDShowUsePrompt_2  (the First Lady to Finkton)
// While the pass is locked, the key is ignored and a message names the missing pass.
//
//   pass lock|unlock <key>  -> "ok pass <key> locked|unlocked" (all unlocked when the mod starts)
//   pass                    -> the locked passes
//
// Game thread only.

#include <functional>
#include <string>

namespace bsap::areas {

using Reply = std::function<void(const std::string&)>;

bool Command(const std::string& cmd, const std::string& line, const Reply& reply);

// Before a ProcessEvent: true if the call must be ignored (area prompt while its pass is locked).
bool Intercept(const void* self, const void* func);

}  // namespace bsap::areas
