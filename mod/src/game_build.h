#pragma once

// Steam BioShockInfinite.exe, build of 2022-05-11. ImageBase 0x400000 without ASLR, so an RVA is
// the file address minus 0x400000. The first values come from bioshock-infinite-headtracking
// (itsloopyo, MIT). If Steam updates the game, the fingerprint stops matching and the mod stays
// off.

#include <cstddef>
#include <cstdint>

namespace bsap::build {

constexpr const char* kName = "steam-win32-20220511";

// PE fingerprint of the exe (NT header).
constexpr std::uint32_t kTimeDateStamp = 0x627BE455u;
constexpr std::uint32_t kSizeOfImage = 0x0124F000u;

// APlayerController::GetPlayerViewPoint(FVector& out_Location, FRotator& out_Rotation)
// __thiscall, called many times per frame on the game thread.
constexpr std::uintptr_t kRvaGetPlayerViewPoint = 0x1E1350u;
// Return address of its call from CalcSceneView: once per rendered frame, our "tick".
constexpr std::uintptr_t kRvaSceneViewCallSite = 0x26351Cu;

// UE3 object model.
constexpr std::uintptr_t kRvaGNamesData = 0xF9DFECu;  // FNameEntry** (TArray.Data)
constexpr std::uintptr_t kRvaGNamesNum = 0xF9DFF0u;   // int32 (TArray.Num)
constexpr std::size_t kNameEntryPacked = 0x08u;       // (index << 1) | isWide
constexpr std::size_t kNameEntryText = 0x10u;
// UObject** table. Found by scanning the running game for a TArray whose elements each hold their
// own index at +0x04.
constexpr std::uintptr_t kRvaGObjectsData = 0xF8C15Cu;
constexpr std::uintptr_t kRvaGObjectsNum = 0xF8C160u;
constexpr std::size_t kObjectIndex = 0x04u;

// ProcessEvent(UFunction*, void* Parms, void* Result) vtable slot. "pescan" gave 31 or 33; in a
// disassembler slot 31 ends with `ret 0xC` and the actor version (0x59A150) calls 0x4CFE70, slot 31
// of a plain UObject.
constexpr int kProcessEventSlot = 31;
// UObject::ProcessEvent itself. AActor's ends up here, so hooking it sees every UnrealScript call.
constexpr std::uintptr_t kRvaUObjectProcessEvent = 0xCFE70u;

// Plays an actor's XEffectContainer (sound, visuals...). __thiscall(actor), 12 arguments: the
// effect then 11 zeros. Found in the vending code, which calls it with the machine's
// PurchaseSuccessEffect (+0x1408) and PurchaseFailEffect (+0x1420).
constexpr std::uintptr_t kRvaPlayEffectContainer = 0x3EE620u;

// XVendingMachine::HasStock(int index): AvailableItems[index].AvailableStacksRemaining is -1 or
// > 0. __thiscall, ret 4. Called by:
//   - the filter building the list when the screen opens (row missing if false)  ret 0x74B04D
//   - the creation of a screen row ("sold" if false)                               ret 0x766E20
//   - the update of a screen row ("sold" if false)                                 ret 0x769C97
// The mod answers false to the last two for a bought AP row, so the game itself shows it as sold.
constexpr std::uintptr_t kRvaVendingHasStock = 0x7CFF90u;
constexpr std::uintptr_t kRvaHasStockFromRowInit = 0x766E20u;
constexpr std::uintptr_t kRvaHasStockFromRowUpdate = 0x769C97u;
// XVendingMachine: is the row valid for the player? int __thiscall(controller, index), ret 8,
// called before HasStock at the same three places. False for gear the player already owns.
constexpr std::uintptr_t kRvaVendingItemAvailable = 0x7D13A0u;

// UFunction: FunctionFlags @0x70, iNative (WORD) @0x74, ParmsSize @0x82, Func @0x8C.
// Found by comparing Object.Left (static native 128) and IsInPauseScreenFlow (native, 0).
constexpr std::size_t kFunctionNativeIndex = 0x74u;

// XReactiveActor: empties the container's LootList (state 4), then hides it and fires its Kismet
// events, as the Infusion screen does once the item is given. __thiscall, no argument.
constexpr std::uintptr_t kRvaReactiveActorSetLooted = 0x7BB0C0u;
// XPlayerController: opens a container's search screen, one function per screen type (archetype
// +0x1818, +0x1808, +0x180C). __thiscall(Container), ret 4: they keep the container (+0x1820) and
// create the screen. The first is ClientOpenContainerSearchUI; the other two are called by the
// native search code (0xBC9300 / 0xBC9313), including the Infusion choice screen.
constexpr std::uintptr_t kRvaOpenContainerSearchUI[] = {0x582290u, 0x582490u, 0x586150u};
constexpr std::size_t kControllerSearchedContainer = 0x1820u;  // XPlayerController: container of the open screen

// Infusion choice screen (XUnstableSelectionScreen), native:
//   - the searched container holds the 3 choices (XLootDropItemDefinition*, ItemLookupID @0x80)
//     at +0x740;
//   - Accept (vtable +0x188, __thiscall, returns 1 if accepted) gives the chosen item then marks
//     the container looted; the choice comes from the widget named by the global FName
//     @0x1404110: FindWidget(screen, &name), IsValid(widget), SelectedIndex(widget);
//   - OwnerController(screen): the screen's XPlayerController.
constexpr std::size_t kContainerChoices = 0x740u;

// Designer player variables (XPlayerController.DesignerPlayerVariablesMap @0x1AC4), saved with
// the playthrough. __stdcall(PC, const FName*, ...):
//   SetDPV(pc, name, value) creates the variable if needed; HasDPV(pc, name); GetDPV(pc, name).
// FName(const TCHAR*, EFindName, bool): __thiscall, ret 0xC (FNAME_Add = 1).
constexpr std::uintptr_t kRvaSetDPV = 0x5293A0u;
constexpr std::uintptr_t kRvaHasDPV = 0x527DA0u;
constexpr std::uintptr_t kRvaGetDPV = 0x529440u;
constexpr std::uintptr_t kRvaFNameConstruct = 0xC5E40u;
constexpr std::uintptr_t kRvaUnstableAccept = 0x748980u;
constexpr std::uintptr_t kRvaScreenOwnerController = 0x7278E0u;
constexpr std::uintptr_t kRvaScreenFindWidget = 0x727E10u;
constexpr std::uintptr_t kRvaSelectionWidgetName = 0x1004110u;
constexpr std::uintptr_t kRvaWidgetIsValid = 0x75D030u;
constexpr std::uintptr_t kRvaWidgetSelectedIndex = 0x75D020u;

// Wwise audio engine (AK::SoundEngine), exported by the exe (decorated names in the export table).
constexpr std::uintptr_t kRvaAkLoadBank = 0xB71270u;         // LoadBank(const char*, AkMemPoolId, AkBankID&)
constexpr std::uintptr_t kRvaAkUnloadBank = 0xB713E0u;       // UnloadBank(const char*, AkMemPoolId*)
constexpr std::uintptr_t kRvaAkRegisterGameObj = 0xB70060u;  // RegisterGameObj(AkGameObjectID, const char*)
constexpr std::uintptr_t kRvaAkGetListenerPosition =
    0xB6EB70u;  // Query::GetListenerPosition(index, AkListenerPosition&)
constexpr std::uintptr_t kRvaAkSetPosition =
    0xB70D50u;  // SetPosition(AkGameObjectID, const AkSoundPosition&, listener)
constexpr std::uintptr_t kRvaAkPostEvent =
    0xB73460u;  // PostEvent(AkUniqueID, AkGameObjectID, flags, cb, cookie, n, ext, playingID)

// Game object of an ItemLookupID, as the inventory code looks it up (__thiscall on the global
// manager, 64-bit id, ret 8). nullptr when not loaded: giving it then crashes the game.
constexpr std::uintptr_t kRvaItemResolve = 0x80BF10u;
constexpr std::uintptr_t kRvaItemManager = 0x1006B48u;

// Sizes of the vending machines' UnrealScript structs (read from the reflection).
constexpr std::size_t kSizeofVendingMachineItem = 0x0Cu;  // LootItem, ItemCost, ItemsAvailable
constexpr std::size_t kSizeofAvailableItemInfo = 0x38u;   // ..., AvailableStacksRemaining @0x34

// XItemDatabase.ItemLookupTable: one XItemCompositeData per item (599), indexed by ItemLookupID.
// Vending machines copy name, description and price from it whenever their screen opens.
constexpr std::size_t kSizeofItemCompositeData = 0x5Cu;
constexpr std::size_t kCompositeItemClass = 0x04u;          // UClass*
constexpr std::size_t kCompositeItemName = 0x10u;           // FString
constexpr std::size_t kCompositeItemDescription = 0x28u;    // FString
constexpr std::size_t kCompositeItemValue = 0x34u;          // int32, base price
constexpr std::size_t kCompositeAtlassedIconIndex = 0x38u;  // int32: index in XItemDatabase.AtlassedIcons

// XItemDatabase.AtlassedIcons: { AtlasTexture (Texture2D*), Left, Top, Width, Height }.
// The UI shows "img://" + texture, cut to this rectangle.
constexpr std::size_t kSizeofAtlassedTexture2D = 0x14u;

// Textures created at runtime (Texture2D.Create):
//   FUntypedBulkData::Lock(flags) -> void* and Unlock(): __thiscall on the mip level
//   (FTexture2DMipMap, data at the start of the struct), as in UTexture2D::Init (0x5BE310).
//   UTexture2D::UpdateResource = vtable slot 39 (GPU upload): the only virtual call of
//   TextureRenderTarget2D's initialization (0x599D70), confirmed for Texture2D (0x59C460).
constexpr std::uintptr_t kRvaBulkDataLock = 0xC4260u;
constexpr std::uintptr_t kRvaBulkDataUnlock = 0x834B0u;
constexpr int kTextureUpdateResourceSlot = 39;

// Game menus (the Archipelago login lives in the 2K account screen). Localize(out, Section, Key,
// Package, LangExt, bOptional) -> FString*: __cdecl, `out` uninitialized. Every text of the 2K
// screens and their modals goes through it.
constexpr std::uintptr_t kRvaLocalize = 0xF6500u;
// GMalloc (FMalloc*): vtable +0x08 Malloc(Count, Alignment), +0x10 Free(Ptr). See appFree 0x482AE0.
constexpr std::uintptr_t kRvaGMalloc = 0xF71CC8u;
constexpr std::size_t kMallocSlotMalloc = 0x08u;
constexpr std::size_t kMallocSlotFree = 0x10u;

// XMainMenu: Y key / "2K ACCOUNT" button (vtable +0x1A0, no argument). Depending on the 2K state,
// it leads to the account screen (output 5 of the screen flow) or to the 2K policies.
constexpr std::uintptr_t kRvaMainMenuOnY = 0x72D430u;
constexpr std::size_t kMainMenuCreditsFlags = 0x298u;  // bits 0-1: credits running
constexpr int kMainMenuAccountLink = 5;
// XSequentialMenu: follows output n of the screen flow, (n, 1, 1) like the game. __thiscall.
constexpr std::uintptr_t kRvaMenuFollowLink = 0x743B00u;
// XSequentialMenu: CLIK widget / list by name (FName*), and closing a modal (int* ID).
constexpr std::uintptr_t kRvaMenuFindWidget = 0x727E10u;
constexpr std::uintptr_t kRvaMenuFindList = 0x727DD0u;
constexpr std::uintptr_t kRvaMenuCloseModal = 0x727F70u;
constexpr std::uintptr_t kRvaListSelectedIndex = 0x75D020u;
// GFxObject: GetString(FString* out, const char* member), SetString(const char* member, FString*).
constexpr std::uintptr_t kRvaGFxGetString = 0x4598D0u;
constexpr std::uintptr_t kRvaGFxSetString = 0x459B60u;
// Names (global FNames) of the account screen's widgets.
constexpr std::uintptr_t kRvaNameAccountList = 0x100AB88u;      // TwoKMenuScrollingList
constexpr std::uintptr_t kRvaNameAccountEmail = 0x100AB90u;     // InputTextEmail
constexpr std::uintptr_t kRvaNameAccountPassword = 0x100AB98u;  // InputTextPassword

// XTwoKAccountMenu: AcceptOrSelect (vtable +0x16C; rows 0 e-mail, 1 password, 2 LOG IN,
// 3 CREATE ACCOUNT), native modal windows and the wait callback.
constexpr std::uintptr_t kRvaAccountAcceptOrSelect = 0x8F68D0u;
constexpr std::uintptr_t kRvaAccountShowWait = 0x8F5BD0u;      // TITLE_2KACCOUNT / STATE_1, Cancel button
constexpr std::uintptr_t kRvaAccountWaitCallback = 0x8F5E40u;  // (ModalID, Result, Index), ret 0xC
constexpr std::uintptr_t kRvaAccountShowError = 0x8F49D0u;     // (code); 0x2F = SA01_TITLE_2_ERROR / SA01_BODY_2
constexpr std::uintptr_t kRvaAccountShowSuccess = 0x8F4FC0u;   // SA03_TITLE_1 / SA03_BODY_1, OK = back to the menu
constexpr int kAccountErrorBadCredentials = 0x2F;
constexpr std::size_t kAccountWaitModalId = 0x220u;
constexpr int kAccountRowConnect = 2;
constexpr int kAccountRowCreate = 3;

// XMenuLevelUtilityScreen: the main menu's "2K account" box (logo + "NOT LINKED" state), updated
// by 0xCA9000 with these native GFxObject functions (__thiscall):
//   SetVisible(bool), GetObject(const char* member, UClass* type) -> GFxObject*.
constexpr std::uintptr_t kRvaGFxSetVisible = 0x453690u;
constexpr std::uintptr_t kRvaGFxGetObject = 0x4598C0u;
constexpr std::uintptr_t kRvaGFxObjectClass = 0xFED2ACu;  // UClass* GFxObject (global variable)
constexpr std::size_t kUtilityTwoKWidget = 0x1FCu;        // the box (child "textField" = state)
constexpr std::size_t kUtilityTwoKLoading = 0x200u;
constexpr std::size_t kUtilityTwoKLogo = 0x204u;
constexpr std::size_t kUtilityTwoKLogoOffline = 0x208u;

constexpr std::size_t kObjectOuter = 0x14u;
constexpr std::size_t kObjectName = 0x18u;  // FName.Index
constexpr std::size_t kObjectClass = 0x20u;
constexpr std::size_t kObjectArchetype = 0x24u;
constexpr std::size_t kFieldNext = 0x28u;
constexpr std::size_t kStructSuper = 0x34u;
constexpr std::size_t kStructChildren = 0x38u;
constexpr std::size_t kPropertyOffset = 0x48u;
constexpr std::size_t kBoolPropertyMask = 0x58u;

}  // namespace bsap::build
