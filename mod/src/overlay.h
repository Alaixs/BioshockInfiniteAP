#pragma once

// Debug overlay drawn in game (engine Canvas), over the picture. Only with debug=1 in
// BioShockAP.ini:
//   - a panel: Archipelago state, loaded map, position and heading, nearby targets, cheat keys;
//   - a colored frame around each check left within 60 m (Voxophones, Infusions, Gear, machines
//     with AP rows), seen through walls, with its distance; checks already done are not shown.
//     The selected target gets a thicker frame at any distance, and a marker at the screen edge
//     when it is off screen.
//
// Keys (when the game has focus):
//   F4  infinite health        F5  one-hit enemies
//   F6  show / hide            F7  next target (nearest first)    F8  teleport to the target
//   Ctrl+1..6  test traps      Ctrl+0  end traps
//
// Drawing goes through the ProcessEvent hook: the frames after XPlayerController.PostRender
// (rendering of the player's view, where the Canvas can project), the panel after
// XGameViewportClient.PostRender. Game thread only.

namespace bsap::overlay {

// Every frame (game thread): keys and refresh of the displayed data.
void Tick(const void* controller);

// Called by the ProcessEvent hook after each call: draws if it is one of the PostRender calls
// (their parameter is the frame's Canvas).
void AfterProcessEvent(const void* self, const void* func, void* parms);

}  // namespace bsap::overlay
