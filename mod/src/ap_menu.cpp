#include "ap_menu.h"

#include "ap_client.h"
#include "ap_texture.h"
#include "engine_call.h"
#include "game_alloc.h"
#include "game_build.h"
#include "log.h"
#include "ue3.h"
#include "ui_fix.h"

#include "MinHook.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace bsap::apmenu {

namespace {

using namespace bsap::build;

struct FString {
    wchar_t* data;
    std::int32_t num;
    std::int32_t max;
};

// __thiscall modeled as __fastcall with a dummy edx.
using Localize_t = FString*(__cdecl*)(FString* out, const wchar_t* section, const wchar_t* key, const wchar_t* package,
                                      const wchar_t* lang, int optional);
using NoArg_t = void(__fastcall*)(void* self, void* edx);
using FollowLink_t = void(__fastcall*)(void* self, void* edx, int link, int a, int b);
using FindWidget_t = void*(__fastcall*)(void* self, void* edx, const void* name);
using CloseModal_t = void(__fastcall*)(void* self, void* edx, int* modalId);
using SelectedIndex_t = int(__fastcall*)(void* list, void* edx);
using GetString_t = FString*(__fastcall*)(void* gfx, void* edx, FString* out, const char* member);
using SetString_t = void(__fastcall*)(void* gfx, void* edx, const char* member, const FString* value);
using ShowError_t = void(__fastcall*)(void* self, void* edx, int code);
using WaitCallback_t = void(__fastcall*)(void* self, void* edx, int modalId, int result, int index);
using SetVisible_t = void(__fastcall*)(void* gfx, void* edx, int visible);
using GetObject_t = void*(__fastcall*)(void* gfx, void* edx, const char* member, const void* type);

std::uintptr_t Base() {
    static const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("BioShockInfinite.exe"));
    return base;
}

template <typename T>
T Fn(std::uintptr_t rva) {
    return reinterpret_cast<T>(Base() + rva);
}

const void* Name(std::uintptr_t rva) {
    return reinterpret_cast<const void*>(Base() + rva);
}

Localize_t g_localize = nullptr;
NoArg_t g_mainMenuOnY = nullptr;
NoArg_t g_accountAccept = nullptr;
WaitCallback_t g_waitCallback = nullptr;

void* GMalloc() {
    return *reinterpret_cast<void**>(Base() + kRvaGMalloc);
}

void* GameMalloc(std::size_t bytes) {
    void* gm = GMalloc();
    if (!gm) return nullptr;
    using Malloc_t = void*(__fastcall*)(void* self, void* edx, DWORD count, DWORD alignment);
    void* const* vtable = *static_cast<void* const* const*>(gm);
    return reinterpret_cast<Malloc_t>(vtable[kMallocSlotMalloc / 4])(gm, nullptr, static_cast<DWORD>(bytes), 8);
}

void GameFree(void* p) {
    void* gm = GMalloc();
    if (!gm || !p) return;
    using Free_t = void(__fastcall*)(void* self, void* edx, void* ptr);
    void* const* vtable = *static_cast<void* const* const*>(gm);
    reinterpret_cast<Free_t>(vtable[kMallocSlotFree / 4])(gm, nullptr, p);
}

std::wstring TakeString(FString& s) {
    std::wstring text = s.data && s.num > 0 ? std::wstring(s.data) : std::wstring();
    GameFree(s.data);
    s = {};
    return text;
}

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

enum Msg {
    kAddressLabel,
    kDisconnectButton,
    kConnected,
    kDisconnected,
    kNotConnected,
    kBadAddress,
    kUnreachable,
    kInvalidSlot,
    kInvalidGame,
    kInvalidPassword,
    kIncompatibleVersion,
    kRefused,
    kStatusConnected,
    kStatusIdle,
    kStatusRetry,
    kAutoConnect,  // button hint, after a check box
    kMsgCount
};

// %1 = server:port, %2 = player name, %3 = refusal detail.
struct Language {
    const char* code;
    const wchar_t* text[kMsgCount];
};

