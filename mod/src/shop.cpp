#include "shop.h"

#include "ap_texture.h"
#include "bridge.h"
#include "engine_call.h"
#include "game_alloc.h"
#include "game_build.h"
#include "log.h"
#include "ue3.h"
#include "ui_fix.h"

#include "MinHook.h"

#include <intrin.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace bsap::shop {

namespace {

using namespace bsap::build;

constexpr std::uint32_t kNoOffset = 0xFFFFFFFFu;
constexpr std::size_t kStacksOffset = 0x34;  // XAvailableItemInfo.AvailableStacksRemaining
constexpr DWORD kRescanMs = 2000;

template <typename T>
T Get(const void* base, std::size_t offset) {
    T value{};
    ue3::ReadBytes(static_cast<const std::uint8_t*>(base) + offset, &value, sizeof(value));
    return value;
}

template <typename T>
bool Put(const void* base, std::size_t offset, const T& value) {
    return ue3::WriteBytes(const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(base)) + offset, &value,
                           sizeof(value));
}

std::uint32_t Offset(const void* obj, const char* prop) {
    static std::unordered_map<std::string, std::uint32_t> cache;
    const std::string key = ue3::ClassName(obj) + "." + prop;
    auto it = cache.find(key);
    if (it == cache.end()) {
        ue3::Property p;
        it = cache.emplace(key, ue3::FindProperty(obj, prop, &p) ? p.offset : kNoOffset).first;
    }
    return it->second;
}

bool ArrayAt(const void* obj, const char* prop, ue3::ArrayHeader* out) {
    const std::uint32_t off = Offset(obj, prop);
    return off != kNoOffset && ue3::ReadBytes(static_cast<const std::uint8_t*>(obj) + off, out, sizeof(*out)) &&
           out->num >= 0 && out->num <= out->max && out->max < 4096;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// FString = TArray<wchar_t>: { Data, Num (terminating zero included), Max }.
struct FStringHeader {
    wchar_t* data;
    std::int32_t num;
    std::int32_t max;
};

std::string ReadFString(const void* at) {
    FStringHeader h{};
    if (!ue3::ReadBytes(at, &h, sizeof(h)) || !h.data || h.num <= 0 || h.num > 1024) {
        return {};
    }
    std::vector<wchar_t> buf(static_cast<std::size_t>(h.num));
    if (!ue3::ReadBytes(h.data, buf.data(), buf.size() * sizeof(wchar_t))) {
        return {};
    }
    std::string out;
    for (wchar_t c : buf) {
        if (c == 0) break;
        out.push_back(c < 128 ? static_cast<char>(c) : '?');
    }
    return out;
}

std::uint8_t* DbEntry(int index) {
    static const void* db = nullptr;
    if (!ue3::IsObject(db) || ue3::ClassName(db) != "XItemDatabase") {
        db = nullptr;
        if (const void* cls = ue3::FindClass("XItemDatabase")) {
            ue3::ForEachObjectOfClass(cls, [&](int, const void* obj) {
                if (!db && ue3::ObjectName(obj).rfind("Default__", 0) != 0) db = obj;
            });
        }
    }
    ue3::ArrayHeader table{};
    if (!db || !ArrayAt(db, "ItemLookupTable", &table) || index < 0 || index >= table.num) {
        return nullptr;
    }
    return table.data + static_cast<std::size_t>(index) * kSizeofItemCompositeData;
}

int DbCount() {
    int n = 0;
    while (DbEntry(n)) ++n;
    return n;
}

std::vector<const void*> g_machines;
DWORD g_lastScan = 0;

void RescanMachines(bool force) {
    const DWORD now = GetTickCount();
    if (!force && now - g_lastScan < kRescanMs) {
        return;
    }
    g_lastScan = now;
    static const void* machineClass = nullptr;
    if (!ue3::IsObject(machineClass)) {
        machineClass = ue3::FindClass("XVendingMachine");
    }
    g_machines.clear();
    ue3::ForEachObjectOfClass(machineClass, [](int, const void* obj) {
        // Instances placed in a map have a Level as Outer (the others are templates).
        if (ue3::ClassName(ue3::OuterOf(obj)) == "Level") g_machines.push_back(obj);
    });
}

// ItemLookupID -> name of the loot definition (ItemList, the machine's config).
std::unordered_map<std::uint64_t, std::string> LootNames(const void* machine) {
    std::unordered_map<std::uint64_t, std::string> names;
    ue3::ArrayHeader list{};
    if (!ArrayAt(machine, "ItemList", &list)) {
        return names;
    }
    for (int i = 0; i < list.num; ++i) {
        const void* loot = Get<const void*>(list.data, i * kSizeofVendingMachineItem);
        if (ue3::IsObject(loot)) {
            const std::uint32_t off = Offset(loot, "ItemLookupID");
            if (off != kNoOffset) names[Get<std::uint64_t>(loot, off)] = ue3::ObjectName(loot);
        }
    }
    return names;
}

std::uint8_t* Row(const ue3::ArrayHeader& rows, int i) {
    return rows.data + static_cast<std::size_t>(i) * kSizeofAvailableItemInfo;
}

std::set<std::string> g_locked;  // lower case; "prefix*" matches every name starting with prefix
std::map<std::pair<const void*, std::uint64_t>, std::int32_t> g_savedStock;

bool IsLocked(const std::string& lootName) {
    const std::string name = Lower(lootName);
    for (const std::string& pattern : g_locked) {
        if (!pattern.empty() && pattern.back() == '*') {
            if (name.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0) return true;
        } else if (name == pattern) {
            return true;
        }
    }
    return false;
}

struct Slot {
    std::string id;
    std::string machine;
    std::string name;
    int price = 0;
    int carrier = -1;
    bool bought = false;
};

struct CarrierBackup {
    alloc::FStringHeader name{}, desc{};
    std::int32_t value = 0;
    std::int32_t icon = 0;
};

std::vector<Slot> g_slots;
std::map<int, CarrierBackup> g_carrierBackup;
// Machine -> original size of its ItemList before we grew it. The array storage we allocated
// identifies the machine: another machine loaded later at the same address has other storage.
struct ItemListOrigin {
    std::int32_t num;
    const void* data;
};
std::map<const void*, ItemListOrigin> g_itemListOriginalNum;

const ItemListOrigin* KnownOrigin(const void* machine, const ue3::ArrayHeader& list) {
    const auto it = g_itemListOriginalNum.find(machine);
    if (it == g_itemListOriginalNum.end()) return nullptr;
    if (it->second.data != list.data) {
        g_itemListOriginalNum.erase(it);
        return nullptr;
    }
    return &it->second;
}

bool IsCarrier(std::uint64_t lookup) {
    return std::any_of(g_slots.begin(), g_slots.end(), [&](const Slot& s) {
        return s.carrier >= 0 && static_cast<std::uint64_t>(s.carrier) == lookup;
    });
}

bool ReplaceFString(const void* context, const void* at, const std::string& text) {
    alloc::FStringHeader fresh{};
    // UTF-8 from the client; byte by byte if invalid.
    std::wstring wide(text.size(), L'\0');
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                      wide.data(), static_cast<int>(wide.size()));
    if (n > 0) {
        wide.resize(static_cast<std::size_t>(n));
    } else {
        for (std::size_t i = 0; i < text.size(); ++i) wide[i] = static_cast<unsigned char>(text[i]);
    }
    return alloc::NewString(context, wide, &fresh) && Put(at, 0, fresh);
}

