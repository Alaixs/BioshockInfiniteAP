#include "overlay.h"

#include "ap_client.h"
#include "ap_screamer.h"
#include "collect.h"
#include "engine_call.h"
#include "game_build.h"
#include "log.h"
#include "traps.h"
#include "ue3.h"
#include "world.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace bsap::overlay {

namespace {

constexpr DWORD kRefreshMs = 300;
constexpr float kMarkerRange = 6000.0f;   // 60 m; the selected target is framed at any distance
constexpr float kFallbackExtent = 25.0f;  // half size of a frame for an actor without bounding box
constexpr float kMinFrame = 28.0f;
constexpr std::size_t kListedTargets = 8;
constexpr double kPi = 3.14159265358979;

enum class Kind { Voxophone, Infusion, Gear, Machine };

// Checks left to do nearby.
struct Target {
    Kind kind;
    const void* actor;
    std::string label;   // "Voxophone JF_A_Fink_Solution"
    std::string status;  // "to pick up", "2 AP row(s)"...
    world::Position pos;
    world::Box box;
    float distance;
};

bool g_visible = true;
const void* g_postRender = nullptr;        // GameViewportClient.PostRender (panel, scream picture)
const void* g_playerPostRender = nullptr;  // XPlayerController.PostRender: the player's view (frames)
bool g_hasPlayer = false;
world::Position g_player{};
std::string g_maps;
std::vector<Target> g_targets;
int g_selected = 0;  // index of the selected target (F7)
std::string g_flash;
DWORD g_flashUntil = 0;
// Test helpers for reaching places quickly.
bool g_god = false;
bool g_oneHit = false;

std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string AreaOf(const std::string& maps) {
    static const std::pair<const char*, const char*> kAreas[] = {
        {"S_TWN", "Town Center"}, {"S_LizT", "Monument Island"}, {"S_BW", "Battleship Bay"},
        {"S_Fink", "Finkton"},    {"S_EMP", "Emporia"},          {"S_CHU", "Hand of the Prophet"},
    };
    for (const auto& [prefix, area] : kAreas) {
        if (_strnicmp(maps.c_str(), prefix, std::strlen(prefix)) == 0) return area;
    }
    return "?";
}

// Direction of a target seen from the player, as a clock (12 = ahead, 3 = right).
int ClockOf(const world::Position& from, const world::Position& to) {
    const double bearing = std::atan2(to.y - from.y, to.x - from.x) * 180.0 / kPi;
    const double relative = std::fmod(bearing - from.yaw + 720.0, 360.0);
    const int hour = static_cast<int>(std::lround(relative / 30.0)) % 12;
    return hour == 0 ? 12 : hour;
}

bool GameHasFocus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

bool KeyPressed(int vk) {
    static bool down[256] = {};
    const bool now = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool pressed = now && !down[vk];
    down[vk] = now;
    return pressed;
}

const wchar_t* KindLabel(Kind kind) {
    switch (kind) {
        case Kind::Voxophone:
            return L"Voxophone";
        case Kind::Infusion:
            return L"Infusion";
        case Kind::Gear:
            return L"Gear";
        default:
            return L"Machine";
    }
}

struct Rgb {
    std::uint8_t r, g, b;
};

Rgb KindColor(Kind kind) {
    switch (kind) {
        case Kind::Voxophone:
            return {255, 170, 0};
        case Kind::Infusion:
            return {255, 60, 200};
        case Kind::Gear:
            return {60, 255, 120};
        default:
            return {60, 200, 255};
    }
}

void Flash(const std::string& text) {
    g_flash = text;
    g_flashUntil = GetTickCount() + 3000;
}

const Target* SelectedTarget() {
    return g_targets.empty() ? nullptr : &g_targets[static_cast<std::size_t>(g_selected) % g_targets.size()];
}

void AddMachines(std::vector<Target>& out) {
    static const void* cls = nullptr;
    if (!ue3::IsObject(cls)) cls = ue3::FindClass("XVendingMachine");
    ue3::ForEachObjectOfClass(cls, [&](int, const void* machine) {
        if (ue3::ClassName(ue3::OuterOf(machine)) != "Level") return;
        const std::string path = ue3::PathName(machine);
        const int remaining = apclient::MachineRemaining(path);
        world::Position pos{};
        if (remaining <= 0 || !world::ActorPosition(machine, &pos)) return;
        out.push_back({Kind::Machine,
                       machine,
                       "Machine " + path.substr(0, path.find('.')),
                       std::to_string(remaining) + " AP row(s)",
                       pos,
                       {},
                       0});
    });
}

void AddPickups(std::vector<Target>& out, Kind kind, const char* label, const std::vector<collect::Pickup>& pickups,
                apclient::LocationStatus (*statusOf)(const std::string&)) {
    for (const collect::Pickup& v : pickups) {
        world::Position pos{};
        if (v.state == collect::kLootableStateLooted || statusOf(v.id) != apclient::LocationStatus::ToDo ||
            !world::ActorPosition(v.actor, &pos)) {
            continue;
        }
        out.push_back({kind, v.actor, std::string(label) + " " + v.id, "to pick up", pos, {}, 0});
    }
}

void Refresh(const void* controller) {
    g_hasPlayer = world::PlayerPosition(controller, &g_player);
    g_maps = g_hasPlayer ? world::LoadedMaps() : "-";
    g_targets.clear();
    if (!g_hasPlayer) return;
    AddPickups(g_targets, Kind::Voxophone, "Voxophone", collect::Voxophones(), apclient::VoxophoneStatus);
    AddPickups(g_targets, Kind::Infusion, "Infusion", collect::Infusions(), apclient::InfusionStatus);
    AddPickups(g_targets, Kind::Gear, "Gear", collect::Gear(), apclient::GearStatus);
    AddMachines(g_targets);
    for (Target& t : g_targets) {
        const float dx = t.pos.x - g_player.x, dy = t.pos.y - g_player.y, dz = t.pos.z - g_player.z;
        t.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!world::ActorBounds(t.actor, &t.box)) {
            t.box = {t.pos.x, t.pos.y, t.pos.z, kFallbackExtent, kFallbackExtent, kFallbackExtent};
        }
    }
    std::sort(g_targets.begin(), g_targets.end(),
              [](const Target& a, const Target& b) { return a.distance < b.distance; });
}