const Language kLanguages[] = {
    {"INT",
     {L"SLOT@SERVER:PORT", L"DISCONNECT", L"Connected to %1 as %2.", L"Disconnected from Archipelago.",
      L"You are not connected to Archipelago.",
      L"Enter your slot name, the server and its port as name@server:port, for example Booker@archipelago.gg:38281.",
      L"Could not reach %1. Check the address and port, and that the server is running.",
      L"There is no player named \"%2\" in this game.", L"The slot \"%2\" is not a BioShock Infinite slot.",
      L"Wrong password.", L"This server needs a newer version of the mod.", L"The server refused the connection (%3).",
      L"CONNECTED: %2", L"NOT CONNECTED", L"RECONNECTING...", L"AUTO-CONNECT"}},
    {"FRA",
     {L"NOM@SERVEUR:PORT", L"DÉCONNEXION", L"Connecté à %1 en tant que %2.", L"Déconnecté d'Archipelago.",
      L"Tu n'es pas connecté à Archipelago.",
      L"Saisis ton nom de joueur, le serveur et son port sous la forme nom@serveur:port, par exemple "
      L"Booker@archipelago.gg:38281.",
      L"Impossible de joindre %1. Vérifie l'adresse et le port, et que le serveur est lancé.",
      L"Aucun joueur nommé « %2 » dans cette partie.", L"Le joueur « %2 » n'est pas un slot BioShock Infinite.",
      L"Mot de passe incorrect.", L"Ce serveur demande une version plus récente du mod.",
      L"Le serveur a refusé la connexion (%3).", L"CONNECTÉ : %2", L"NON CONNECTÉ", L"RECONNEXION...",
      L"CONNEXION AUTO"}},
    {"DEU",
     {L"NAME@SERVER:PORT", L"TRENNEN", L"Verbunden mit %1 als %2.", L"Von Archipelago getrennt.",
      L"Du bist nicht mit Archipelago verbunden.",
      L"Gib deinen Spielernamen, den Server und den Port im Format name@server:port ein, zum Beispiel "
      L"Booker@archipelago.gg:38281.",
      L"%1 ist nicht erreichbar. Prüfe Adresse und Port und ob der Server läuft.",
      L"In diesem Spiel gibt es keinen Spieler namens \"%2\".", L"Der Slot \"%2\" ist kein BioShock-Infinite-Slot.",
      L"Falsches Passwort.", L"Dieser Server benötigt eine neuere Version des Mods.",
      L"Der Server hat die Verbindung abgelehnt (%3).", L"VERBUNDEN: %2", L"NICHT VERBUNDEN", L"VERBINDE...",
      L"AUTOM. VERBINDEN"}},
    {"ITA",
     {L"NOME@SERVER:PORTA", L"DISCONNETTI", L"Connesso a %1 come %2.", L"Disconnesso da Archipelago.",
      L"Non sei connesso ad Archipelago.",
      L"Inserisci il nome del giocatore, il server e la porta nel formato nome@server:porta, ad esempio "
      L"Booker@archipelago.gg:38281.",
      L"Impossibile raggiungere %1. Controlla indirizzo e porta, e che il server sia avviato.",
      L"Nessun giocatore di nome \"%2\" in questa partita.", L"Lo slot \"%2\" non è uno slot di BioShock Infinite.",
      L"Password errata.", L"Questo server richiede una versione più recente della mod.",
      L"Il server ha rifiutato la connessione (%3).", L"CONNESSO: %2", L"NON CONNESSO", L"RICONNESSIONE...",
      L"CONNESSIONE AUTO"}},
    {"ESN",
     {L"NOMBRE@SERVIDOR:PUERTO", L"DESCONECTAR", L"Conectado a %1 como %2.", L"Desconectado de Archipelago.",
      L"No estás conectado a Archipelago.",
      L"Introduce tu nombre de jugador, el servidor y su puerto con el formato nombre@servidor:puerto, por ejemplo "
      L"Booker@archipelago.gg:38281.",
      L"No se puede conectar con %1. Comprueba la dirección y el puerto, y que el servidor esté en marcha.",
      L"No hay ningún jugador llamado \"%2\" en esta partida.", L"El slot \"%2\" no es de BioShock Infinite.",
      L"Contraseña incorrecta.", L"Este servidor necesita una versión más reciente del mod.",
      L"El servidor ha rechazado la conexión (%3).", L"CONECTADO: %2", L"NO CONECTADO", L"RECONECTANDO...",
      L"CONEXIÓN AUTO"}},
};