const void* At(const void* obj, const char* prop) {
    const std::uint32_t off = Offset(obj, prop);
    return off == kNoOffset ? nullptr : static_cast<const std::uint8_t*>(obj) + off;
}

// Carriers are gear items (XTonicItem): no "full" gauge like money or salts, so the row is always
// buyable. The longest names leave the most room for the AP item's name.
std::vector<int> CarrierPool() {
    static std::vector<int> pool;
    if (pool.empty()) {
        std::vector<std::pair<std::int32_t, int>> found;
        const int n = DbCount();
        for (int i = 0; i < n; ++i) {
            std::uint8_t* e = DbEntry(i);
            if (ue3::ObjectName(Get<const void*>(e, kCompositeItemClass)) == "XTonicItem") {
                found.push_back({Get<std::int32_t>(e, kCompositeItemName + 8), i});
            }
        }
        std::sort(found.begin(), found.end(), [](auto& a, auto& b) { return a.first > b.first; });
        for (auto& f : found) pool.push_back(f.second);
    }
    return pool;
}

bool AssignCarrier(const void* context, Slot& slot) {
    if (slot.carrier >= 0) {
        return true;
    }
    for (int candidate : CarrierPool()) {
        if (std::any_of(g_slots.begin(), g_slots.end(), [&](const Slot& s) { return s.carrier == candidate; })) {
            continue;
        }
        std::uint8_t* e = DbEntry(candidate);
        if (!e) return false;
        CarrierBackup b;
        b.name = Get<alloc::FStringHeader>(e, kCompositeItemName);
        b.desc = Get<alloc::FStringHeader>(e, kCompositeItemDescription);
        b.value = Get<std::int32_t>(e, kCompositeItemValue);
        b.icon = Get<std::int32_t>(e, kCompositeAtlassedIconIndex);

        std::string upper = slot.name;
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (!ReplaceFString(context, e + kCompositeItemName, upper) ||
            !ReplaceFString(context, e + kCompositeItemDescription, "Archipelago item")) {
            Put(e, kCompositeItemName, b.name);
            return false;
        }
        Put<std::int32_t>(e, kCompositeItemValue, slot.price);
        const int apIcon = aptexture::IconIndex(context);
        if (apIcon >= 0) Put<std::int32_t>(e, kCompositeAtlassedIconIndex, apIcon);
        g_carrierBackup[candidate] = b;
        slot.carrier = candidate;
        Log("shop: row %s -> carrier %d", slot.id.c_str(), candidate);
        return true;
    }
    return false;
}