class Call {
public:
    Call(const void* obj, const char* func) : obj_(obj) {
        static std::map<std::string, Layout> layouts;
        auto it = layouts.find(func);
        if (it == layouts.end() || !ue3::IsObject(it->second.func)) {
            Layout l;
            l.func = ue3::FindFunction(obj, func);
            if (l.func) ue3::ForEachOwnProperty(l.func, [&](const ue3::Property& p) { l.params[p.name] = p; });
            it = layouts.insert_or_assign(func, std::move(l)).first;
        }
        layout_ = &it->second;
    }
    template <typename T>
    Call& Set(const char* name, const T& value) {
        const auto it = layout_->params.find(name);
        if (it != layout_->params.end() && it->second.offset + sizeof(T) <= sizeof(parms_)) {
            std::memcpy(parms_ + it->second.offset, &value, sizeof(T));
        }
        return *this;
    }
    Call& SetBool(const char* name, bool value) {
        const auto it = layout_->params.find(name);
        if (it != layout_->params.end()) {
            const std::uint32_t bits = value ? (it->second.boolMask ? it->second.boolMask : 1u) : 0u;
            std::memcpy(parms_ + it->second.offset, &bits, 4);
        }
        return *this;
    }
    template <typename T>
    T Get(const char* name) const {
        T value{};
        const auto it = layout_->params.find(name);
        if (it != layout_->params.end()) std::memcpy(&value, parms_ + it->second.offset, sizeof(T));
        return value;
    }
    bool Run() { return layout_->func && engine::CallRaw(obj_, layout_->func, parms_); }

private:
    struct Layout {
        const void* func = nullptr;
        std::map<std::string, ue3::Property> params;
    };
    const void* obj_;
    const Layout* layout_;
#pragma warning(suppress : 4324)  // the padding is the point: parameters aligned as the game expects
    alignas(16) std::uint8_t parms_[256] = {};
};