const Language* g_language = nullptr;

// Game language: the one whose UserInterface file gives the same "PASSWORD" as the game.
void DetectLanguage() {
    const Localize_t localize = g_localize ? g_localize : Fn<Localize_t>(kRvaLocalize);
    FString value{};
    localize(&value, L"DNAGeneral", L"FORM_PASSWORD", L"UserInterface", nullptr, 0);
    std::string code = uifix::LanguageWithValue("UserInterface", L"[DNAGeneral]", L"FORM_PASSWORD", TakeString(value));
    if (code == "ESM") code = "ESN";
    g_language = &kLanguages[0];
    for (const Language& l : kLanguages) {
        if (code == l.code) g_language = &l;
    }
    Log("apmenu: game language %s, texts %s", code.empty() ? "?" : code.c_str(), g_language->code);
}

std::wstring Text(Msg msg) {
    const apclient::Address& a = apclient::CurrentAddress();
    std::wstring s = g_language->text[msg];
    for (const auto& [key, value] : {std::pair<const wchar_t*, std::wstring>{L"%1", Wide(a.HostPort())},
                                     {L"%2", Wide(a.name)},
                                     {L"%3", Wide(apclient::FailureDetail())}}) {
        for (std::size_t i; (i = s.find(key)) != std::wstring::npos;) s.replace(i, 2, value);
    }
    return s;
}

// Replaced translations ("Section/Key" of the UserInterface package). Fixed ones are for the
// screen; temporary ones only live while a modal is built, which is synchronous.

std::mutex g_overrideMutex;
std::map<std::wstring, std::wstring> g_fixed, g_temporary;

FString* __cdecl LocalizeDetour(FString* out, const wchar_t* section, const wchar_t* key, const wchar_t* package,
                                const wchar_t* lang, int optional) {
    if (out && section && key && package && _wcsicmp(package, L"UserInterface") == 0) {
        std::wstring text;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(g_overrideMutex);
            const std::wstring id = std::wstring(section) + L"/" + key;
            for (const auto* table : {&g_temporary, &g_fixed}) {
                if (const auto it = table->find(id); !found && it != table->end()) {
                    text = it->second;
                    found = true;
                }
            }
        }
        if (found) {
            const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
            if (auto* data = static_cast<wchar_t*>(GameMalloc(bytes))) {
                std::memcpy(data, text.c_str(), bytes);
                *out = {data, static_cast<std::int32_t>(text.size() + 1), static_cast<std::int32_t>(text.size() + 1)};
                return out;
            }
        }
    }
    return g_localize(out, section, key, package, lang, optional);
}

bool MovieOpen(const void* movie) {
    ue3::Property p;
    std::uint32_t bits = 0;
    return ue3::FindProperty(movie, "bMovieIsOpen", &p) &&
           ue3::ReadBytes(static_cast<const std::uint8_t*>(movie) + p.offset, &bits, sizeof(bits)) &&
           (bits & p.boolMask);
}

bool AccountMenuOpen(void* menu) {
    if (!ue3::IsObject(menu) || ue3::ClassName(menu) != "XTwoKAccountMenu") return false;
    ue3::Property p;
    std::uint32_t bits = 0;
    return ue3::FindProperty(menu, "bMovieIsOpen", &p) &&
           ue3::ReadBytes(static_cast<const std::uint8_t*>(menu) + p.offset, &bits, sizeof(bits)) &&
           (bits & p.boolMask);
}

void* Widget(void* menu, std::uintptr_t nameRva) {
    return Fn<FindWidget_t>(kRvaMenuFindWidget)(menu, nullptr, Name(nameRva));
}

std::wstring ReadField(void* menu, std::uintptr_t nameRva) {
    void* widget = Widget(menu, nameRva);
    if (!widget) return {};
    FString value{};
    Fn<GetString_t>(kRvaGFxGetString)(widget, nullptr, &value, "text");
    return TakeString(value);
}