void ReleaseCarrier(int carrier) {
    auto it = g_carrierBackup.find(carrier);
    std::uint8_t* e = DbEntry(carrier);
    if (it == g_carrierBackup.end() || !e) return;
    Put(e, kCompositeItemName, it->second.name);
    Put(e, kCompositeItemDescription, it->second.desc);
    Put<std::int32_t>(e, kCompositeItemValue, it->second.value);
    Put<std::int32_t>(e, kCompositeAtlassedIconIndex, it->second.icon);
    g_carrierBackup.erase(it);
}

// The screen only shows row i of AvailableItems if ItemList has an entry i, so ItemList gets
// neutral entries.
bool CoverItemList(const void* machine, int rowsNeeded) {
    const void* listAt = At(machine, "ItemList");
    ue3::ArrayHeader list{};
    if (!listAt || !ue3::ReadBytes(listAt, &list, sizeof(list)) || list.num <= 0) {
        return false;
    }
    const ItemListOrigin* known = KnownOrigin(machine, list);
    const std::int32_t originalNum = known ? known->num : list.num;
    if (list.num >= rowsNeeded) {
        if (!known) g_itemListOriginalNum[machine] = {originalNum, list.data};
        return true;
    }
    if (!alloc::EnsureArrayCapacity(machine, listAt, kSizeofVendingMachineItem, rowsNeeded - list.num)) {
        return false;
    }
    ue3::ReadBytes(listAt, &list, sizeof(list));
    g_itemListOriginalNum[machine] = {originalNum, list.data};
    const void* loot = Get<const void*>(list.data, 0);  // any valid loot definition
    for (; list.num < rowsNeeded; ++list.num) {
        const struct {
            const void* loot;
            std::int32_t cost, available;
        } entry{loot, -1, -1};
        ue3::WriteBytes(list.data + static_cast<std::size_t>(list.num) * kSizeofVendingMachineItem, &entry,
                        sizeof(entry));
    }
    return Put<std::int32_t>(listAt, 4, list.num);
}

// Adds a "carrier" row at the end of AvailableItems (grown by the game if needed).
bool AppendRow(const void* machine, int carrier) {
    std::uint8_t* e = DbEntry(carrier);
    const void* rowsAt = At(machine, "AvailableItems");
    if (!e || !rowsAt || !alloc::EnsureArrayCapacity(machine, rowsAt, kSizeofAvailableItemInfo, 1)) {
        return false;
    }
    ue3::ArrayHeader rows{};
    ue3::ReadBytes(rowsAt, &rows, sizeof(rows));
    if (!CoverItemList(machine, rows.num + 1)) {
        return false;
    }
    std::uint8_t row[kSizeofAvailableItemInfo] = {};
    if (rows.num > 0) {
        ue3::ReadBytes(Row(rows, 0), row, sizeof(row));  // template: valid cache flags
    }
    const std::uint64_t lookup = static_cast<std::uint64_t>(carrier);
    std::memcpy(row + 0x00, &lookup, 8);  // ItemLookupID
    const std::int32_t one = 1;
    std::memcpy(row + 0x08, &one, 4);  // ItemAmount
    std::memset(row + 0x0C, 0, 12);    // EquippedInLoadoutSlot: an empty array of ours
    const void* cls = Get<const void*>(e, kCompositeItemClass);
    std::memcpy(row + 0x1C, &cls, 4);  // Cached_ItemClass
    std::memset(row + 0x20, 0, 8);     // Validator, OnlyValidForPlayer
    std::memset(row + 0x28, 0, 12);    // AdditionalItemDefinitions...: empty
    std::memcpy(row + kStacksOffset, &one, 4);
    if (!ue3::WriteBytes(Row(rows, rows.num), row, sizeof(row))) {
        return false;
    }
    return Put<std::int32_t>(machine, Offset(machine, "AvailableItems") + 4, rows.num + 1);
}