struct FStr {
    const wchar_t* data;
    std::int32_t num, max;
};

struct Vec3f {
    float x, y, z;
};

class Painter {
public:
    explicit Painter(const void* canvas) : canvas_(canvas) {}

    void Color(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
        Call(canvas_, "SetDrawColor").Set("R", r).Set("G", g).Set("B", b).Set("A", a).Run();
    }
    void Pos(float x, float y) { Call(canvas_, "SetPos").Set("PosX", x).Set("PosY", y).Set("PosZ", 1.0f).Run(); }
    void Rect(float x, float y, float w, float h) {
        Pos(x, y);
        Call(canvas_, "DrawRect").Set("RectX", w).Set("RectY", h).Run();
    }
    // t pixels thick, inwards: this Canvas draws no lines.
    void Frame(float x, float y, float w, float h, float t) {
        Rect(x, y, w, t);
        Rect(x, y + h - t, w, t);
        Rect(x, y + t, t, h - 2 * t);
        Rect(x + w - t, y + t, t, h - 2 * t);
    }
    void Text(float x, float y, const std::wstring& text) {
        Pos(x, y);
        const FStr s{text.c_str(), static_cast<std::int32_t>(text.size() + 1),
                     static_cast<std::int32_t>(text.size() + 1)};
        Call(canvas_, "DrawText").Set("Text", s).SetBool("CR", false).Set("XScale", 1.0f).Set("YScale", 1.0f).Run();
    }
    void Image(const void* texture, float x, float y, float w, float h, float texW, float texH, float opacity) {
        struct LinearColor {
            float r, g, b, a;
        };
        Pos(x, y);
        Call(canvas_, "DrawTile")
            .Set("Tex", texture)
            .Set("XL", w)
            .Set("YL", h)
            .Set("U", 0.0f)
            .Set("V", 0.0f)
            .Set("UL", texW)
            .Set("VL", texH)
            .Set("LColor", LinearColor{1.0f, 1.0f, 1.0f, opacity})
            .Set("Blend", static_cast<std::uint8_t>(2))  // BLEND_Translucent: opacity counts
            .Run();
    }
    float Width(const std::wstring& text) {
        const FStr s{text.c_str(), static_cast<std::int32_t>(text.size() + 1),
                     static_cast<std::int32_t>(text.size() + 1)};
        Call c(canvas_, "StrLen");
        c.Set("String", s).Run();
        return c.Get<float>("XL");
    }
    void Project(const Vec3f& p, float* x, float* y) {
        Call c(canvas_, "Project");
        c.Set("Location", p).Run();
        const Vec3f r = c.Get<Vec3f>("ReturnValue");
        *x = r.x;
        *y = r.y;
    }
    void DeProject(float x, float y, Vec3f* origin, Vec3f* dir) {
        struct Vec2f {
            float x, y;
        };
        Call c(canvas_, "DeProject");
        c.Set("ScreenPos", Vec2f{x, y}).Run();
        *origin = c.Get<Vec3f>("WorldOrigin");
        *dir = c.Get<Vec3f>("WorldDirection");
    }

private:
    const void* canvas_;
};

bool ScreenSize(const void* canvas, float* width, float* height) {
    ue3::Property p;
    *width = *height = 0;
    if (ue3::FindProperty(canvas, "ClipX", &p))
        ue3::ReadBytes(static_cast<const std::uint8_t*>(canvas) + p.offset, width, 4);
    if (ue3::FindProperty(canvas, "ClipY", &p))
        ue3::ReadBytes(static_cast<const std::uint8_t*>(canvas) + p.offset, height, 4);
    return *width > 0 && *height > 0;
}