bool WriteField(void* menu, std::uintptr_t nameRva, const std::wstring& text) {
    void* widget = Widget(menu, nameRva);
    if (!widget) return false;
    std::wstring copy = text;
    const FString value{copy.data(), static_cast<std::int32_t>(copy.size() + 1),
                        static_cast<std::int32_t>(copy.size() + 1)};
    Fn<SetString_t>(kRvaGFxSetString)(widget, nullptr, "text", &value);
    return true;
}

enum class Modal { Wait, Success, Error, Info };

void ShowModal(void* menu, Modal kind, const std::wstring& body = {}) {
    {
        std::lock_guard<std::mutex> lock(g_overrideMutex);
        g_temporary.clear();
        switch (kind) {
            case Modal::Wait:
                g_temporary[L"DNAGeneral/TITLE_2KACCOUNT"] = L"ARCHIPELAGO";
                break;
            case Modal::Success:
                g_temporary[L"XTwoKAccountMenu/SA03_TITLE_1"] = L"ARCHIPELAGO";
                g_temporary[L"XTwoKAccountMenu/SA03_BODY_1"] = body;
                break;
            case Modal::Info:
                g_temporary[L"XTwoKAccountMenu/SA01_TITLE_2_ERROR"] = L"ARCHIPELAGO";
                [[fallthrough]];
            case Modal::Error:  // original title: "CONNECTION ERROR"
                g_temporary[L"XTwoKAccountMenu/SA01_BODY_2"] = body;
                break;
        }
    }
    switch (kind) {
        case Modal::Wait:
            Fn<NoArg_t>(kRvaAccountShowWait)(menu, nullptr);
            break;
        case Modal::Success:
            Fn<NoArg_t>(kRvaAccountShowSuccess)(menu, nullptr);
            break;
        default:
            Fn<ShowError_t>(kRvaAccountShowError)(menu, nullptr, kAccountErrorBadCredentials);
            break;
    }
    std::lock_guard<std::mutex> lock(g_overrideMutex);
    g_temporary.clear();
}

void* g_pendingMenu = nullptr;
int g_waitModalId = 0;
bool g_waitOpen = false;  // false: closed by us (its callback will be ignored)
// Window to show on the next frame (after the waiting window closes).
struct Queued {
    void* menu = nullptr;
    Modal kind = Modal::Info;
    std::wstring body;
};
Queued g_queued;

int* WaitIdField(void* menu) {
    return reinterpret_cast<int*>(static_cast<std::uint8_t*>(menu) + kAccountWaitModalId);
}

void CloseWait(void* menu) {
    g_waitOpen = false;
    if (AccountMenuOpen(menu) && *WaitIdField(menu) == g_waitModalId && g_waitModalId) {
        Fn<CloseModal_t>(kRvaMenuCloseModal)(menu, nullptr, WaitIdField(menu));
    }
}

Msg FailureMessage(apclient::Failure f) {
    switch (f) {
        case apclient::Failure::InvalidSlot:
            return kInvalidSlot;
        case apclient::Failure::InvalidGame:
            return kInvalidGame;
        case apclient::Failure::InvalidPassword:
            return kInvalidPassword;
        case apclient::Failure::IncompatibleVersion:
            return kIncompatibleVersion;
        case apclient::Failure::Refused:
            return kRefused;
        default:
            return kUnreachable;
    }
}

void OnConnectPressed(void* menu) {
    const std::wstring address = ReadField(menu, kRvaNameAccountEmail);
    const std::wstring password = ReadField(menu, kRvaNameAccountPassword);
    apclient::Address a;
    if (!apclient::ParseAddress(Utf8(address), &a)) {
        Log("apmenu: invalid address \"%s\"", Utf8(address).c_str());
        ShowModal(menu, Modal::Error, g_language->text[kBadAddress]);
        return;
    }
    apclient::Connect(a, Utf8(password));
    ShowModal(menu, Modal::Wait);
    g_pendingMenu = menu;
    g_waitModalId = *WaitIdField(menu);
    g_waitOpen = g_waitModalId != 0;
}

void OnDisconnectPressed(void* menu) {
    if (apclient::GetState() == apclient::State::Idle) {
        ShowModal(menu, Modal::Info, Text(kNotConnected));
        return;
    }
    apclient::Disconnect();
    ShowModal(menu, Modal::Info, Text(kDisconnected));
}