bool RemoveRow(const void* machine, const ue3::ArrayHeader& rows, int i) {
    const std::size_t tail = static_cast<std::size_t>(rows.num - i - 1) * kSizeofAvailableItemInfo;
    std::vector<std::uint8_t> moved(tail);
    if (tail &&
        (!ue3::ReadBytes(Row(rows, i + 1), moved.data(), tail) || !ue3::WriteBytes(Row(rows, i), moved.data(), tail))) {
        return false;
    }
    return Put<std::int32_t>(machine, Offset(machine, "AvailableItems") + 4, rows.num - 1);
}

bool GetBool(const void* obj, const char* prop) {
    ue3::Property p;
    return ue3::FindProperty(obj, prop, &p) && p.boolMask && (Get<std::uint32_t>(obj, p.offset) & p.boolMask);
}

void SetBool(const void* obj, const char* prop, bool value) {
    ue3::Property p;
    if (!ue3::FindProperty(obj, prop, &p) || !p.boolMask) return;
    std::uint32_t word = Get<std::uint32_t>(obj, p.offset);
    word = value ? (word | p.boolMask) : (word & ~p.boolMask);
    Put(obj, p.offset, word);
}

// The game fills AvailableItems from ItemList the first time the machine is used: before that,
// we don't touch it (it would overwrite our rows and copy our ItemList entries again).
bool Populated(const void* machine) {
    return GetBool(machine, "bVendingItemsPopulated");
}

// An open vending screen refers to rows by index: while it is open, rows are never added or
// removed.
const void* g_openScreen = nullptr;
DWORD g_screenSeenAt = 0;

bool VendingScreenOpen() {
    static DWORD lastCheck = 0;
    static bool open = false;
    const DWORD now = GetTickCount();
    if (now - lastCheck < 250) return open;
    lastCheck = now;
    if (open) g_screenSeenAt = now;
    open = false;
    g_openScreen = nullptr;
    static const void* classes[2] = {};
    if (!ue3::IsObject(classes[0])) {
        classes[0] = ue3::FindClass("XDollarBillScreen");
        classes[1] = ue3::FindClass("XVendingMachineScreen");
    }
    for (const void* cls : classes) {
        ue3::ForEachObjectOfClass(cls, [&](int, const void* screen) {
            if (GetBool(screen, "bMovieIsOpen")) {
                open = true;
                g_openScreen = screen;
            }
        });
    }
    if (open) g_screenSeenAt = now;
    return open;
}