float Dot(const Vec3f& a, const Vec3f& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Camera and screen size. Only valid during the player's PostRender: in the viewport's,
// Project/DeProject return nothing.
struct View {
    Vec3f origin, dir;
    float width, height;
};

bool MakeView(Painter& paint, const void* canvas, View* v) {
    if (!ScreenSize(canvas, &v->width, &v->height)) return false;
    paint.DeProject(v->width / 2, v->height / 2, &v->origin, &v->dir);
    return Dot(v->dir, v->dir) > 0.5f;
}

// Rectangle around the 8 projected corners, at least kMinFrame. False if a corner is behind the
// camera or the frame is off screen.
bool ScreenFrame(Painter& paint, const View& view, const world::Box& b, float* x, float* y, float* w, float* h) {
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (int i = 0; i < 8; ++i) {
        const Vec3f c{b.x + (i & 1 ? b.ex : -b.ex), b.y + (i & 2 ? b.ey : -b.ey), b.z + (i & 4 ? b.ez : -b.ez)};
        const Vec3f d{c.x - view.origin.x, c.y - view.origin.y, c.z - view.origin.z};
        if (!(Dot(d, view.dir) > 1.0f)) return false;
        float sx = 0, sy = 0;
        paint.Project(c, &sx, &sy);
        minX = std::min(minX, sx);
        minY = std::min(minY, sy);
        maxX = std::max(maxX, sx);
        maxY = std::max(maxY, sy);
    }
    if (maxX < 0 || maxY < 0 || minX > view.width || minY > view.height) return false;
    const float cx = (minX + maxX) / 2, cy = (minY + maxY) / 2;
    *w = std::max(maxX - minX, kMinFrame);
    *h = std::max(maxY - minY, kMinFrame);
    *x = cx - *w / 2;
    *y = cy - *h / 2;
    return true;
}

std::wstring MarkerText(const Target& t) {
    wchar_t buf[160];
    if (t.kind == Kind::Machine) {
        std::swprintf(buf, 160, L"%ls: %ls  %.0f m", KindLabel(t.kind), Wide(t.status).c_str(), t.distance / 100.0f);
    } else {
        std::swprintf(buf, 160, L"%ls  %.0f m", KindLabel(t.kind), t.distance / 100.0f);
    }
    return buf;
}

// Pulsing fill, outline with a black border so it reads on sky and in the dark, name and distance
// above.
void DrawMarker(Painter& paint, const Target& t, float x, float y, float w, float h, bool selected) {
    const Rgb c = KindColor(t.kind);
    const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(GetTickCount() * 2.0 * kPi / 900.0));
    const float thick = selected ? 5.0f : 3.0f;
    paint.Color(c.r, c.g, c.b, static_cast<std::uint8_t>(30 + 60 * pulse));
    paint.Rect(x, y, w, h);
    paint.Color(0, 0, 0, 220);
    paint.Frame(x - 1, y - 1, w + 2, h + 2, thick + 2);
    paint.Color(c.r, c.g, c.b);
    paint.Frame(x, y, w, h, thick);

    const std::wstring text = (selected ? L"> " : L"") + MarkerText(t);
    const float tw = paint.Width(text);
    const float tx = x + w / 2 - tw / 2, ty = y - 26;
    paint.Color(0, 0, 0, 190);
    paint.Rect(tx - 5, ty - 2, tw + 10, 23);
    paint.Color(c.r, c.g, c.b);
    paint.Text(tx, ty, text);
}

// Selected target off screen: its name at the edge, on the side to turn to.
void DrawOffscreen(Painter& paint, const View& view, const Target& t) {
    const double turn = std::remainder(
        std::atan2(t.box.y - view.origin.y, t.box.x - view.origin.x) - std::atan2(view.dir.y, view.dir.x), 2 * kPi);
    const std::wstring label = MarkerText(t);
    std::wstring text;
    float x = 0, y = 0;
    if (std::fabs(turn) < 35 * kPi / 180) {  // ahead, but too high or too low
        const bool up = t.box.z > view.origin.z;
        text = up ? L"^^  " + label + L"  ^^" : L"vv  " + label + L"  vv";
        x = view.width / 2 - paint.Width(text) / 2;
        y = up ? 60.0f : view.height - 90.0f;
    } else {
        text = turn > 0 ? label + L"  >>" : L"<<  " + label;
        x = turn > 0 ? view.width - paint.Width(text) - 30.0f : 30.0f;
        y = view.height / 2;
    }
    const Rgb c = KindColor(t.kind);
    const float w = paint.Width(text);
    paint.Color(0, 0, 0, 200);
    paint.Rect(x - 8, y - 5, w + 16, 30);
    paint.Color(c.r, c.g, c.b);
    paint.Frame(x - 8, y - 5, w + 16, 30, 2);
    paint.Text(x, y, text);
}

