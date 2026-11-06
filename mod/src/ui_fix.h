#pragma once

// The vending machines' "sold" banner, untranslated by the game.
//
// The Dollar Bill Flash screen does `textID = "Sold!"`, a translation key no language has, so
// Scaleform shows it as is (the game never shows sold-out rows, so nobody noticed). The real text
// exists: [XVendingMachineRendererData] SoldOutString (FRA: "Vendu !").
//
// The movie is stored zlib-compressed ("CFX") in SwfMovie.RawData. We inflate it once, replace
// "Sold!" (fixing the SWF lengths around it) and store it uncompressed ("GFX", which Scaleform
// accepts) in memory from the game's allocator. Game thread only.

#include <functional>
#include <string>

namespace bsap::uifix {

// context: any valid object (for allocations by the game).
void Tick(const void* context);

// The installed language ("FRA", "INT"...) whose <file> gives `value` for section/key (section
// with brackets: L"[DNAGeneral]"). Empty if none.
std::string LanguageWithValue(const std::string& file, const std::wstring& section, const std::wstring& key,
                              const std::wstring& value);

// Debug command: "uifix off" restores the original Flash (to compare with the base game),
// "uifix on" fixes it again.
void Command(const std::string& arg, const std::function<void(const std::string&)>& reply);

}  // namespace bsap::uifix
