#include "ap_notify.h"

#include "ap_menu.h"
#include "engine_call.h"
#include "log.h"
#include "ue3.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>

namespace bsap::apnotify {

namespace {

using Kind = apclient::Notice::Kind;

constexpr std::size_t kMaxQueued = 20;
constexpr std::size_t kMaxItemsPerMessage = 4;
constexpr DWORD kDeathLinkDelayMs = 1500;  // the message waits for the respawn

// %1 = item, %2 = player, %3 = cause.
struct Texts {
    const char* code;
    const wchar_t* received;  // prefix of a list of received items
    const wchar_t* from;      // after an item received from another player
    const wchar_t* sent;
    const wchar_t* found;
    const wchar_t* deathCause;
    const wchar_t* deathNoCause;
    const wchar_t* goal;
    const wchar_t* lost;
    const wchar_t* reconnected;
    const wchar_t* infusionPrompt;
    const wchar_t* noShield;
    const wchar_t* tearsLocked;
    const wchar_t* passLocked;  // %1 = pass
    const wchar_t* lockpickLocked;
};

const Texts kTexts[] = {
    {"INT", L"Received: ", L" (from %2)", L"Sent %1 to %2", L"Found: %1", L"Killed by DeathLink (%2): %3",
     L"Killed by DeathLink: %2 died.", L"Goal complete! Your Archipelago game is finished.",
     L"Lost connection to Archipelago, reconnecting...", L"Reconnected to Archipelago.",
     L"Infusion received! Choose: F9 = Health, F10 = Shield, F11 = Salts",
     L"You don't have your shield yet: choose Health or Salts.",
     L"Elizabeth can't open Tears yet: find the Elizabeth's Tears item.", L"You can't go further yet: you need the %1.",
     L"Elizabeth can't pick locks yet: find the Lockpicking item."},
    {"FRA", L"Reçu : ", L" (de %2)", L"Envoyé : %1 à %2", L"Trouvé : %1", L"Tué par DeathLink (%2) : %3",
     L"Tué par DeathLink : %2 est mort.", L"Objectif atteint ! Ta partie Archipelago est terminée.",
     L"Connexion à Archipelago perdue, reconnexion...", L"Reconnecté à Archipelago.",
     L"Infusion reçue ! Choisis : F9 = Santé, F10 = Bouclier, F11 = Sels",
     L"Tu n'as pas encore ton bouclier : choisis Santé ou Sels.",
     L"Elizabeth ne peut pas encore ouvrir de failles : trouve l'objet Elizabeth's Tears.",
     L"Tu ne peux pas encore aller plus loin : il te faut le %1.",
     L"Elizabeth ne peut pas encore crocheter : trouve l'objet Lockpicking."},
    {"DEU", L"Erhalten: ", L" (von %2)", L"Gesendet: %1 an %2", L"Gefunden: %1", L"Durch DeathLink getötet (%2): %3",
     L"Durch DeathLink getötet: %2 ist gestorben.", L"Ziel erreicht! Dein Archipelago-Spiel ist abgeschlossen.",
     L"Verbindung zu Archipelago verloren, verbinde neu...", L"Wieder mit Archipelago verbunden.",
     L"Infusion erhalten! Wähle: F9 = Gesundheit, F10 = Schild, F11 = Salze",
     L"Du hast noch keinen Schild: Wähle Gesundheit oder Salze.",
     L"Elizabeth kann noch keine Risse öffnen: Finde den Gegenstand Elizabeth's Tears.",
     L"Du kannst noch nicht weiter: Du brauchst den %1.",
     L"Elizabeth kann noch keine Schlösser knacken: Finde den Gegenstand Lockpicking."},
    {"ITA", L"Ricevuto: ", L" (da %2)", L"Inviato: %1 a %2", L"Trovato: %1", L"Ucciso dal DeathLink (%2): %3",
     L"Ucciso dal DeathLink: %2 è morto.", L"Obiettivo raggiunto! La tua partita Archipelago è finita.",
     L"Connessione ad Archipelago persa, riconnessione...", L"Riconnesso ad Archipelago.",
     L"Infusione ricevuta! Scegli: F9 = Salute, F10 = Scudo, F11 = Sali",
     L"Non hai ancora lo scudo: scegli Salute o Sali.",
     L"Elizabeth non può ancora aprire gli squarci: trova l'oggetto Elizabeth's Tears.",
     L"Non puoi ancora proseguire: ti serve il %1.",
     L"Elizabeth non può ancora scassinare: trova l'oggetto Lockpicking."},
    {"ESN", L"Recibido: ", L" (de %2)", L"Enviado: %1 a %2", L"Encontrado: %1", L"Muerto por DeathLink (%2): %3",
     L"Muerto por DeathLink: %2 ha muerto.", L"¡Objetivo cumplido! Tu partida de Archipelago ha terminado.",
     L"Conexión con Archipelago perdida, reconectando...", L"Reconectado a Archipelago.",
     L"¡Infusión recibida! Elige: F9 = Salud, F10 = Escudo, F11 = Sales",
     L"Aún no tienes el escudo: elige Salud o Sales.",
     L"Elizabeth aún no puede abrir desgarros: encuentra el objeto Elizabeth's Tears.",
     L"Aún no puedes seguir: necesitas el %1.",
     L"Elizabeth aún no puede forzar cerraduras: encuentra el objeto Lockpicking."},
};

const Texts& Lang() {
    const char* code = apmenu::LanguageCode();
    for (const Texts& t : kTexts) {
        if (std::strcmp(t.code, code) == 0) return t;
    }
    return kTexts[0];
}

struct Queued {
    apclient::Notice notice;
    DWORD at;
};
std::deque<Queued> g_queue;
std::wstring g_current;  // ready, waiting to be shown
DWORD g_nextAt = 0;
bool g_prompt = false;
bool g_promptTurn = true;

// Latin-1 is kept, the rest decomposed (Ã© -> e) or "?". Brackets are for keys ([Jump]...), so they
// become parentheses.
std::wstring GameText(const std::string& utf8) {
    std::wstring wide;
    if (!utf8.empty()) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
        wide.resize(static_cast<std::size_t>(n));
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), n);
    }
    std::wstring out;
    for (const wchar_t c : wide) {
        if (c == L'[')
            out.push_back(L'(');
        else if (c == L']')
            out.push_back(L')');
        else if (c < 0x20)
            out.push_back(L' ');
        else if (c <= 0xFF && c != 0x7F)
            out.push_back(c);
        else {
            wchar_t decomposed[8] = {};
            const int n = NormalizeString(NormalizationKD, &c, 1, decomposed, 8);
            bool kept = false;
            for (int i = 0; i < n; ++i) {
                if (decomposed[i] >= 0x20 && decomposed[i] < 0x7F) {
                    out.push_back(decomposed[i]);
                    kept = true;
                }
            }
            if (!kept) out.push_back(L'?');
        }
    }
    if (out.size() > 120) out = out.substr(0, 117) + L"...";
    return out;
}