const void* FindFont(const char* name) {
    static std::map<std::string, const void*> cache;
    const void*& font = cache[name];
    if (ue3::IsObject(font) && ue3::ObjectName(font) == name) return font;
    font = nullptr;
    if (const void* cls = ue3::FindClass("Font")) {
        ue3::ForEachObjectOfClass(cls, [&](int, const void* obj) {
            if (!font && ue3::ObjectName(obj) == name) font = obj;
        });
    }
    return font;
}

class FontScope {
public:
    explicit FontScope(const void* canvas)
        : bytes_(const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(canvas))) {
        const void* font = FindFont("MediumFont");
        ok_ = font && ue3::FindProperty(canvas, "Font", &prop_);
        if (ok_) {
            ue3::ReadBytes(bytes_ + prop_.offset, &old_, sizeof(old_));
            ue3::WriteBytes(bytes_ + prop_.offset, &font, sizeof(font));
        }
    }
    ~FontScope() {
        if (ok_) ue3::WriteBytes(bytes_ + prop_.offset, &old_, sizeof(old_));
    }
    FontScope(const FontScope&) = delete;
    FontScope& operator=(const FontScope&) = delete;

private:
    std::uint8_t* bytes_;
    ue3::Property prop_;
    const void* old_ = nullptr;
    bool ok_ = false;
};

std::wstring StatusLine() {
    const apclient::Address& a = apclient::CurrentAddress();
    const wchar_t* state = L"not connected";
    switch (apclient::GetState()) {
        case apclient::State::Connected:
            state = L"CONNECTED";
            break;
        case apclient::State::Connecting:
            state = L"connecting...";
            break;
        case apclient::State::Reconnecting:
            state = L"RECONNECTING...";
            break;
        default:
            break;
    }
    wchar_t buf[200];
    std::swprintf(buf, 200, L"AP: %ls  %ls@%ls   locations: %zu done, %zu left", state, Wide(a.name).c_str(),
                  Wide(a.HostPort()).c_str(), apclient::CheckedCount(), apclient::MissingCount());
    return buf;
}

// Targets nearby are framed through walls; the selected one at any distance, with an edge marker
// when off screen.
void DrawMarkers(const void* canvas) {
    Painter paint(canvas);
    View view{};
    if (!g_hasPlayer || !MakeView(paint, canvas, &view)) return;
    const FontScope font(canvas);
    const Target* selected = SelectedTarget();
    for (const Target& t : g_targets) {
        if (t.distance > kMarkerRange && &t != selected) continue;
        float x = 0, y = 0, w = 0, h = 0;
        if (ScreenFrame(paint, view, t.box, &x, &y, &w, &h)) {
            DrawMarker(paint, t, x, y, w, h, &t == selected);
        } else if (&t == selected) {
            DrawOffscreen(paint, view, t);
        }
    }
}

