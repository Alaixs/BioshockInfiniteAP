#include "ui_fix.h"

#include "game_alloc.h"
#include "log.h"
#include "ue3.h"

#include "miniz.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace bsap::uifix {

namespace {

// ActionPush "textID", "Sold!": parameter of the CLIK Label in the banner.
constexpr char kNeedle[] = "\0textID\0\0Sold!\0";
constexpr std::size_t kNeedleSize = sizeof(kNeedle) - 1;
constexpr std::size_t kKeyOffset = 9;
constexpr std::size_t kKeySize = 5;

bool g_enabled = true;
std::map<const void*, ue3::ArrayHeader> g_original;  // movie -> original RawData (compressed)

std::string LocalizationDir() {
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string root(exe);
    for (int i = 0; i < 3; ++i) root = root.substr(0, root.find_last_of("\\/"));  // ...\Binaries\Win32\x.exe
    return root + "\\XGame\\Localization\\";
}

std::vector<std::string> InstalledLanguages() {
    std::vector<std::string> langs;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((LocalizationDir() + "*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return langs;
    do {
        const std::string name = fd.cFileName;
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && name != "." && name != "..") langs.push_back(name);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return langs;
}

// Value in a UTF-16 LE language file, unquoted. Empty if missing.
std::wstring LocValue(const std::string& lang, const std::string& file, const std::wstring& section,
                      const std::wstring& key) {
    std::string ext = lang;
    for (char& c : ext) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    std::ifstream f(LocalizationDir() + lang + "\\" + file + "." + ext, std::ios::binary);
    const std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (raw.size() < 2 || static_cast<unsigned char>(raw[0]) != 0xFF || static_cast<unsigned char>(raw[1]) != 0xFE) {
        return {};
    }
    const std::wstring wide(reinterpret_cast<const wchar_t*>(raw.data() + 2), (raw.size() - 2) / 2);
    std::wistringstream lines(wide);
    std::wstring line, current;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (!line.empty() && line[0] == L'[') current = line;
        if (current == section && line.rfind(key + L"=", 0) == 0) {
            std::wstring value = line.substr(key.size() + 1);
            for (std::size_t i; (i = value.find(L"\\\"")) != std::wstring::npos;) value.erase(i, 2);  // \"
            if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"')
                value = value.substr(1, value.size() - 2);
            return value;
        }
    }
    return {};
}

// The active language: the one whose file holds the text the Dollar Bill screen has loaded
// (WeaponReplaceString). Empty until loaded.
std::string ActiveLanguage() {
    static const void* screenClass = nullptr;
    if (!ue3::IsObject(screenClass)) screenClass = ue3::FindClass("XDollarBillScreen");
    std::wstring runtime;
    ue3::ForEachObjectOfClass(screenClass, [&](int, const void* screen) {
        ue3::Property p;
        struct {
            wchar_t* data;
            std::int32_t num, max;
        } str{};
        if (!runtime.empty() || !ue3::FindProperty(screen, "WeaponReplaceString", &p) ||
            !ue3::ReadBytes(static_cast<const std::uint8_t*>(screen) + p.offset, &str, sizeof(str)) || !str.data ||
            str.num <= 1 || str.num > 256) {
            return;
        }
        std::vector<wchar_t> text(static_cast<std::size_t>(str.num));
        if (ue3::ReadBytes(str.data, text.data(), text.size() * sizeof(wchar_t))) runtime.assign(text.data());
    });
    if (runtime.empty()) return {};
    for (const std::string& lang : InstalledLanguages()) {
        if (LocValue(lang, "Arc_GFxSinglePlayerMovies", L"[DollarBillScreen XDollarBillScreen]",
                     L"WeaponReplaceString") == runtime) {
            return lang;
        }
    }
    return {};
}

// Official "sold" in the active language (UTF-8). Empty until known.
std::string Translation() {
    std::string lang = ActiveLanguage();
    if (lang.empty()) {
        // Fallback: the first folder other than INT (Steam installs only the chosen language).
        for (const std::string& l : InstalledLanguages()) {
            if (_stricmp(l.c_str(), "INT") != 0) {
                lang = l;
                break;
            }
        }
        if (lang.empty()) lang = "INT";
        Log("uifix: active language not detected, %s by default", lang.c_str());
    }
    const std::wstring value = LocValue(lang, "UserInterface", L"[XVendingMachineRendererData]", L"SoldOutString");
    char utf8[128] = {};
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, utf8, sizeof(utf8), nullptr, nullptr);
    Log("uifix: language %s, \"Sold!\" -> \"%s\"", lang.c_str(), utf8);
    return utf8;
}

// SWF rewrite. A "Sold!" of another length means fixing every length around it: the ActionPush, the
// action record in PlaceObject2, the PlaceObject2 tag, its DefineSprite and the file size.

struct BitReader {
    const std::uint8_t* data;
    std::size_t bit;
    std::uint32_t Read(int n) {
        std::uint32_t v = 0;
        for (int i = 0; i < n; ++i, ++bit) v = (v << 1) | ((data[bit >> 3] >> (7 - (bit & 7))) & 1);
        return v;
    }
    std::size_t Aligned() const { return (bit + 7) >> 3; }
};

std::size_t SkipMatrix(const std::uint8_t* d, std::size_t p) {
    BitReader b{d, p * 8};
    if (b.Read(1)) {
        const int n = b.Read(5);
        b.Read(n);
        b.Read(n);
    }  // scale
    if (b.Read(1)) {
        const int n = b.Read(5);
        b.Read(n);
        b.Read(n);
    }  // rotation
    const int n = b.Read(5);
    b.Read(n);
    b.Read(n);  // translation
    return b.Aligned();
}

std::size_t SkipColorTransformAlpha(const std::uint8_t* d, std::size_t p) {
    BitReader b{d, p * 8};
    const bool add = b.Read(1), mult = b.Read(1);
    const int n = b.Read(4);
    for (int i = 0; i < (mult ? 4 : 0) + (add ? 4 : 0); ++i) b.Read(n);
    return b.Aligned();
}

template <typename T>
void AddTo(std::vector<std::uint8_t>& v, std::size_t pos, std::int32_t delta) {
    T x;
    std::memcpy(&x, v.data() + pos, sizeof(T));
    x = static_cast<T>(x + delta);
    std::memcpy(v.data() + pos, &x, sizeof(T));
}

// swf = whole file, 8-byte "GFX" header included. False if the structure is unexpected.
bool ReplaceSoldText(std::vector<std::uint8_t>& swf, const std::string& text) {
    const auto it = std::search(swf.begin() + 8, swf.end(), kNeedle, kNeedle + kNeedleSize);
    if (it == swf.end()) return false;
    const std::size_t target = static_cast<std::size_t>(it - swf.begin());
    const std::int32_t delta = static_cast<std::int32_t>(text.size()) - static_cast<std::int32_t>(kKeySize);
    std::vector<std::pair<std::size_t, int>> fields;  // (position, size 2 or 4) to adjust

    // 1. ActionPush: 0x96 then its length, right before the first pushed item.
    if (target < 3 || swf[target - 3] != 0x96) return false;
    fields.push_back({target - 2, 2});

    // 2. DefineSprite then PlaceObject2 containing the instruction (long headers).
    std::size_t p = 8 + ((5 + 4 * (swf[8] >> 3) + 7) / 8) + 4, end = swf.size();
    std::size_t placeStart = 0, placeEnd = 0;
    while (p + 2 <= end) {
        std::uint16_t codeLen;
        std::memcpy(&codeLen, swf.data() + p, 2);
        const int code = codeLen >> 6;
        std::uint32_t length = codeLen & 0x3F;
        std::size_t header = 2;
        if (length == 0x3F) {
            std::memcpy(&length, swf.data() + p + 2, 4);
            header = 6;
        }
        const std::size_t start = p + header;
        if (start <= target && target < start + length) {
            if (header != 6) return false;
            fields.push_back({p + 2, 4});
            if (code == 39) {
                p = start + 4;
                end = start + length;
                continue;
            }                              // DefineSprite
            if (code != 26) return false;  // PlaceObject2
            placeStart = start;
            placeEnd = start + length;
            break;
        }
        if (code == 0) return false;
        p = start + length;
    }
    if (!placeEnd) return false;

    // 3. PlaceObject2 fields up to the ClipActions, then the record containing the instruction.
    std::size_t q = placeStart;
    const std::uint8_t flags = swf[q];
    q += 3;                    // flags, depth
    if (flags & 0x02) q += 2;  // character
    if (flags & 0x04) q = SkipMatrix(swf.data(), q);
    if (flags & 0x08) q = SkipColorTransformAlpha(swf.data(), q);
    if (flags & 0x10) q += 2;  // ratio
    if (flags & 0x20)
        while (q < placeEnd && swf[q++]) {
        }                      // name
    if (flags & 0x40) q += 2;  // clip depth
    if (!(flags & 0x80)) return false;
    q += 2 + 4;  // reserved, AllEventFlags
    for (;;) {
        if (q + 8 > placeEnd) return false;
        std::uint32_t event, size;
        std::memcpy(&event, swf.data() + q, 4);
        std::memcpy(&size, swf.data() + q + 4, 4);
        if (!event) return false;
        if (q + 8 <= target && target < q + 8 + size) {
            fields.push_back({q + 4, 4});
            break;
        }
        q += 8 + size;
    }

    for (const auto& [pos, width] : fields) {
        if (width == 2)
            AddTo<std::uint16_t>(swf, pos, delta);
        else
            AddTo<std::uint32_t>(swf, pos, delta);
    }
    AddTo<std::uint32_t>(swf, 4, delta);  // file size in the header
    const std::size_t key = target + kKeyOffset;
    swf.erase(swf.begin() + key, swf.begin() + key + kKeySize);
    swf.insert(swf.begin() + key, text.begin(), text.end());
    return true;
}

bool SwfIsConsistent(const std::vector<std::uint8_t>& swf) {
    std::uint32_t declared;
    std::memcpy(&declared, swf.data() + 4, 4);
    if (declared != swf.size()) return false;
    std::size_t p = 8 + ((5 + 4 * (swf[8] >> 3) + 7) / 8) + 4;
    while (p + 2 <= swf.size()) {
        std::uint16_t codeLen;
        std::memcpy(&codeLen, swf.data() + p, 2);
        std::uint32_t length = codeLen & 0x3F;
        std::size_t header = 2;
        if (length == 0x3F) {
            std::memcpy(&length, swf.data() + p + 2, 4);
            header = 6;
        }
        p += header + length;
        if ((codeLen >> 6) == 0) return p == swf.size();
    }
    return false;
}

bool PatchMovie(const void* context, const void* movie, const std::string& repl) {
    ue3::Property p;
    ue3::ArrayHeader raw{};
    if (!ue3::FindProperty(movie, "RawData", &p) ||
        !ue3::ReadBytes(static_cast<const std::uint8_t*>(movie) + p.offset, &raw, sizeof(raw)) || raw.num < 8) {
        return false;
    }
    std::vector<std::uint8_t> file(static_cast<std::size_t>(raw.num));
    if (!ue3::ReadBytes(raw.data, file.data(), file.size())) return false;
    if (std::memcmp(file.data(), "CFX", 3) != 0) {
        return true;  // already inflated (so already fixed) or unexpected format
    }
    std::uint32_t fileLength = 0;
    std::memcpy(&fileLength, file.data() + 4, 4);
    if (fileLength < 8 || fileLength > 64u * 1024 * 1024) return true;

    std::vector<std::uint8_t> out(fileLength);
    std::memcpy(out.data(), "GFX", 3);
    out[3] = file[3];
    std::memcpy(out.data() + 4, &fileLength, 4);
    mz_ulong bodyLength = fileLength - 8;
    if (mz_uncompress(out.data() + 8, &bodyLength, file.data() + 8, static_cast<mz_ulong>(file.size() - 8)) != MZ_OK ||
        bodyLength != fileLength - 8) {
        Log("uifix: cannot inflate %s", ue3::ObjectName(movie).c_str());
        return true;
    }
    if (!ReplaceSoldText(out, repl) || !SwfIsConsistent(out)) {
        Log("uifix: %s: unexpected structure, left as is", ue3::ObjectName(movie).c_str());
        return true;
    }

    // Allocated by the game, so it frees it with the package.
    std::size_t capacity = 0;
    void* block = alloc::NewBlock(context, out.size(), &capacity);
    if (!block || !ue3::WriteBytes(block, out.data(), out.size())) return false;
    g_original[movie] = raw;  // for "uifix off"
    const ue3::ArrayHeader patchedRaw{static_cast<std::uint8_t*>(block), static_cast<std::int32_t>(out.size()),
                                      static_cast<std::int32_t>(capacity)};
    ue3::WriteBytes(const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(movie)) + p.offset, &patchedRaw,
                    sizeof(patchedRaw));
    Log("uifix: %s fixed (\"Sold!\" -> \"%s\", %u bytes)", ue3::ObjectName(movie).c_str(), repl.c_str(),
        static_cast<unsigned>(out.size()));
    return true;
}

}  // namespace