void PlayMachineEffect(const void* machine, const char* prop) {
    const std::uint32_t off = Offset(machine, prop);
    if (off == kNoOffset) return;
    using PlayEffect_t = void(__fastcall*)(const void* self, void* edx, const void* effect, int, int, int, int, int,
                                           int, int, int, int, int, int);
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("BioShockInfinite.exe"));
    const auto play = reinterpret_cast<PlayEffect_t>(base + kRvaPlayEffectContainer);
    play(machine, nullptr, static_cast<const std::uint8_t*>(machine) + off, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

const void* MachineByPath(const std::string& path) {
    for (const void* m : g_machines) {
        if (_stricmp(ue3::PathName(m).c_str(), path.c_str()) == 0) return m;
    }
    return nullptr;
}

// The save keeps the machines' rows, ours included, but ItemList goes back to its original size on
// load. A row past that size that isn't a current AP row of this machine comes from an older seed
// ("undefined", or mistaken for another machine's row with the same carrier), so it is removed.
void RemoveStaleRows(const void* machine) {
    const void* listAt = At(machine, "ItemList");
    ue3::ArrayHeader list{}, rows{};
    if (!listAt || !ue3::ReadBytes(listAt, &list, sizeof(list))) return;
    const ItemListOrigin* known = KnownOrigin(machine, list);
    const int vanillaRows = known ? known->num : list.num;
    const std::string path = ue3::PathName(machine);
    int removed = 0;
    for (int i = vanillaRows; ArrayAt(machine, "AvailableItems", &rows) && i < rows.num;) {
        const std::uint64_t lookup = Get<std::uint64_t>(Row(rows, i), 0);
        const bool ours = std::any_of(g_slots.begin(), g_slots.end(), [&](const Slot& s) {
            return s.carrier >= 0 && static_cast<std::uint64_t>(s.carrier) == lookup &&
                   _stricmp(s.machine.c_str(), path.c_str()) == 0;
        });
        if (!ours && RemoveRow(machine, rows, i)) {
            ++removed;
        } else {
            ++i;
        }
    }
    if (removed) Log("shop: %d old AP row(s) removed from %s", removed, path.c_str());
}

std::set<std::string> g_disabledEmpty;

void ApplyToMachine(const void* machine, bool structural) {
    if (structural && Populated(machine)) RemoveStaleRows(machine);
    ue3::ArrayHeader rows{};
    if (!ArrayAt(machine, "AvailableItems", &rows)) {
        return;
    }
    // 1. Locks on the game's wares (a stock change, safe at any time).
    const auto names = LootNames(machine);
    for (int i = 0; i < rows.num; ++i) {
        std::uint8_t* r = Row(rows, i);
        const std::uint64_t lookup = Get<std::uint64_t>(r, 0);
        if (IsCarrier(lookup)) continue;
        auto it = names.find(lookup);
        const bool locked = it != names.end() && IsLocked(it->second);
        const auto key = std::make_pair(machine, lookup);
        const std::int32_t stacks = Get<std::int32_t>(r, kStacksOffset);
        if (locked) {
            g_savedStock.emplace(key, stacks);
            if (stacks != 0) Put<std::int32_t>(r, kStacksOffset, 0);
        } else if (auto saved = g_savedStock.find(key); saved != g_savedStock.end()) {
            Put<std::int32_t>(r, kStacksOffset, saved->second);
            g_savedStock.erase(saved);
        }
    }
    const std::string path = ue3::PathName(machine);
    for (Slot& s : g_slots) {
        if (_stricmp(s.machine.c_str(), path.c_str()) != 0) continue;
        int found = -1;
        for (int i = 0; s.carrier >= 0 && i < rows.num && found < 0; ++i) {
            if (Get<std::uint64_t>(Row(rows, i), 0) == static_cast<std::uint64_t>(s.carrier)) found = i;
        }
        // The Archipelago logo may only be available after the row is created.
        if (s.carrier >= 0) {
            const int apIcon = aptexture::IconIndex(machine);
            std::uint8_t* e = DbEntry(s.carrier);
            if (apIcon >= 0 && e && Get<std::int32_t>(e, kCompositeAtlassedIconIndex) != apIcon) {
                Put<std::int32_t>(e, kCompositeAtlassedIconIndex, apIcon);
            }
        }
        if (found < 0 && structural && Populated(machine) && AssignCarrier(machine, s)) {
            if (AppendRow(machine, s.carrier)) ArrayAt(machine, "AvailableItems", &rows);
        }
    }
    // 3. Nothing left to sell (like the Fair's Veni! Vidi! Vigor!, whose only upgrade is an AP
    // item): the screen would only show "undefined", so the machine is disabled until it has
    // something again.
    if (!Populated(machine)) return;
    bool empty = true;
    for (int i = 0; i < rows.num && empty; ++i) empty = Get<std::int32_t>(Row(rows, i), kStacksOffset) == 0;
    const bool ours = g_disabledEmpty.count(path) > 0;
    if (empty && !ours && !GetBool(machine, "bUseDisabled")) {
        SetBool(machine, "bUseDisabled", true);
        g_disabledEmpty.insert(path);
        Log("shop: %s has nothing to sell, disabled", path.c_str());
    } else if (!empty && ours) {
        SetBool(machine, "bUseDisabled", false);
        g_disabledEmpty.erase(path);
        Log("shop: %s has wares again, enabled", path.c_str());
    }
}

std::vector<const void*> ScreenRows(const void* screen) {
    static const void* listClass = nullptr;
    if (!ue3::IsObject(listClass)) listClass = ue3::FindClass("XClikDataProviderList");
    std::vector<const void*> rows;
    ue3::ForEachObjectOfClass(listClass, [&](int, const void* list) {
        if (ue3::OuterOf(list) != screen) return;
        ue3::ArrayHeader members{};
        if (!ArrayAt(list, "DataProviderMembers", &members)) return;
        for (int i = 0; i < members.num; ++i) {
            const void* m = Get<const void*>(members.data, i * sizeof(void*));
            if (ue3::IsObject(m) && Offset(m, "ItemLookupID") != kNoOffset) rows.push_back(m);
        }
    });
    return rows;
}

bool BuySlot(void* screen, Slot& slot) {
    const std::uint64_t carrier = static_cast<std::uint64_t>(slot.carrier);
    const std::vector<const void*> rows = ScreenRows(screen);
    int cost = slot.price;
    for (const void* r : rows) {
        if (Get<std::uint64_t>(r, Offset(r, "ItemLookupID")) == carrier)
            cost = Get<std::int32_t>(r, Offset(r, "ItemCost"));
    }
    const void* machine = MachineByPath(slot.machine);
    const int cash = Get<std::int32_t>(screen, Offset(screen, "CashCurrencyAmount"));
    if (cash < cost) {
        if (machine) PlayMachineEffect(machine, "PurchaseFailEffect");
        bridge::Send("event shopfail " + slot.id + " argent " + std::to_string(cash) + "/" + std::to_string(cost));
        return true;  // blocked anyway: the game must never process a carrier
    }
    // Marked sold before taking the money: the money change redraws the rows at once, and the row
    // must already be sold then, not seconds later.
    slot.bought = true;
    const void* pc = Get<const void*>(screen, Offset(screen, "PlayerOwner"));
    if (ue3::IsObject(pc)) {
        engine::Call(pc, "ClientAdjustCurrency", {"1", std::to_string(-cost)}, [](const std::string&) {});
    }
    if (machine) {
        PlayMachineEffect(machine, "PurchaseSuccessEffect");
    }
    const int newCash = cash - cost;
    Put<std::int32_t>(screen, Offset(screen, "CashCurrencyAmount"), newCash);
    SetBool(screen, "bUpdateYourCashValue", true);
    SetBool(screen, "bIsXGFxMovieDirty", true);
    Log("shop: bought %s (%d $)", slot.id.c_str(), cost);
    bridge::Send("event shopbuy " + slot.id);
    return true;
}

// The game computes sold = !(buyable && HasStock(index)). For a bought AP row we answer "out of
// stock" to the row creation and update but "in stock" to the list filter, so the row stays visible
// as sold without touching its real stock.
using HasStock_t = int(__fastcall*)(const void* machine, void* edx, int index);
HasStock_t g_originalHasStock = nullptr;

bool IsSoldCarrier(std::uint64_t lookup) {
    return std::any_of(g_slots.begin(), g_slots.end(), [&](const Slot& s) {
        return s.bought && s.carrier >= 0 && static_cast<std::uint64_t>(s.carrier) == lookup;
    });
}

int __fastcall HasStockDetour(const void* machine, void* edx, int index) {
    static const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("BioShockInfinite.exe"));
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - base;
    if (caller == kRvaHasStockFromRowInit || caller == kRvaHasStockFromRowUpdate) {
        ue3::ArrayHeader rows{};
        if (ArrayAt(machine, "AvailableItems", &rows) && index >= 0 && index < rows.num &&
            IsSoldCarrier(Get<std::uint64_t>(Row(rows, index), 0))) {
            return 0;
        }
    }
    return g_originalHasStock(machine, edx, index);
}