void DrawPanel(const void* canvas) {
    Painter paint(canvas);
    const FontScope font(canvas);
    const Target* selected = SelectedTarget();
    std::vector<std::wstring> lines;
    lines.push_back(L"BioShockAP debug    [F6] hide   [F7] next target   [F8] teleport");
    lines.push_back(std::wstring(L"[F4] infinite health: ") + (g_god ? L"ON" : L"off") + L"   [F5] one-hit enemies: " +
                    (g_oneHit ? L"ON" : L"off"));
    const std::wstring traps = traps::ActiveSummary();
    lines.push_back(
        L"Traps [Ctrl+1] disarm  [Ctrl+2] jam  [Ctrl+3] no Vigor  [Ctrl+4] Elizabeth strike  [Ctrl+5] walk  [Ctrl+6] "
        L"scream  [Ctrl+0] stop" +
        (traps.empty() ? std::wstring() : L"   ACTIVE: " + traps));
    lines.push_back(StatusLine());
    if (g_hasPlayer) {
        wchar_t buf[200];
        std::swprintf(buf, 200, L"Map: %ls  (%ls)", Wide(g_maps).c_str(), Wide(AreaOf(g_maps)).c_str());
        lines.push_back(buf);
        std::swprintf(buf, 200, L"Position: %.0f  %.0f  %.0f    heading %.0f", g_player.x, g_player.y, g_player.z,
                      g_player.yaw);
        lines.push_back(buf);
        lines.push_back(g_targets.empty() ? L"Nearby targets: nothing left in this level" : L"Nearby targets:");
        std::size_t shown = 0;
        for (const Target& t : g_targets) {
            if (shown++ >= kListedTargets) break;
            std::swprintf(buf, 200, L"%ls %ls   %.0f m at %d o'clock   %ls", &t == selected ? L">" : L"  ",
                          Wide(t.label).c_str(), t.distance / 100.0f, ClockOf(g_player, t.pos), Wide(t.status).c_str());
            lines.push_back(buf);
        }
    } else {
        lines.push_back(L"No character (menu or loading)");
    }
    if (!g_flash.empty() && static_cast<int>(GetTickCount() - g_flashUntil) < 0) lines.push_back(Wide(g_flash));

    const float x = 20, y0 = 140, lineHeight = 22;
    float width = 0;
    for (const auto& l : lines) width = std::max(width, paint.Width(l));
    paint.Color(0, 0, 0, 170);
    paint.Rect(x - 8, y0 - 6, width + 16, lineHeight * lines.size() + 12);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i == 0)
            paint.Color(120, 200, 255);
        else
            paint.Color(235, 235, 235);
        paint.Text(x, y0 + lineHeight * i, lines[i]);
    }
}

// Boys of Silence trap: the picture stretched over the screen, then fading out.
void DrawScreamer(const void* canvas, const void* texture, float opacity) {
    float width = 0, height = 0;
    if (!ScreenSize(canvas, &width, &height)) return;
    Painter paint(canvas);
    paint.Color(255, 255, 255, static_cast<std::uint8_t>(opacity * 255));
    paint.Image(texture, 0, 0, width, height, static_cast<float>(screamer::kSize), static_cast<float>(screamer::kSize),
                opacity);
}

// XPawn health and shield offsets per class, found once.
struct PawnFields {
    bool ok = false;
    std::size_t health = 0, healthMax = 0, shield = 0, shieldMax = 0;
};

const PawnFields& FieldsOf(const void* obj) {
    static std::map<const void*, PawnFields> cache;
    const auto [it, added] = cache.try_emplace(ue3::ClassOf(obj));
    if (added) {
        ue3::Property h, hm, s, sm;
        if (ue3::FindProperty(obj, "XHealth", &h) && ue3::FindProperty(obj, "XHealthMax", &hm) &&
            ue3::FindProperty(obj, "XShield", &s) && ue3::FindProperty(obj, "XShieldMax", &sm)) {
            it->second = {true, h.offset, hm.offset, s.offset, sm.offset};
        }
    }
    return it->second;
}

float ReadFloat(const void* obj, std::size_t offset) {
    float v = 0;
    ue3::ReadBytes(static_cast<const std::uint8_t*>(obj) + offset, &v, sizeof(v));
    return v;
}

void WriteFloat(const void* obj, std::size_t offset, float v) {
    ue3::WriteBytes(const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(obj)) + offset, &v, sizeof(v));
}

// Left alone: Elizabeth and Songbird (allies at the end).
bool IsAlly(const void* pawn) {
    const void* archetype = nullptr;
    ue3::ReadBytes(static_cast<const std::uint8_t*>(pawn) + build::kObjectArchetype, &archetype, sizeof(archetype));
    std::string name = ue3::ObjectName(pawn) + " " + ue3::ObjectName(archetype) + " " + ue3::ClassName(pawn);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return name.find("liz") != std::string::npos || name.find("songbird") != std::string::npos;
}

