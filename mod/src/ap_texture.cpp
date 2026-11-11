#include "ap_texture.h"

#include "ap_emblem.h"
#include "ap_icon.h"
#include "ap_screamer.h"
#include "engine_call.h"
#include "game_alloc.h"
#include "game_build.h"
#include "log.h"
#include "ue3.h"

#include "miniz.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace bsap::aptexture {

namespace {

using namespace bsap::build;

const void* g_texture = nullptr;
int g_index = -1;

std::uintptr_t ExeBase() {
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleA("BioShockInfinite.exe"));
}

const void* ItemDatabase() {
    static const void* db = nullptr;
    if (ue3::IsObject(db) && ue3::ClassName(db) == "XItemDatabase") return db;
    db = nullptr;
    if (const void* cls = ue3::FindClass("XItemDatabase")) {
        ue3::ForEachObjectOfClass(cls, [](int, const void* obj) {
            if (!db && ue3::ObjectName(obj).rfind("Default__", 0) != 0) db = obj;
        });
    }
    return db;
}

const std::uint8_t* IconsArray(const void* db) {
    ue3::Property p;
    return ue3::FindProperty(db, "AtlassedIcons", &p) ? static_cast<const std::uint8_t*>(db) + p.offset : nullptr;
}

// Texture2D.Create(InSizeX, InSizeY, InFormat): static.
const void* CreateTexture(const void* anyTexture, int width, int height) {
    const void* create = ue3::FindFunction(anyTexture, "Create");
    if (!create) return nullptr;
    struct {
        std::int32_t sizeX, sizeY;
        std::uint8_t format, pad[3];
        const void* result;
    } parms{width, height, 2 /* PF_A8R8G8B8 */, {}, nullptr};
    engine::CallRaw(anyTexture, create, &parms);
    return ue3::IsObject(parms.result) ? parms.result : nullptr;
}

// Mip 0, like the engine: Lock, copy, Unlock.
bool WritePixels(const void* texture, const std::uint8_t* bgra, std::size_t bytes) {
    ue3::Property mipsProp;
    ue3::ArrayHeader mips{};
    if (!ue3::FindProperty(texture, "Mips", &mipsProp) ||
        !ue3::ReadBytes(static_cast<const std::uint8_t*>(texture) + mipsProp.offset, &mips, sizeof(mips)) ||
        mips.num < 1) {
        return false;
    }
    void* mip0 = nullptr;
    ue3::ReadBytes(mips.data, &mip0, sizeof(mip0));
    if (!mip0) return false;
    using Lock_t = void*(__fastcall*)(void* bulk, void* edx, std::uint32_t flags);
    using Unlock_t = void(__fastcall*)(void* bulk, void* edx);
    const auto lock = reinterpret_cast<Lock_t>(ExeBase() + kRvaBulkDataLock);
    const auto unlock = reinterpret_cast<Unlock_t>(ExeBase() + kRvaBulkDataUnlock);
    void* data = lock(mip0, nullptr, 2 /* LOCK_READ_WRITE */);
    const bool ok = data && ue3::WriteBytes(data, bgra, bytes);
    unlock(mip0, nullptr);
    return ok;
}

void UpdateResource(const void* texture) {
    using UpdateResource_t = void(__fastcall*)(const void* self, void* edx);
    void* const* vtable = *static_cast<void* const* const*>(texture);
    reinterpret_cast<UpdateResource_t>(vtable[kTextureUpdateResourceSlot])(texture, nullptr);
}

}  // namespace