std::wstring Fill(std::wstring s, const apclient::Notice& n) {
    for (const auto& [key, value] : {std::pair<const wchar_t*, std::wstring>{L"%1", GameText(n.item)},
                                     {L"%2", GameText(n.player)},
                                     {L"%3", GameText(n.text)}}) {
        for (std::size_t i; (i = s.find(key)) != std::wstring::npos;) s.replace(i, 2, value);
    }
    return s;
}

std::wstring NextMessage() {
    const Texts& t = Lang();
    const apclient::Notice first = g_queue.front().notice;
    g_queue.pop_front();
    switch (first.kind) {
        case Kind::ItemReceived: {
            std::wstring text = t.received;
            std::size_t count = 0;
            for (apclient::Notice n = first;;) {
                text += (count++ ? L", " : L"") + GameText(n.item) + (n.player.empty() ? L"" : Fill(t.from, n));
                if (count >= kMaxItemsPerMessage || g_queue.empty() ||
                    g_queue.front().notice.kind != Kind::ItemReceived)
                    break;
                n = g_queue.front().notice;
                g_queue.pop_front();
            }
            return text;
        }
        case Kind::ItemSent:
            return Fill(t.sent, first);
        case Kind::ItemFound:
            return Fill(t.found, first);
        case Kind::DeathLink:
            return Fill(first.text.empty() ? t.deathNoCause : t.deathCause, first);
        case Kind::Goal:
            return t.goal;
        case Kind::ConnectionLost:
            return t.lost;
        case Kind::Reconnected:
            return t.reconnected;
    }
    return {};
}

// Booker in game and alive (no menu, loading or death screen).
bool PlayerReady(const void* controller) {
    ue3::Property p;
    std::string err;
    const void* owner = ue3::ResolvePath(controller, "Pawn", &p, &err);
    const void* pawn = owner ? ue3::ObjectField(owner, p) : nullptr;
    if (!pawn) return false;
    ue3::Property health, respawning;
    if (ue3::FindProperty(pawn, "XHealth", &health)) {
        float value = 0;
        ue3::ReadBytes(static_cast<const std::uint8_t*>(pawn) + health.offset, &value, sizeof(value));
        if (value <= 0.0f) return false;
    }
    if (ue3::FindProperty(controller, "bIsRespawning", &respawning) && respawning.boolMask) {
        std::uint32_t bits = 0;
        ue3::ReadBytes(static_cast<const std::uint8_t*>(controller) + respawning.offset, &bits, sizeof(bits));
        if (bits & respawning.boolMask) return false;
    }
    return true;
}