// The screen hides wares the player already owns. Our carriers are gear, including the Infusion
// bonuses (Tonic_HP_Bonus...), so taking that Infusion made the AP row vanish. AP rows are always
// valid; the rest is untouched.
using ItemAvailable_t = int(__fastcall*)(const void* machine, void* edx, const void* controller, int index);
ItemAvailable_t g_originalItemAvailable = nullptr;

int __fastcall ItemAvailableDetour(const void* machine, void* edx, const void* controller, int index) {
    ue3::ArrayHeader rows{};
    if (ArrayAt(machine, "AvailableItems", &rows) && index >= 0 && index < rows.num &&
        IsCarrier(Get<std::uint64_t>(Row(rows, index), 0))) {
        return 1;
    }
    return g_originalItemAvailable(machine, edx, controller, index);
}

bool InstallHook(std::uintptr_t rva, void* detour, void** original, const char* what) {
    void* target =
        reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(GetModuleHandleA("BioShockInfinite.exe")) + rva);
    if (MH_CreateHook(target, detour, original) != MH_OK || MH_EnableHook(target) != MH_OK) {
        Log("ERROR: cannot hook %s", what);
        return false;
    }
    Log("Hooked %s", what);
    return true;
}

// From the game thread: hooking at process start freezes the game.
void EnsureHasStockHook() {
    static bool done = false;
    if (done) return;
    done = true;
    InstallHook(kRvaVendingHasStock, reinterpret_cast<void*>(&HasStockDetour),
                reinterpret_cast<void**>(&g_originalHasStock), "XVendingMachine::HasStock");
    InstallHook(kRvaVendingItemAvailable, reinterpret_cast<void*>(&ItemAvailableDetour),
                reinterpret_cast<void**>(&g_originalItemAvailable), "the vending row validity");
}

// shopclear / shopslot change the arrays: with a screen open, they wait for it to close.
std::vector<std::string> g_deferred;

float g_px = 0, g_py = 0, g_pz = 0;