int IconIndex(const void* context) {
    const void* db = ItemDatabase();
    const std::uint8_t* iconsAt = db ? IconsArray(db) : nullptr;
    if (!iconsAt) return -1;
    ue3::ArrayHeader icons{};
    ue3::ReadBytes(iconsAt, &icons, sizeof(icons));

    // Still in place?
    if (g_index >= 0 && g_index < icons.num && ue3::IsObject(g_texture)) {
        const void* tex = nullptr;
        ue3::ReadBytes(icons.data + g_index * kSizeofAtlassedTexture2D, &tex, sizeof(tex));
        if (tex == g_texture) return g_index;
    }
    static DWORD lastAttempt = 0;
    static bool attempted = false;
    if (attempted && GetTickCount() - lastAttempt < 5000) return -1;
    attempted = true;
    lastAttempt = GetTickCount();

    // Called on the class default object, which always exists (a loaded texture could be unloaded
    // under us).
    static const void* textureClass = nullptr;
    if (!ue3::IsObject(textureClass)) textureClass = ue3::FindClass("Texture2D");
    const void* anyTexture = nullptr;
    ue3::ForEachObjectOfClass(textureClass, [&](int, const void* obj) {
        if (!anyTexture) anyTexture = obj;
    });
    const int size = icon::kApIconSize;
    const void* texture = ue3::IsObject(anyTexture) ? CreateTexture(anyTexture, size, size) : nullptr;
    if (!texture || !WritePixels(texture, icon::kApIconBgra, sizeof(icon::kApIconBgra))) {
        Log("aptexture: cannot create the texture (retrying in 5 s)");
        return -1;
    }
    UpdateResource(texture);

    // New entry in the icon table; no game icon is touched.
    if (!alloc::EnsureArrayCapacity(context, iconsAt, kSizeofAtlassedTexture2D, 1)) {
        return -1;
    }
    ue3::ReadBytes(iconsAt, &icons, sizeof(icons));
    const struct {
        const void* texture;
        std::int32_t left, top, width, height;
    } entry{texture, 0, 0, size, size};
    static_assert(sizeof(entry) == kSizeofAtlassedTexture2D, "XAtlassedTexture2D");
    ue3::WriteBytes(icons.data + icons.num * kSizeofAtlassedTexture2D, &entry, sizeof(entry));
    const std::int32_t num = icons.num + 1;
    ue3::WriteBytes(const_cast<std::uint8_t*>(iconsAt) + 4, &num, sizeof(num));

    g_texture = texture;
    g_index = icons.num;
    Log("aptexture: logo Archipelago ajoute (%s, icone %d)", ue3::PathName(texture).c_str(), g_index);
    return g_index;
}

// Boys of Silence trap picture.

const void* ScreamerTexture(const void* context) {
    static const void* texture = nullptr;
    static DWORD lastAttempt = 0;
    if (ue3::IsObject(texture)) return texture;
    if (lastAttempt && GetTickCount() - lastAttempt < 5000) return nullptr;
    lastAttempt = GetTickCount() | 1;
    const void* db = ItemDatabase();
    const std::uint8_t* iconsAt = db ? IconsArray(db) : nullptr;
    static const void* textureClass = nullptr;
    if (!ue3::IsObject(textureClass)) textureClass = ue3::FindClass("Texture2D");
    const void* anyTexture = nullptr;
    ue3::ForEachObjectOfClass(textureClass, [&](int, const void* obj) {
        if (!anyTexture) anyTexture = obj;
    });
    if (!iconsAt || !ue3::IsObject(anyTexture)) return nullptr;
    std::vector<std::uint8_t> bgra(screamer::kBgraBytes);
    mz_ulong length = screamer::kBgraBytes;
    if (mz_uncompress(bgra.data(), &length, screamer::kPacked, sizeof(screamer::kPacked)) != MZ_OK ||
        length != screamer::kBgraBytes) {
        Log("aptexture: scream picture unreadable");
        return nullptr;
    }
    const void* created = CreateTexture(anyTexture, screamer::kSize, screamer::kSize);
    if (!created || !WritePixels(created, bgra.data(), bgra.size())) {
        Log("aptexture: cannot create the scream texture (retrying in 5 s)");
        return nullptr;
    }
    UpdateResource(created);
    // One more entry in the icon table keeps it from the garbage collector.
    if (!alloc::EnsureArrayCapacity(context, iconsAt, kSizeofAtlassedTexture2D, 1)) return nullptr;
    ue3::ArrayHeader icons{};
    ue3::ReadBytes(iconsAt, &icons, sizeof(icons));
    const struct {
        const void* texture;
        std::int32_t left, top, width, height;
    } entry{created, 0, 0, screamer::kSize, screamer::kSize};
    ue3::WriteBytes(icons.data + icons.num * kSizeofAtlassedTexture2D, &entry, sizeof(entry));
    const std::int32_t num = icons.num + 1;
    ue3::WriteBytes(const_cast<std::uint8_t*>(iconsAt) + 4, &num, sizeof(num));
    texture = created;
    Log("aptexture: scream picture ready (%s)", ue3::PathName(texture).c_str());
    return texture;
}

// Login screen emblem.