// ARCHIPELAGO button (Y) of the main menu: straight to the login screen.
void __fastcall MainMenuOnYDetour(void* self, void* /*edx*/) {
    std::uint32_t flags = 0;
    ue3::ReadBytes(static_cast<const std::uint8_t*>(self) + kMainMenuCreditsFlags, &flags, sizeof(flags));
    if (flags & 3) return;  // credits running
    Fn<FollowLink_t>(kRvaMenuFollowLink)(self, nullptr, kMainMenuAccountLink, 1, 1);
}

void __fastcall AccountAcceptDetour(void* self, void* edx) {
    void* list = Fn<FindWidget_t>(kRvaMenuFindList)(self, nullptr, Name(kRvaNameAccountList));
    const int row = list ? (Fn<SelectedIndex_t>(kRvaListSelectedIndex)(list, nullptr) & 0xFF) : -1;
    if (row == kAccountRowConnect) {
        OnConnectPressed(self);
    } else if (row == kAccountRowCreate) {
        OnDisconnectPressed(self);
    } else {
        g_accountAccept(self, edx);
    }
}

// Closing OUR waiting window: by us (ignored) or by Cancel (we give up).
void __fastcall WaitCallbackDetour(void* self, void* edx, int modalId, int result, int index) {
    if (!g_waitModalId || modalId != g_waitModalId) {
        g_waitCallback(self, edx, modalId, result, index);
        return;
    }
    if (*WaitIdField(self) == modalId) *WaitIdField(self) = 0;
    g_waitModalId = 0;
    if (g_waitOpen) {
        g_waitOpen = false;
        g_pendingMenu = nullptr;
        apclient::Disconnect();
        Log("apmenu: connection cancelled");
    }
}

bool Hook(std::uintptr_t rva, void* detour, void** original) {
    void* target = reinterpret_cast<void*>(Base() + rva);
    return MH_CreateHook(target, detour, original) == MH_OK && MH_EnableHook(target) == MH_OK;
}

bool InstallHooks() {
    const bool ok =
        Hook(kRvaLocalize, reinterpret_cast<void*>(&LocalizeDetour), reinterpret_cast<void**>(&g_localize)) &&
        Hook(kRvaMainMenuOnY, reinterpret_cast<void*>(&MainMenuOnYDetour), reinterpret_cast<void**>(&g_mainMenuOnY)) &&
        Hook(kRvaAccountAcceptOrSelect, reinterpret_cast<void*>(&AccountAcceptDetour),
             reinterpret_cast<void**>(&g_accountAccept)) &&
        Hook(kRvaAccountWaitCallback, reinterpret_cast<void*>(&WaitCallbackDetour),
             reinterpret_cast<void**>(&g_waitCallback));
    Log(ok ? "apmenu: Archipelago login screen in place" : "apmenu: ERROR, cannot hook the menus");
    return ok;
}

// "2K ACCOUNT" / "LOG IN" / "UNLINK 2K ACCOUNT" -> ARCHIPELAGO (Y button).
void RelabelHints(const void* context) {
    static const void* cls = nullptr;
    if (!ue3::IsObject(cls)) cls = ue3::FindClass("XMenuLevelUtilityScreen");
    const std::wstring label = L"ARCHIPELAGO";
    ue3::ForEachObjectOfClass(cls, [&](int, const void* screen) {
        if (ue3::ObjectName(screen).rfind("Default__", 0) == 0) return;
        for (const char* prop :
             {"TwoKLoginButtonHintEntries", "TwoKLinkButtonHintEntries", "TwoKUnlinkButtonHintEntries"}) {
            ue3::Property p;
            ue3::ArrayHeader entries{};
            FString current{};
            if (!ue3::FindProperty(screen, prop, &p) ||
                !ue3::ReadBytes(static_cast<const std::uint8_t*>(screen) + p.offset, &entries, sizeof(entries)) ||
                entries.num < 1 || !ue3::ReadBytes(entries.data, &current, sizeof(current))) {
                continue;
            }
            std::wstring text(static_cast<std::size_t>(current.num > 0 && current.num < 64 ? current.num : 0), L'\0');
            if (!text.empty()) ue3::ReadBytes(current.data, text.data(), text.size() * sizeof(wchar_t));
            if (text.c_str() == label) continue;
            alloc::FStringHeader fresh{};
            if (alloc::NewString(context, label, &fresh)) ue3::WriteBytes(entries.data, &fresh, sizeof(fresh));
        }
    });
}