std::string LanguageWithValue(const std::string& file, const std::wstring& section, const std::wstring& key,
                              const std::wstring& value) {
    for (const std::string& lang : InstalledLanguages()) {
        if (!value.empty() && LocValue(lang, file, section, key) == value) return lang;
    }
    return {};
}

void Command(const std::string& arg, const std::function<void(const std::string&)>& reply) {
    if (arg == "off") {
        g_enabled = false;
        for (const auto& [movie, raw] : g_original) {
            ue3::Property p;
            if (ue3::IsObject(movie) && ue3::FindProperty(movie, "RawData", &p)) {
                ue3::WriteBytes(const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(movie)) + p.offset, &raw,
                                sizeof(raw));
            }
        }
        g_original.clear();
        reply("ok uifix off: original Flash restored (applies on the next opening)");
    } else if (arg == "on") {
        g_enabled = true;
        reply("ok uifix on: fix applied again within 5 s");
    } else {
        reply(std::string("uifix ") + (g_enabled ? "on" : "off"));
    }
}

void Tick(const void* context) {
    if (!g_enabled) return;

    static DWORD last = 0;
    if (GetTickCount() - last < 5000) return;
    last = GetTickCount();

    static const void* swfClass = nullptr;
    if (!ue3::IsObject(swfClass)) swfClass = ue3::FindClass("SwfMovie");
    std::vector<const void*> movies;
    ue3::ForEachObjectOfClass(swfClass, [&](int, const void* movie) {
        if (ue3::ObjectName(movie).rfind("DollarBillScreen", 0) == 0) movies.push_back(movie);
    });
    if (movies.empty()) return;

    // Known once the Dollar Bill screen is loaded (same package as its Flash).
    static bool known = false;
    static std::string repl;
    if (!known) {
        repl = Translation();
        known = true;
    }
    if (repl.empty() || repl == "Sold!") return;  // English (or no translation): nothing to fix
    for (const void* movie : movies) {
        PatchMovie(context, movie, repl);  // an already fixed movie is no longer "CFX": left as is
    }
}

}  // namespace bsap::uifix