namespace {

const void* FindNamed(const char* className, const char* name) {
    const void* cls = ue3::FindClass(className);
    const void* found = nullptr;
    if (!cls) return nullptr;
    ue3::ForEachObjectOfClass(cls, [&](int, const void* obj) {
        if (!found && ue3::ObjectName(obj) == name) found = obj;
    });
    return found;
}

// Points resource's FTexture2DResource::Owner at `to` instead of `from`.
bool RetargetOwner(void* resource, const void* from, const void* to) {
    auto* bytes = static_cast<std::uint8_t*>(resource);
    for (std::size_t off = 4; off < 0x100; off += 4) {
        const void* value = nullptr;
        if (!ue3::ReadBytes(bytes + off, &value, sizeof(value))) return false;
        if (value == from) return ue3::WriteBytes(bytes + off, &to, sizeof(to));
    }
    return false;
}

}  // namespace

bool ReplaceAccountEmblem(const void* context) {
    (void)context;
    // The main menu package is reloaded each time the menu comes back, with a new original texture.
    static const void* treatedOriginal = nullptr;
    static void* installedResource = nullptr;
    // The render thread initializes our texture's GPU resource later, from the pixels of the
    // resource's owner, so the swap waits for the next pass.
    static const void* pendingTexture = nullptr;
    static const void* pendingOriginal = nullptr;
    static DWORD lastCheck = 0;
    const DWORD now = GetTickCount();
    if (now - lastCheck < 1000) return treatedOriginal != nullptr;
    lastCheck = now;

    const void* movie = FindNamed("SwfMovie", "XTwoKAccountMenu");
    ue3::Property refsProp, resourceProp;
    if (!movie || !ue3::FindProperty(movie, "References", &refsProp)) return false;
    ue3::ArrayHeader refs{};
    ue3::ReadBytes(static_cast<const std::uint8_t*>(movie) + refsProp.offset, &refs, sizeof(refs));
    const void* original = nullptr;
    for (int i = 0; i < refs.num && i < 64; ++i) {
        const void* ref = nullptr;
        ue3::ReadBytes(refs.data + i * sizeof(void*), &ref, sizeof(ref));
        if (ue3::IsObject(ref) && ue3::ObjectName(ref) == "XTwoKAccountMenu_I79") original = ref;
    }
    if (!original || !ue3::FindProperty(original, "Resource", &resourceProp)) return false;
    auto* originalResourceAt =
        const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(original)) + resourceProp.offset;
    void* originalResource = nullptr;
    ue3::ReadBytes(originalResourceAt, &originalResource, sizeof(originalResource));
    if (original == treatedOriginal && originalResource == installedResource) return true;
    if (!originalResource) return false;

    static std::vector<std::uint8_t> pixels;
    if (pixels.empty()) {
        pixels.resize(static_cast<std::size_t>(emblem::kWidth) * emblem::kHeight * 4);
        mz_ulong size = static_cast<mz_ulong>(pixels.size());
        if (mz_uncompress(pixels.data(), &size, emblem::kBgraZlib, sizeof(emblem::kBgraZlib)) != MZ_OK ||
            size != pixels.size()) {
            Log("aptexture: embleme illisible");
            treatedOriginal = original;  // don't insist
            return false;
        }
    }
    if (pendingOriginal != original || !ue3::IsObject(pendingTexture)) {
        const void* texture = CreateTexture(original, emblem::kWidth, emblem::kHeight);
        if (!texture || !WritePixels(texture, pixels.data(), pixels.size())) {
            Log("aptexture: cannot create the emblem");
            return false;
        }
        UpdateResource(texture);
        pendingTexture = texture;
        pendingOriginal = original;
        return false;  // swap on the next pass
    }
    const void* texture = pendingTexture;
    pendingTexture = pendingOriginal = nullptr;
    auto* textureResourceAt =
        const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(texture)) + resourceProp.offset;
    void* textureResource = nullptr;
    ue3::ReadBytes(textureResourceAt, &textureResource, sizeof(textureResource));
    if (!textureResource) return false;

    // The movie looks the picture up by the original texture's name, so we swap the two textures'
    // GPU resources (and owners). Each still frees its own: ours takes the old picture away, the
    // original shows the logo until the menu unloads.
    const bool ownersOk =
        RetargetOwner(textureResource, texture, original) && RetargetOwner(originalResource, original, texture);
    ue3::WriteBytes(originalResourceAt, &textureResource, sizeof(textureResource));
    ue3::WriteBytes(textureResourceAt, &originalResource, sizeof(originalResource));
    treatedOriginal = original;
    installedResource = textureResource;
    Log("aptexture: Archipelago emblem in place on the login screen%s", ownersOk ? "" : " (resource owners not found)");
    return true;
}

}  // namespace bsap::aptexture