// The "Armor" tutorial, with no text in the game: our slot.
const void* MessageObject() {
    static const void* message = nullptr;
    if (ue3::IsObject(message) && ue3::ObjectName(message) == "Armor") return message;
    message = nullptr;
    if (const void* cls = ue3::FindClass("XAdaptiveTrainingMessage")) {
        ue3::ForEachObjectOfClass(cls, [](int, const void* obj) {
            if (!message && ue3::ObjectName(obj) == "Armor") message = obj;
        });
    }
    return message;
}

bool Display(const void* controller, const std::wstring& text, float seconds) {
    const void* message = MessageObject();
    const void* display = ue3::FindFunction(controller, "ClientDisplayAdaptiveTrainingMessage");
    if (!message || !display) return false;
    auto* bytes = const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(message));
    ue3::Property p;
    if (ue3::FindProperty(message, "HUDDisplaySeconds", &p))
        ue3::WriteBytes(bytes + p.offset, &seconds, sizeof(seconds));
    // The game won't repeat a tutorial it just showed: reset its count of shown ones.
    if (ue3::FindProperty(message, "CachedPlayersLastTimeTrainingMessageDelivered", &p)) {
        const std::int32_t zero = 0;
        ue3::WriteBytes(bytes + p.offset + 4, &zero, sizeof(zero));
    }
    apmenu::SetText(L"TrainingMessages", L"Armor", text);
    struct {
        const void* message;
    } parms{message};
    return engine::CallRaw(controller, display, &parms);
}

}  // namespace

void Push(const apclient::Notice& notice) {
    if (g_queue.size() >= kMaxQueued) g_queue.pop_front();
    g_queue.push_back({notice, GetTickCount()});
}

void SetInfusionPrompt(bool active) {
    if (active && !g_prompt) g_promptTurn = true;
    g_prompt = active;
}

void ShowNoShield() {
    g_current = Lang().noShield;
    g_nextAt = GetTickCount();
}

void ShowTearsLocked() {
    g_current = Lang().tearsLocked;
    g_nextAt = GetTickCount();
}

void ShowLockpickLocked() {
    g_current = Lang().lockpickLocked;
    g_nextAt = GetTickCount();
}

void ShowPassLocked(const wchar_t* pass) {
    std::wstring text = Lang().passLocked;
    if (const std::size_t at = text.find(L"%1"); at != std::wstring::npos) text.replace(at, 2, pass);
    g_current = text;
    g_nextAt = GetTickCount();
}

void Tick(const void* controller) {
    if (g_queue.empty() && g_current.empty() && !g_prompt) return;
    const DWORD now = GetTickCount();
    if (static_cast<int>(now - g_nextAt) < 0) return;
    if (g_prompt && g_current.empty() && (g_promptTurn || g_queue.empty())) {
        if (!PlayerReady(controller)) return;
        constexpr float kPromptSeconds = 8.0f;
        if (Display(controller, Lang().infusionPrompt, kPromptSeconds)) {
            g_promptTurn = false;
            g_nextAt = now + static_cast<DWORD>(kPromptSeconds * 1000.0f) + 500;
        } else {
            g_nextAt = now + 2000;
        }
        return;
    }
    g_promptTurn = true;
    if (g_queue.empty() && g_current.empty()) return;
    if (g_current.empty() && g_queue.front().notice.kind == Kind::DeathLink &&
        now - g_queue.front().at < kDeathLinkDelayMs)
        return;
    if (!PlayerReady(controller)) return;
    if (g_current.empty()) g_current = NextMessage();
    const std::wstring text = g_current;
    // Long enough to read: 4 s, plus 1 s per 30 characters, 9 s at most.
    const float seconds = std::min(9.0f, 4.0f + static_cast<float>(text.size()) / 30.0f);
    if (!Display(controller, text, seconds)) {
        Log("apnotify: cannot display (retrying in 2 s)");
        g_nextAt = now + 2000;
        return;
    }
    g_current.clear();
    Log("apnotify: %ls", text.c_str());
    g_nextAt = now + static_cast<DWORD>(seconds * 1000.0f) + 500;
}

}  // namespace bsap::apnotify