std::set<void*> g_openMenus;

// The main menu's 2K box: logos hidden, Archipelago state instead of "NOT LINKED". Reapplied
// regularly since the game updates it.
void UpdateMenuBadge() {
    static const void* cls = nullptr;
    if (!ue3::IsObject(cls)) cls = ue3::FindClass("XMenuLevelUtilityScreen");
    const apclient::State state = apclient::GetState();
    const std::wstring status = Text(state == apclient::State::Connected ? kStatusConnected
                                     : state == apclient::State::Idle    ? kStatusIdle
                                                                         : kStatusRetry);
    const void* gfxClass = *reinterpret_cast<const void* const*>(Base() + kRvaGFxObjectClass);
    ue3::ForEachObjectOfClass(cls, [&](int, const void* screen) {
        if (ue3::PathName(screen).rfind("Transient.", 0) != 0 || !MovieOpen(screen)) return;
        const auto field = [&](std::size_t offset) {
            void* obj = nullptr;
            ue3::ReadBytes(static_cast<const std::uint8_t*>(screen) + offset, &obj, sizeof(obj));
            return ue3::IsObject(obj) && ue3::ClassName(obj) == "GFxObject" ? obj : nullptr;
        };
        for (const std::size_t offset : {kUtilityTwoKLoading, kUtilityTwoKLogo, kUtilityTwoKLogoOffline}) {
            if (void* logo = field(offset)) Fn<SetVisible_t>(kRvaGFxSetVisible)(logo, nullptr, 0);
        }
        void* widget = field(kUtilityTwoKWidget);
        void* label =
            widget && gfxClass ? Fn<GetObject_t>(kRvaGFxGetObject)(widget, nullptr, "textField", gfxClass) : nullptr;
        if (ue3::IsObject(label)) {
            std::wstring copy = status;
            const FString value{copy.data(), static_cast<std::int32_t>(copy.size() + 1),
                                static_cast<std::int32_t>(copy.size() + 1)};
            Fn<SetString_t>(kRvaGFxSetString)(label, nullptr, "text", &value);
        }
    });
}

void PrefillOpenMenus() {
    static std::set<void*> filled;
    static const void* cls = nullptr;
    if (!ue3::IsObject(cls)) cls = ue3::FindClass("XTwoKAccountMenu");
    std::set<void*> open;
    ue3::ForEachObjectOfClass(cls, [&](int, const void* obj) {
        void* menu = const_cast<void*>(obj);
        if (AccountMenuOpen(menu)) open.insert(menu);
    });
    g_openMenus = open;
    for (auto it = filled.begin(); it != filled.end();) {
        it = open.count(*it) ? std::next(it) : filled.erase(it);
    }
    std::string address, password;
    apclient::Saved(&address, &password);
    for (void* menu : open) {
        if (filled.count(menu)) continue;
        if (address.empty() || (WriteField(menu, kRvaNameAccountEmail, Wide(address)) &&
                                WriteField(menu, kRvaNameAccountPassword, Wide(password)))) {
            filled.insert(menu);
        }
    }
}

// Auto-connect: the screen has no check box widget, so a third button hint ("[X] AUTO-CONNECT", Y
// or click) follows SELECT and BACK. The game draws it with the right key for keyboard or
// controller.

// Element of XSequentialMenu.ButtonHintEntries. CommandName is shown as is (it is localized when
// the menu is loaded, not when drawn).
struct HintEntry {
    alloc::FStringHeader commandName;
    std::int32_t inputAlias[2];  // FName
    std::int32_t additionalInputAlias[2];
    std::uint32_t flags;  // 1 bUseCommandNameInsteadOfAliasLocalizedName, 2 bDisableMouseClickable
    alloc::FStringHeader textToAppend;
};
static_assert(sizeof(HintEntry) == 0x2C, "layout of a button hint entry");

