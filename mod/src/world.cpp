#include "world.h"

#include "engine_call.h"
#include "ue3.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace bsap::world {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kUnitsPerTurn = 65536;  // FRotator: 65536 units = 360°

struct Vec3 {
    float x, y, z;
};
struct Rot {
    std::int32_t pitch, yaw, roll;
};

template <typename T>
T Get(const void* base, std::uint32_t offset) {
    T value{};
    ue3::ReadBytes(static_cast<const std::uint8_t*>(base) + offset, &value, sizeof(value));
    return value;
}

std::uint32_t Offset(const void* obj, const char* prop) {
    ue3::Property p;
    return ue3::FindProperty(obj, prop, &p) ? p.offset : 0xFFFFFFFFu;
}

// Irrational renamed Actor.Location / Actor.Rotation.
bool Placement(const void* actor, Vec3* loc, Rot* rot) {
    static std::uint32_t locOff = 0xFFFFFFFFu, rotOff = 0xFFFFFFFFu;
    if (locOff == 0xFFFFFFFFu) {
        locOff = Offset(actor, "XPrivateLocalLocation");
        rotOff = Offset(actor, "XPrivateLocalRotation");
    }
    if (locOff == 0xFFFFFFFFu || !ue3::IsObject(actor)) {
        return false;
    }
    if (loc) *loc = Get<Vec3>(actor, locOff);
    if (rot) *rot = Get<Rot>(actor, rotOff);
    return true;
}

const void* PawnOf(const void* controller) {
    ue3::Property p;
    std::string err;
    const void* owner = ue3::ResolvePath(controller, "Pawn", &p, &err);
    return owner ? ue3::ObjectField(owner, p) : nullptr;
}

double YawDegrees(std::int32_t yaw) {
    double d = std::fmod(static_cast<double>(yaw) * 360.0 / kUnitsPerTurn, 360.0);
    return d < 0 ? d + 360.0 : d;
}

std::int32_t YawUnits(double degrees) {
    return static_cast<std::int32_t>(std::lround(degrees * kUnitsPerTurn / 360.0)) % kUnitsPerTurn;
}

std::string Label(const void* actor) {
    if (ue3::ClassName(actor) == "XVendingMachine") {
        ue3::Property p;
        ue3::ArrayHeader list{};
        if (ue3::FindProperty(actor, "ItemList", &p) &&
            ue3::ReadBytes(static_cast<const std::uint8_t*>(actor) + p.offset, &list, sizeof(list)) && list.num > 0) {
            const std::string first = ue3::ObjectName(Get<const void*>(list.data, 0));
            if (first.find("Health") != std::string::npos) return "Dollar Bill";
            if (first.find("Plasmid") != std::string::npos) return "Veni! Vidi! Vigor!";
            return "Vending machine (" + first + ")";
        }
    }
    return ue3::ObjectName(actor);
}

bool Teleport(const void* controller, Vec3 dest, Rot rot, const Reply& reply) {
    const void* pawn = PawnOf(controller);
    if (!pawn) {
        reply("err no character to teleport");
        return false;
    }
    // ClientSetLocation(NewLocation, NewRotation): moves the Pawn and turns the camera through the
    // engine.
    if (const void* func = ue3::FindFunction(controller, "ClientSetLocation")) {
        struct {
            Vec3 location;
            Rot rotation;
        } parms{dest, rot};
        engine::CallRaw(controller, func, &parms);
    }
    Vec3 now{};
    Placement(pawn, &now, nullptr);
    const float miss = std::sqrt((now.x - dest.x) * (now.x - dest.x) + (now.y - dest.y) * (now.y - dest.y));
    char buf[128];
    std::snprintf(buf, sizeof(buf), "tp ok %.0f %.0f %.0f (off by %.0f)", now.x, now.y, now.z, miss);
    reply(buf);
    return true;
}

bool TeleportInFront(const void* controller, const void* actor, const Reply& reply) {
    Vec3 loc{};
    Rot rot{};
    if (!Placement(actor, &loc, &rot)) return false;
    const double yaw = rot.yaw * 2.0 * kPi / kUnitsPerTurn;
    const Vec3 dest{loc.x + static_cast<float>(150 * std::cos(yaw)), loc.y + static_cast<float>(150 * std::sin(yaw)),
                    loc.z + 100};
    const Rot facing{0, rot.yaw + kUnitsPerTurn / 2, 0};
    return Teleport(controller, dest, facing, reply);
}

}  // namespace

bool PlayerPosition(const void* controller, Position* out) {
    const void* pawn = PawnOf(controller);
    Vec3 loc{};
    Rot view{};
    if (!pawn || !Placement(pawn, &loc, nullptr)) return false;
    Placement(controller, nullptr, &view);  // the camera follows the controller's rotation
    *out = {loc.x, loc.y, loc.z, static_cast<float>(YawDegrees(view.yaw))};
    return true;
}

bool ActorPosition(const void* actor, Position* out) {
    Vec3 loc{};
    Rot rot{};
    if (!Placement(actor, &loc, &rot)) return false;
    *out = {loc.x, loc.y, loc.z, static_cast<float>(YawDegrees(rot.yaw))};
    return true;
}

