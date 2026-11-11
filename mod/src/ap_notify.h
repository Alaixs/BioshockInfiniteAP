#pragma once

// Archipelago messages during play (items received / sent, DeathLink, goal, connection), shown
// with the game's own tutorial messages, at the bottom center of the screen.
//
// The "Armor" tutorial (XAdaptiveTrainingMessage Messages.Armor) has no text in the game's
// translations and is never shown: we give it our text (intercepted Localize, UserInterface
// [TrainingMessages] Armor) and show it like the game does, with
// XPlayerController.ClientDisplayAdaptiveTrainingMessage. Texts are in the game's language.
//
// Messages wait until Booker is in game and alive (a DeathLink death is explained after the
// respawn) and are shown one after the other; several items received at once share a message.
//
// Game thread only.

#include "ap_client.h"

namespace bsap::apnotify {

void Push(const apclient::Notice& notice);
void Tick(const void* controller);

// A received Infusion waits for a choice (inventory): while true, the "choose: F9 = Health,
// F10 = Shield, F11 = Salts" message comes back, alternating with the other messages.
void SetInfusionPrompt(bool active);

// Shield picked for an Infusion before Booker has one: "no shield yet".
void ShowNoShield();

// Tear asked while Tears are locked (tears.cpp): "find the Elizabeth's Tears item".
void ShowTearsLocked();

// Area prompt used without its pass (areas.cpp): "you need the <pass>".
void ShowPassLocked(const wchar_t* pass);

// Paid lock used while Lockpicking is locked (lockpick.cpp): "find the Lockpicking item".
void ShowLockpickLocked();

}  // namespace bsap::apnotify