void ApplyCheats(const void* controller) {
    ue3::Property pawnProp;
    std::string err;
    const void* owner = ue3::ResolvePath(controller, "Pawn", &pawnProp, &err);
    const void* player = owner ? ue3::ObjectField(owner, pawnProp) : nullptr;
    if (g_god && player) {
        const PawnFields& f = FieldsOf(player);
        if (f.ok && ReadFloat(player, f.health) > 0) {
            WriteFloat(player, f.health, ReadFloat(player, f.healthMax));
            WriteFloat(player, f.shield, ReadFloat(player, f.shieldMax));
        }
    }
    static DWORD next = 0;
    if (!g_oneHit || static_cast<int>(GetTickCount() - next) < 0) return;
    next = GetTickCount() + 250;
    // Live enemies only, not archetypes.
    ue3::ForEachObject([&](int, const void* obj) {
        if (obj == player) return;
        const PawnFields& f = FieldsOf(obj);
        if (!f.ok || ReadFloat(obj, f.health) <= 1.0f || ue3::ClassName(ue3::OuterOf(obj)) != "Level" || IsAlly(obj))
            return;
        WriteFloat(obj, f.health, 1.0f);
        WriteFloat(obj, f.shield, 0.0f);
    });
}

void HandleKeys(const void* controller) {
    if (!GameHasFocus()) return;
    if (KeyPressed(VK_F6)) g_visible = !g_visible;
    if (KeyPressed(VK_F4)) {
        g_god = !g_god;
        Flash(g_god ? "Infinite health: ON" : "Infinite health: OFF");
    }
    if (KeyPressed(VK_F5)) {
        g_oneHit = !g_oneHit;
        Flash(g_oneHit ? "One-hit enemies: ON" : "One-hit enemies: OFF");
    }
    // Ctrl+1..5: test traps (30 s), Ctrl+6: scream, Ctrl+0: end of the traps.
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
        static const char* const kTraps[] = {"disarm", "jam", "novigor", "lizstrike", "walk"};
        const auto ignore = [](const std::string&) {};
        for (int i = 0; i < 5; ++i) {
            if (KeyPressed('1' + i))
                traps::Command("trap", std::string("trap ") + kTraps[i] + " 30", controller, ignore);
        }
        if (KeyPressed('6')) traps::Command("trap", "trap scream", controller, ignore);
        if (KeyPressed('0')) traps::Command("trap", "trap stop", controller, ignore);
    }
    if (KeyPressed(VK_F7)) ++g_selected;
    if (KeyPressed(VK_F8)) {
        if (const Target* t = SelectedTarget(); !t) {
            Flash("No target left in this level");
        } else {
            Flash((world::TeleportTo(controller, t->actor) ? "Teleported: " : "Cannot teleport: ") + t->label);
            Log("overlay: teleport to %s", t->label.c_str());
        }
    }
}

}  // namespace

void Tick(const void* controller) {
    if (!ue3::IsObject(g_postRender)) {  // once (and after a reload)
        static const void* cls = nullptr;
        if (!ue3::IsObject(cls)) cls = ue3::FindClass("XGameViewportClient");
        ue3::ForEachObjectOfClass(cls, [](int, const void* viewport) {
            if (!g_postRender && ue3::ObjectName(viewport).rfind("Default__", 0) != 0) {
                g_postRender = ue3::FindFunction(viewport, "PostRender");
            }
        });
    }
    if (!apclient::DebugEnabled()) return;
    if (!ue3::IsObject(g_playerPostRender)) g_playerPostRender = ue3::FindFunction(controller, "PostRender");
    ApplyCheats(controller);
    HandleKeys(controller);
    static DWORD last = 0;
    if (g_visible && GetTickCount() - last >= kRefreshMs) {
        last = GetTickCount();
        Refresh(controller);
    }
}

void AfterProcessEvent(const void* self, const void* func, void* parms) {
    (void)self;
    if (!func || !parms || (func != g_postRender && func != g_playerPostRender)) return;
    const void* canvas = *static_cast<const void* const*>(parms);
    if (!ue3::IsObject(canvas)) return;
    const bool debug = apclient::DebugEnabled() && g_visible;
    if (func == g_playerPostRender) {
        if (debug) DrawMarkers(canvas);
        return;
    }
    if (debug) DrawPanel(canvas);
    float opacity = 0;
    if (const void* screamer = traps::ScreamerImage(&opacity)) DrawScreamer(canvas, screamer, opacity);
}

}  // namespace bsap::overlay
