#pragma once

// Mod data in the game's save: the designers' player variables
// (XPlayerController.DesignerPlayerVariablesMap, name -> int). They are saved with the
// playthrough, so a value the mod writes rolls back with a reloaded checkpoint.
//
//   dpvget <name>          -> "dpv <name> <value>" (0 if the variable does not exist)
//   dpvset <name> <value>  -> "dpv <name> <value>" (the variable is created if needed)
//   dpvmissing <name>...   -> "dpvmissing <names at 0 or missing>" (items not given yet)
//   apgive <variable> <n> <command> -> (dllmain) runs the command; if it answers
//                             "called ...", <variable> = n. This is how the client counts the
//                             Archipelago items already given, in a variable of its own seed.
//
// SetDPV creates a missing variable; HasDPV / GetDPV read (RVAs in game_build.h).
//
// Game thread only.

#include <functional>
#include <string>

namespace bsap::savedata {

using Reply = std::function<void(const std::string&)>;

bool Get(const void* controller, const std::wstring& name, int* value);
bool Set(const void* controller, const std::wstring& name, int value);

bool Command(const std::string& cmd, const std::string& line, const void* controller, const Reply& reply);

}  // namespace bsap::savedata