bool g_autoConnect = true;
bool g_hintChanged = false;  // toggled: update the hint on the next frame

std::wstring AutoConnectLabel() {
    return std::wstring(g_autoConnect ? L"[X] " : L"[  ] ") + g_language->text[kAutoConnect];
}

std::int32_t YButtonAlias() {
    static const std::int32_t alias = ue3::FindName("UI Generic Y/Triangle Button");
    return alias;
}

void RefreshHints(void* menu) {
    if (const void* update = ue3::FindFunction(menu, "OnButtonHintUpdateRequired")) {
        std::uint8_t noParms[16] = {};
        engine::CallRaw(menu, update, noParms);
    }
}

void UpdateAutoConnectHint(const void* context, void* menu) {
    ue3::Property p;
    if (YButtonAlias() < 0 || !ue3::FindProperty(menu, "ButtonHintEntries", &p)) return;
    void* arrayAt = static_cast<std::uint8_t*>(menu) + p.offset;
    ue3::ArrayHeader h{};
    if (!ue3::ReadBytes(arrayAt, &h, sizeof(h)) || h.num < 1 || h.num > 8 || h.num > h.max) return;
    std::vector<HintEntry> entries(static_cast<std::size_t>(h.num));
    if (!ue3::ReadBytes(h.data, entries.data(), entries.size() * sizeof(HintEntry))) return;
    // The layout is checked on the game's own first hint (SELECT) before writing anything.
    if (ue3::NameString(entries[0].inputAlias[0]) != "UI Accept / Select") {
        static bool logged = false;
        if (!logged) Log("apmenu: unexpected button hint layout, no auto-connect hint");
        logged = true;
        return;
    }
    const std::wstring label = AutoConnectLabel();
    alloc::FStringHeader text{};
    for (int i = 0; i < h.num; ++i) {
        HintEntry& e = entries[static_cast<std::size_t>(i)];
        if (e.inputAlias[0] != YButtonAlias()) continue;
        std::wstring current(static_cast<std::size_t>(e.commandName.num > 0 ? e.commandName.num - 1 : 0), L'\0');
        if (!current.empty()) ue3::ReadBytes(e.commandName.data, current.data(), current.size() * sizeof(wchar_t));
        if (current == label || !alloc::NewString(context, label, &text)) return;
        GameFree(e.commandName.data);
        ue3::WriteBytes(h.data + i * sizeof(HintEntry), &text, sizeof(text));
        RefreshHints(menu);
        return;
    }
    if (!alloc::EnsureArrayCapacity(context, arrayAt, sizeof(HintEntry), 1) ||
        !ue3::ReadBytes(arrayAt, &h, sizeof(h)) || !alloc::NewString(context, label, &text)) {
        return;
    }
    HintEntry ours{};
    ours.commandName = text;
    ours.inputAlias[0] = YButtonAlias();
    ours.flags = 1;
    ue3::WriteBytes(h.data + h.num * sizeof(HintEntry), &ours, sizeof(ours));
    ++h.num;
    ue3::WriteBytes(arrayAt, &h, sizeof(h));
    RefreshHints(menu);
}

void ToggleAutoConnect() {
    g_autoConnect = !g_autoConnect;
    g_hintChanged = true;
    apclient::SetAutoConnect(g_autoConnect);
}

void AutoConnectAtStart() {
    static bool done = false;
    if (done) return;
    done = true;
    if (g_autoConnect && apclient::GetState() == apclient::State::Idle && apclient::ConnectSaved()) {
        Log("apmenu: auto-connect with the saved login");
    }
}

// Ctrl+V / Shift+Insert: the game's Flash can't reach the Windows clipboard, so we append to the
// selected field ourselves (row 0 address, 1 password).
std::wstring ClipboardText() {
    std::wstring text;
    if (!OpenClipboard(nullptr)) return text;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* data = static_cast<const wchar_t*>(GlobalLock(h))) {
            text = data;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    for (wchar_t& c : text) {
        if (c < 0x20) c = L' ';  // line breaks, tabs
    }
    const std::size_t first = text.find_first_not_of(L' '), last = text.find_last_not_of(L' ');
    return first == std::wstring::npos ? std::wstring() : text.substr(first, last - first + 1);
}