void CmdVend(const Reply& reply) {
    RescanMachines(true);
    for (const void* m : g_machines) {
        char head[160];
        // Irrational renamed Actor.Location to XPrivateLocalLocation.
        const std::uint32_t loc = Offset(m, "XPrivateLocalLocation");
        float dist = -1;
        if (loc != kNoOffset) {
            const float x = Get<float>(m, loc), y = Get<float>(m, loc + 4), z = Get<float>(m, loc + 8);
            dist = std::sqrt((x - g_px) * (x - g_px) + (y - g_py) * (y - g_py) + (z - g_pz) * (z - g_pz)) / 100.0f;
        }
        std::snprintf(head, sizeof(head), "vend #%d a %.0f m  ", ue3::IndexOf(m), dist);
        reply(head + ue3::PathName(m));
        ue3::ArrayHeader rows{};
        ArrayAt(m, "AvailableItems", &rows);
        const auto names = LootNames(m);
        for (int i = 0; i < rows.num; ++i) {
            std::uint8_t* r = Row(rows, i);
            const std::uint64_t lookup = Get<std::uint64_t>(r, 0);
            std::string label;
            if (auto it = names.find(lookup); it != names.end()) label = it->second;
            for (const Slot& s : g_slots) {
                if (static_cast<std::uint64_t>(s.carrier) == lookup) label = "[AP " + s.id + "] " + s.name;
            }
            if (label.empty()) {
                std::uint8_t* e = DbEntry(static_cast<int>(lookup));
                label = e ? ReadFString(e + kCompositeItemName) : "id " + std::to_string(lookup);
            }
            if (IsLocked(label)) label += "  [VERROU]";
            reply("  [" + std::to_string(i) + "] " + label +
                  "  stock=" + std::to_string(Get<std::int32_t>(r, kStacksOffset)));
        }
    }
    reply("end vend " + std::to_string(g_machines.size()));
}

const void* MachineArg(const std::string& arg, const Reply& reply) {
    const void* m = ue3::GlobalObject(std::atoi(arg.c_str() + (arg.empty() || arg[0] != '#' ? 0 : 1)));
    RescanMachines(true);
    if (std::find(g_machines.begin(), g_machines.end(), m) == g_machines.end()) {
        reply("err " + arg + " is not a vending machine of the level (see 'vend')");
        return nullptr;
    }
    return m;
}

}  // namespace

bool g_replaying = false;

bool Command(const std::string& cmd, const std::string& line, const Reply& reply) {
    std::istringstream in(line);
    std::string skip, a1, a2, a3;
    in >> skip >> a1 >> a2 >> a3;

    if (cmd == "vend") {
        CmdVend(reply);
    } else if (cmd == "vendstock" || cmd == "vendremove") {
        const void* m = MachineArg(a1, reply);
        ue3::ArrayHeader rows{};
        const int i = std::atoi(a2.c_str());
        if (!m || !ArrayAt(m, "AvailableItems", &rows) || a2.empty() || i < 0 || i >= rows.num) {
            if (m) reply("err usage: vendstock #M i N | vendremove #M i");
            return true;
        }
        if (cmd == "vendstock") {
            Put<std::int32_t>(Row(rows, i), kStacksOffset, std::atoi(a3.c_str()));
            reply("ok stock=" + a3);
        } else {
            reply(RemoveRow(m, rows, i) ? "ok row removed" : "err cannot remove");
        }
    } else if (cmd == "lock" || cmd == "unlock") {
        if (a1.empty()) {
            reply("err usage: " + cmd + " <loot name, e.g. Loot_Consumable_Health_HealthKit>");
            return true;
        }
        if (cmd == "lock")
            g_locked.insert(Lower(a1));
        else
            g_locked.erase(Lower(a1));
        RescanMachines(true);
        for (const void* m : g_machines) ApplyToMachine(m, !VendingScreenOpen());
        reply("ok " + cmd + " " + a1);
    } else if (cmd == "lockclear") {
        g_locked.clear();
        RescanMachines(true);
        for (const void* m : g_machines) ApplyToMachine(m, !VendingScreenOpen());
        reply("ok locks removed");
    } else if (cmd == "locks") {
        for (const std::string& l : g_locked) reply("lock " + l);
        reply("end locks " + std::to_string(g_locked.size()));
    } else if ((cmd == "shopslot" || cmd == "shopclear" || cmd == "shopsold") && !g_replaying &&
               (VendingScreenOpen() || !g_deferred.empty())) {
        g_deferred.push_back(line);
        reply("ok " + cmd + " deferred (vending screen open)");
    } else if (cmd == "shopslot") {
        // shopslot <id> <machine> <price> <name...>
        std::string name;
        std::getline(in, name);
        name.erase(0, name.find_first_not_of(' '));
        if (a1.empty() || a2.empty() || a3.empty() || name.empty()) {
            reply("err usage: shopslot <id> <machine> <price> <shown name>");
            return true;
        }
        for (const Slot& s : g_slots) {
            if (s.id == a1) {
                reply("err row " + a1 + " already defined");
                return true;
            }
        }
        Slot s;
        s.id = a1;
        s.machine = a2;
        s.price = std::atoi(a3.c_str());
        s.name = name;
        g_slots.push_back(s);
        RescanMachines(true);
        for (const void* m : g_machines) ApplyToMachine(m, true);
        reply("ok row " + a1 +
              (g_slots.back().carrier >= 0 ? " placed (carrier " + std::to_string(g_slots.back().carrier) + ")"
                                           : " waiting (machine not in this level)"));
    } else if (cmd == "shopsold") {
        // shopsold <id>: already bought (shown sold, click blocked)
        for (Slot& s : g_slots) {
            if (s.id == a1) s.bought = true;
        }
        reply("ok sold " + a1);
    } else if (cmd == "shops") {
        for (const Slot& s : g_slots) {
            reply("slot " + s.id + " machine=" + s.machine + " price=" + std::to_string(s.price) +
                  " carrier=" + std::to_string(s.carrier) + (s.bought ? " BOUGHT" : "") + " : " + s.name);
        }
        reply("end shops " + std::to_string(g_slots.size()));
    } else if (cmd == "shopclear") {
        RescanMachines(true);
        for (const void* m : g_machines) {
            ue3::ArrayHeader rows{};
            for (int i = 0; ArrayAt(m, "AvailableItems", &rows) && i < rows.num;) {
                if (IsCarrier(Get<std::uint64_t>(Row(rows, i), 0)))
                    RemoveRow(m, rows, i);
                else
                    ++i;
            }
        }
        for (const Slot& s : g_slots) {
            if (s.carrier >= 0) ReleaseCarrier(s.carrier);
        }
        for (const auto& [machine, origin] : g_itemListOriginalNum) {
            const void* listAt = std::find(g_machines.begin(), g_machines.end(), machine) != g_machines.end()
                                     ? At(machine, "ItemList")
                                     : nullptr;
            ue3::ArrayHeader list{};
            if (listAt && ue3::ReadBytes(listAt, &list, sizeof(list)) && list.data == origin.data) {
                Put<std::int32_t>(machine, Offset(machine, "ItemList") + 4, origin.num);
            }
        }
        g_itemListOriginalNum.clear();
        g_slots.clear();
        reply("ok AP rows removed");
    } else {
        return false;
    }
    return true;
}

