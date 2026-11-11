#pragma once

// Archipelago login in the game's own UI: the 2K account screen (XTwoKAccountMenu) becomes it.
//   - The main menu's "2K ACCOUNT" button (Y) is renamed ARCHIPELAGO and goes straight there.
//   - Only a few Localize keys are replaced (title, "NAME@SERVER:PORT", DISCONNECT); the rest stays
//     in the game's language.
//   - LOG IN starts ap_client, with the game's modals for waiting, success and errors. The fields
//     are filled with the last successful login.
//   - A third button hint, "[X] AUTO-CONNECT", sets whether that login is used at startup.
// Nothing reaches 2K: the 2K code of these buttons is never called.
//
// Game thread only.

#include <string>

namespace bsap::apmenu {

// context: any valid object (allocations by the game).
void Tick(const void* context);

// ProcessEvent filter: the login screen's Y key toggles auto-connect. True = call blocked.
bool Intercept(void* self, const void* func);

// The game's UI language (INT, FRA, DEU, ITA, ESN), once detected (INT until then).
const char* LanguageCode();

// Replaces the UserInterface [section] key translation with `text` (empty: back to the game's).
void SetText(const std::wstring& section, const std::wstring& key, const std::wstring& text);

}  // namespace bsap::apmenu