bool GameHasFocus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

void HandlePaste() {
    static bool wasDown = false;
    const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool down = (ctrl && (GetAsyncKeyState('V') & 0x8000)) || (shift && (GetAsyncKeyState(VK_INSERT) & 0x8000));
    const bool pressed = down && !wasDown;
    wasDown = down;
    if (!pressed || g_openMenus.empty() || !GameHasFocus()) return;
    for (void* menu : g_openMenus) {
        if (!AccountMenuOpen(menu)) continue;
        void* list = Fn<FindWidget_t>(kRvaMenuFindList)(menu, nullptr, Name(kRvaNameAccountList));
        const int row = list ? (Fn<SelectedIndex_t>(kRvaListSelectedIndex)(list, nullptr) & 0xFF) : -1;
        const std::uintptr_t field = row == 0 ? kRvaNameAccountEmail : row == 1 ? kRvaNameAccountPassword : 0;
        const std::wstring paste = field ? ClipboardText() : std::wstring();
        if (!paste.empty()) WriteField(menu, field, ReadField(menu, field) + paste);
    }
}

void FollowPendingConnection() {
    if (g_queued.menu) {
        if (AccountMenuOpen(g_queued.menu)) ShowModal(g_queued.menu, g_queued.kind, g_queued.body);
        g_queued = {};
    }
    if (!g_pendingMenu) return;
    const apclient::State state = apclient::GetState();
    if (state == apclient::State::Connecting) return;
    void* menu = g_pendingMenu;
    g_pendingMenu = nullptr;
    CloseWait(menu);
    if (!AccountMenuOpen(menu)) return;
    if (state == apclient::State::Connected) {
        g_queued = {menu, Modal::Success, Text(kConnected)};
    } else {
        g_queued = {menu, Modal::Error, Text(FailureMessage(apclient::LastFailure()))};
    }
}

}  // namespace

void Tick(const void* context) {
    static bool installed = false, failed = false;
    if (!installed && !failed) {
        DetectLanguage();
        {
            std::lock_guard<std::mutex> lock(g_overrideMutex);
            g_fixed[L"XTwoKAccountMenu/SA01_TITLE_1"] = L"ARCHIPELAGO";
            g_fixed[L"DNAGeneral/FORM_EMAIL"] = g_language->text[kAddressLabel];
            g_fixed[L"XTwoKAccountMenu/SA01_BUTTON_1"] = g_language->text[kDisconnectButton];
        }
        installed = InstallHooks();
        failed = !installed;
        g_autoConnect = apclient::AutoConnectEnabled();
    }
    if (!installed) return;
    AutoConnectAtStart();
    aptexture::ReplaceAccountEmblem(context);
    FollowPendingConnection();
    HandlePaste();
    static DWORD last = 0;
    const DWORD now = GetTickCount();
    if (now - last >= 500 || g_hintChanged) {
        last = now;
        g_hintChanged = false;
        PrefillOpenMenus();
        for (void* menu : g_openMenus) UpdateAutoConnectHint(context, menu);
        static int slow = 0;
        if (++slow % 2 == 0) RelabelHints(context);
        UpdateMenuBadge();
    }
}

bool Intercept(void* self, const void* func) {
    static std::unordered_map<const void*, bool> isY;  // per UFunction (several classes declare one)
    auto it = isY.find(func);
    if (it == isY.end()) it = isY.emplace(func, ue3::ObjectName(func) == "OnYTriangleButtonPress").first;
    if (!it->second || ue3::ClassName(self) != "XTwoKAccountMenu") return false;
    ToggleAutoConnect();
    return true;  // the 2K code of this key is never called
}

const char* LanguageCode() {
    return g_language ? g_language->code : "INT";
}

void SetText(const std::wstring& section, const std::wstring& key, const std::wstring& text) {
    std::lock_guard<std::mutex> lock(g_overrideMutex);
    if (text.empty()) {
        g_fixed.erase(section + L"/" + key);
    } else {
        g_fixed[section + L"/" + key] = text;
    }
}

}  // namespace bsap::apmenu