void Tick(const void* controller, float x, float y, float z) {
    (void)controller;
    EnsureHasStockHook();
    VendingScreenOpen();      // updates the state GrantsBlocked() reads
    uifix::Tick(controller);  // "Sold!" banner translation
    g_px = x;
    g_py = y;
    g_pz = z;
    if (!g_deferred.empty() && !VendingScreenOpen()) {
        std::vector<std::string> lines;
        lines.swap(g_deferred);
        g_replaying = true;
        for (const std::string& l : lines) {
            Command(l.substr(0, l.find(' ')), l, [](const std::string&) {});
        }
        g_replaying = false;
    }
    if (g_locked.empty() && g_slots.empty()) {
        return;
    }
    const DWORD before = g_lastScan;
    RescanMachines(false);
    if (g_lastScan != before) {
        const bool structural = !VendingScreenOpen();
        for (const void* m : g_machines) ApplyToMachine(m, structural);
    }
}

bool Intercept(void* self, const void* func) {
    if (g_slots.empty()) {
        return false;
    }
    static std::unordered_map<const void*, bool> isBuy;
    auto it = isBuy.find(func);
    if (it == isBuy.end()) {
        const std::string name = ue3::ObjectName(func);
        it = isBuy.emplace(func, name == "AcceptOrSelect" || name == "OnVendingItemDoubleClick").first;
    }
    if (!it->second || ue3::ClassName(self) != "XDollarBillScreen") {
        return false;
    }
    const std::uint32_t off = Offset(self, "CurrentItemLookupId");
    if (off == kNoOffset) {
        return false;
    }
    const std::uint64_t selected = Get<std::uint64_t>(self, off);
    for (Slot& s : g_slots) {
        if (s.carrier >= 0 && static_cast<std::uint64_t>(s.carrier) == selected) {
            // A carrier must never reach the game's purchase code (crash), even once bought.
            if (!s.bought) return BuySlot(self, s);
            if (const void* machine = MachineByPath(s.machine)) PlayMachineEffect(machine, "PurchaseFailEffect");
            return true;
        }
    }
    return false;
}

bool AnyScreenOpen() {
    return VendingScreenOpen();
}

bool GrantsBlocked() {
    // While closing, the screen still listens to money and inventory but the game has forgotten the
    // machine: a grant then crashes it.
    constexpr DWORD kAfterCloseMs = 1500;
    return VendingScreenOpen() || (g_screenSeenAt && GetTickCount() - g_screenSeenAt < kAfterCloseMs);
}

}  // namespace bsap::shop