bool ActorBounds(const void* actor, Box* out) {
    struct BoxSphereBounds {
        Vec3 origin, extent;
        float radius;
    };
    static std::uint32_t componentOff = 0xFFFFFFFFu, boundsOff = 0xFFFFFFFFu;
    Vec3 loc{};
    if (!Placement(actor, &loc, nullptr)) return false;
    if (componentOff == 0xFFFFFFFFu) componentOff = Offset(actor, "CollisionComponent");
    const void* component = componentOff == 0xFFFFFFFFu ? nullptr : Get<const void*>(actor, componentOff);
    if (!ue3::IsObject(component)) return false;
    if (boundsOff == 0xFFFFFFFFu) boundsOff = Offset(component, "Bounds");
    if (boundsOff == 0xFFFFFFFFu) return false;
    const auto b = Get<BoxSphereBounds>(component, boundsOff);
    // Inconsistent bounds (not computed yet, component elsewhere): not used.
    const auto sane = [](float e) { return e > 0.5f && e < 1000.0f; };
    const float dx = b.origin.x - loc.x, dy = b.origin.y - loc.y, dz = b.origin.z - loc.z;
    if (!sane(b.extent.x) || !sane(b.extent.y) || !sane(b.extent.z) ||
        !(dx * dx + dy * dy + dz * dz < 1000.0f * 1000.0f)) {
        return false;
    }
    *out = {b.origin.x, b.origin.y, b.origin.z, b.extent.x, b.extent.y, b.extent.z};
    return true;
}

// Infinite loads a master level (Master_P), and the area (S_TWN_P, S_Fink_P...) as its sublevel.
std::string LoadedMaps() {
    static const void* levelClass = nullptr;
    if (!ue3::IsObject(levelClass)) levelClass = ue3::FindClass("Level");
    std::string map;
    ue3::ForEachObjectOfClass(levelClass, [&](int, const void* level) {
        std::string pkg = ue3::PathName(level);
        pkg = pkg.substr(0, pkg.find('.'));
        if (pkg.size() > 2 && _stricmp(pkg.c_str() + pkg.size() - 2, "_P") == 0 &&
            _stricmp(pkg.c_str(), "Master_P") != 0 && ("," + map + ",").find("," + pkg + ",") == std::string::npos) {
            map += (map.empty() ? "" : ",") + pkg;
        }
    });
    return map.empty() ? "-" : map;
}

bool TeleportTo(const void* controller, const void* actor) {
    return TeleportInFront(controller, actor, [](const std::string&) {});
}

bool Command(const std::string& cmd, const std::string& line, const void* controller, const Reply& reply) {
    std::istringstream in(line);
    std::string skip;
    in >> skip;

    if (cmd == "where") {
        Position pos{};
        if (!PlayerPosition(controller, &pos)) {
            reply("where none");
            return true;
        }
        char buf[128];
        std::snprintf(buf, sizeof(buf), "where %.0f %.0f %.0f %.1f ", pos.x, pos.y, pos.z, pos.yaw);
        reply(buf + LoadedMaps());
    } else if (cmd == "poi") {
        std::string classes = "XVendingMachine";
        in >> classes;
        static std::unordered_map<std::string, const void*> classCache;
        int count = 0;
        std::stringstream list(classes);
        for (std::string cls; std::getline(list, cls, ',');) {
            auto it = classCache.find(cls);
            if (it == classCache.end() || !ue3::IsObject(it->second)) {
                it = classCache.insert_or_assign(cls, ue3::FindClass(cls)).first;
            }
            ue3::ForEachObjectOfClass(it->second, [&](int index, const void* actor) {
                Vec3 loc{};
                Rot rot{};
                if (ue3::ClassName(ue3::OuterOf(actor)) != "Level" || !Placement(actor, &loc, &rot)) return;
                char buf[160];
                std::snprintf(buf, sizeof(buf), "poi #%d %s %.0f %.0f %.0f %.1f ", index, cls.c_str(), loc.x, loc.y,
                              loc.z, YawDegrees(rot.yaw));
                reply(buf + ue3::PathName(actor) + " " + Label(actor));
                ++count;
            });
        }
        reply("end poi " + std::to_string(count));
    } else if (cmd == "tp") {
        Vec3 dest{};
        double yaw = -1;
        if (!(in >> dest.x >> dest.y >> dest.z)) {
            reply("err usage: tp x y z [yaw]");
            return true;
        }
        Rot rot{};
        Placement(controller, nullptr, &rot);
        if (in >> yaw) rot.yaw = YawUnits(yaw);
        rot.pitch = 0;
        rot.roll = 0;
        Teleport(controller, dest, rot, reply);
    } else if (cmd == "tpto") {
        std::string target;
        in >> target;
        const void* actor = ue3::GlobalObject(std::atoi(target.c_str() + (target.rfind('#', 0) == 0 ? 1 : 0)));
        if (!actor || !Placement(actor, nullptr, nullptr)) {
            reply("err usage: tpto #index (see 'poi')");
            return true;
        }
        TeleportInFront(controller, actor, reply);
    } else if (cmd == "bounds") {
        std::string target;
        in >> target;
        const void* actor = ue3::GlobalObject(std::atoi(target.c_str() + (target.rfind('#', 0) == 0 ? 1 : 0)));
        Box b{};
        if (!actor || !ActorBounds(actor, &b)) {
            reply("err no bounding box for " + target + " (see 'poi')");
            return true;
        }
        char buf[160];
        std::snprintf(buf, sizeof(buf), "bounds %s %.0f %.0f %.0f %.0f %.0f %.0f", target.c_str(), b.x, b.y, b.z, b.ex,
                      b.ey, b.ez);
        reply(buf);
    } else {
        return false;
    }
    return true;
}

}  // namespace bsap::world
