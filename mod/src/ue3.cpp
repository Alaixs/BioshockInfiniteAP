#include "ue3.h"

#include "game_build.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace bsap::ue3 {

namespace {

using namespace bsap::build;

// The only function touching game memory. __try can't be in a function that destroys C++ objects,
// hence no std::string here.
bool SafeCopy(void* dst, const void* src, std::size_t bytes) {
    __try {
        std::memcpy(dst, src, bytes);
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                                                 : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

template <typename T>
T Read(const void* base, std::size_t offset) {
    T value{};
    SafeCopy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
}

const void* ReadPtr(const void* base, std::size_t offset) {
    return Read<const void*>(base, offset);
}

bool Plausible(const void* p) {
    const auto v = reinterpret_cast<std::uintptr_t>(p);
    // Large Address Aware: objects can live above 2 GB.
    return v >= 0x10000u && v < 0xFFFF0000u && (v & 3u) == 0u;
}

std::uintptr_t ExeBase() {
    static const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("BioShockInfinite.exe"));
    return base;
}

bool Names(const void** data, std::int32_t* num) {
    const std::uintptr_t base = ExeBase();
    if (!base) {
        return false;
    }
    *data = ReadPtr(reinterpret_cast<const void*>(base + kRvaGNamesData), 0);
    *num = Read<std::int32_t>(reinterpret_cast<const void*>(base + kRvaGNamesNum), 0);
    return Plausible(*data) && *num > 0;
}

std::string NameText(std::int32_t index) {
    const void* data = nullptr;
    std::int32_t num = 0;
    if (!Names(&data, &num) || index < 0 || index >= num) {
        return {};
    }
    // Entries are packed, not 4-byte aligned.
    const void* entry = ReadPtr(data, static_cast<std::size_t>(index) * sizeof(void*));
    if (reinterpret_cast<std::uintptr_t>(entry) < 0x10000u) {
        return {};
    }
    // An entry stores its own index: if it doesn't match, it is not a name.
    const auto packed = Read<std::uint32_t>(entry, kNameEntryPacked);
    if (static_cast<std::int32_t>(packed >> 1) != index) {
        return {};
    }
    const bool wide = (packed & 1u) != 0;
    std::string out;
    for (std::size_t i = 0; i < 256; ++i) {
        const std::uint32_t c =
            wide ? Read<std::uint16_t>(entry, kNameEntryText + i * 2) : Read<std::uint8_t>(entry, kNameEntryText + i);
        if (c == 0) {
            return out;
        }
        out.push_back(c >= 0x20 && c <= 0x7E ? static_cast<char>(c) : '?');
    }
    return {};
}

}  // namespace

bool NamesReady() {
    const void* data = nullptr;
    std::int32_t num = 0;
    return Names(&data, &num);
}

// An object's class is an object, and the class of classes is its own class: a random pointer fails
// this.
bool IsObject(const void* p) {
    if (!Plausible(p)) {
        return false;
    }
    const void* cls = ReadPtr(p, kObjectClass);
    if (!Plausible(cls)) {
        return false;
    }
    const void* meta = ReadPtr(cls, kObjectClass);
    return Plausible(meta) && ReadPtr(meta, kObjectClass) == meta;
}

std::string ObjectName(const void* obj) {
    if (!IsObject(obj)) {
        return {};
    }
    std::string name = NameText(Read<std::int32_t>(obj, kObjectName));
    // FName.Number: 0 = no suffix, N = "_<N-1>". Two vending machines of a level differ only by it.
    const std::int32_t number = Read<std::int32_t>(obj, kObjectName + 4);
    if (number > 0) {
        name += "_" + std::to_string(number - 1);
    }
    return name;
}

std::string PathName(const void* obj) {
    std::string path;
    for (int depth = 0; depth < 8 && IsObject(obj); ++depth) {
        path = path.empty() ? ObjectName(obj) : ObjectName(obj) + "." + path;
        obj = ReadPtr(obj, kObjectOuter);
    }
    return path;
}

std::string ClassName(const void* obj) {
    return IsObject(obj) ? ObjectName(ReadPtr(obj, kObjectClass)) : std::string();
}

std::string FullName(const void* obj) {
    if (!IsObject(obj)) {
        return {};
    }
    const void* outer = ReadPtr(obj, kObjectOuter);
    const std::string name = ObjectName(obj);
    return IsObject(outer) ? ObjectName(outer) + "." + name : name;
}

const void* ClassOf(const void* obj) {
    return IsObject(obj) ? ReadPtr(obj, kObjectClass) : nullptr;
}

const void* OuterOf(const void* obj) {
    const void* outer = IsObject(obj) ? ReadPtr(obj, kObjectOuter) : nullptr;
    return IsObject(outer) ? outer : nullptr;
}

bool ReadBytes(const void* src, void* dst, std::size_t bytes) {
    return SafeCopy(dst, src, bytes);
}

int GlobalObjectCount() {
    const std::uintptr_t base = ExeBase();
    return base ? Read<std::int32_t>(reinterpret_cast<const void*>(base + kRvaGObjectsNum), 0) : 0;
}

const void* GlobalObject(int index) {
    const std::uintptr_t base = ExeBase();
    if (!base || index < 0 || index >= GlobalObjectCount()) {
        return nullptr;
    }
    const void* data = ReadPtr(reinterpret_cast<const void*>(base + kRvaGObjectsData), 0);
    const void* obj = ReadPtr(data, static_cast<std::size_t>(index) * sizeof(void*));
    return IsObject(obj) ? obj : nullptr;
}

int IndexOf(const void* obj) {
    return IsObject(obj) ? Read<std::int32_t>(obj, kObjectIndex) : -1;
}

namespace {

std::vector<const void*> ObjectTable() {
    const std::uintptr_t base = ExeBase();
    const int count = GlobalObjectCount();
    std::vector<const void*> table;
    if (!base || count <= 0 || count > 4000000) {
        return table;
    }
    table.resize(static_cast<std::size_t>(count));
    const void* data = ReadPtr(reinterpret_cast<const void*>(base + kRvaGObjectsData), 0);
    if (!SafeCopy(table.data(), data, table.size() * sizeof(void*))) {
        table.clear();
    }
    return table;
}

}  // namespace

const void* FindClass(const std::string& name) {
    const std::vector<const void*> table = ObjectTable();
    const void* classClass = nullptr;
    for (const void* obj : table) {
        if (!Plausible(obj)) {
            continue;
        }
        const void* cls = ReadPtr(obj, kObjectClass);
        if (!classClass) {
            // The class-of-classes is its own class: the first object pointing to it gives it.
            const void* meta = Plausible(cls) ? ReadPtr(cls, kObjectClass) : nullptr;
            if (Plausible(meta) && ReadPtr(meta, kObjectClass) == meta) {
                classClass = meta;
            }
        }
        if (cls == classClass && classClass && ObjectName(obj) == name) {
            return obj;
        }
    }
    return nullptr;
}

void ForEachObjectOfClass(const void* cls, const std::function<void(int, const void*)>& fn) {
    if (!Plausible(cls)) {
        return;
    }
    const std::vector<const void*> table = ObjectTable();
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (Plausible(table[i]) && ReadPtr(table[i], kObjectClass) == cls) {
            fn(static_cast<int>(i), table[i]);
        }
    }
}

void ForEachObject(const std::function<void(int, const void*)>& fn) {
    const std::vector<const void*> table = ObjectTable();
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (Plausible(table[i])) fn(static_cast<int>(i), table[i]);
    }
}

void ForEachProperty(const void* obj, const std::function<void(const Property&)>& fn) {
    if (IsObject(obj)) {
        ForEachClassProperty(ReadPtr(obj, kObjectClass), fn);
    }
}

void ForEachClassProperty(const void* cls, const std::function<void(const Property&)>& fn) {
    for (int level = 0; level < 32 && Plausible(cls); ++level) {
        ForEachOwnProperty(cls, fn);
        cls = ReadPtr(cls, kStructSuper);
    }
}

const void* FindFunction(const void* obj, const std::string& name) {
    if (!IsObject(obj)) {
        return nullptr;
    }
    const void* cls = ReadPtr(obj, kObjectClass);
    for (int level = 0; level < 32 && Plausible(cls); ++level) {
        const void* field = ReadPtr(cls, kStructChildren);
        for (int i = 0; i < 4096 && IsObject(field); ++i) {
            if (ClassName(field) == "Function" && _stricmp(ObjectName(field).c_str(), name.c_str()) == 0) {
                return field;
            }
            field = ReadPtr(field, kFieldNext);
        }
        cls = ReadPtr(cls, kStructSuper);
    }
    return nullptr;
}

void ForEachFunction(const void* obj, const std::string& filter, const std::function<void(const std::string&)>& fn) {
    if (!IsObject(obj)) {
        return;
    }
    std::string lowerFilter = filter;
    for (char& c : lowerFilter) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    const void* cls = ReadPtr(obj, kObjectClass);
    for (int level = 0; level < 32 && Plausible(cls); ++level) {
        const void* field = ReadPtr(cls, kStructChildren);
        for (int i = 0; i < 4096 && IsObject(field); ++i) {
            if (ClassName(field) == "Function") {
                const std::string name = ObjectName(field);
                std::string lower = name;
                for (char& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
                if (lower.find(lowerFilter) != std::string::npos) {
                    std::string sig = ObjectName(cls) + "." + name + "(";
                    bool first = true;
                    ForEachOwnProperty(field, [&](const Property& p) {
                        sig += (first ? "" : ", ") + p.kind.substr(0, p.kind.size() - 8) + " " + p.name;
                        first = false;
                    });
                    fn(sig + ")");
                }
            }
            field = ReadPtr(field, kFieldNext);
        }
        cls = ReadPtr(cls, kStructSuper);
    }
}

void ForEachOwnProperty(const void* strct, const std::function<void(const Property&)>& fn) {
    if (!Plausible(strct)) {
        return;
    }
    const std::string owner = ObjectName(strct);
    const void* field = ReadPtr(strct, kStructChildren);
    for (int i = 0; i < 4096 && IsObject(field); ++i) {
        Property p;
        p.kind = ClassName(field);
        // Children also include functions, constants and enums: only *Property are fields.
        const std::size_t len = p.kind.size();
        if (len > 8 && p.kind.compare(len - 8, 8, "Property") == 0) {
            p.owner = owner;
            p.name = ObjectName(field);
            p.offset = Read<std::uint32_t>(field, kPropertyOffset);
            p.boolMask = p.kind == "BoolProperty" ? Read<std::uint32_t>(field, kBoolPropertyMask) : 0;
            fn(p);
        }
        field = ReadPtr(field, kFieldNext);
    }
}

bool FindProperty(const void* obj, const std::string& name, Property* out) {
    bool found = false;
    ForEachProperty(obj, [&](const Property& p) {
        if (!found && _stricmp(p.name.c_str(), name.c_str()) == 0) {
            *out = p;
            found = true;
        }
    });
    return found;
}

const void* ResolvePath(const void* root, const std::string& path, Property* last, std::string* err) {
    const void* obj = root;
    std::size_t start = 0;
    if (!path.empty() && path[0] == '#') {
        const std::size_t dot = path.find('.');
        const int index = std::atoi(path.substr(1, dot == std::string::npos ? std::string::npos : dot - 1).c_str());
        obj = GlobalObject(index);
        if (!obj) {
            *err = "object #" + std::to_string(index) + " not found";
            return nullptr;
        }
        if (dot == std::string::npos) {
            *err = "missing field: #" + std::to_string(index) + ".Field";
            return nullptr;
        }
        start = dot + 1;
    }
    for (;;) {
        const std::size_t dot = path.find('.', start);
        const std::string part = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        Property p;
        if (!FindProperty(obj, part, &p)) {
            *err = "property not found: " + part + " (on " + ClassName(obj) + ")";
            return nullptr;
        }
        if (dot == std::string::npos) {
            *last = p;
            return obj;
        }
        if (p.kind != "ObjectProperty" && p.kind != "ComponentProperty") {
            *err = part + " is not an object (" + p.kind + ")";
            return nullptr;
        }
        obj = ReadPtr(obj, p.offset);
        if (!IsObject(obj)) {
            *err = part + " is empty";
            return nullptr;
        }
        start = dot + 1;
    }
}

const void* ObjectField(const void* obj, const Property& p) {
    const void* v = ReadPtr(obj, p.offset);
    return IsObject(v) ? v : nullptr;
}

bool ReadNumber(const void* obj, const Property& p, double* out) {
    const auto* src = static_cast<const std::uint8_t*>(obj) + p.offset;
    if (p.kind == "IntProperty") {
        std::int32_t v;
        if (!SafeCopy(&v, src, sizeof(v))) return false;
        *out = v;
    } else if (p.kind == "FloatProperty") {
        float v;
        if (!SafeCopy(&v, src, sizeof(v))) return false;
        *out = v;
    } else if (p.kind == "ByteProperty") {
        std::uint8_t v;
        if (!SafeCopy(&v, src, sizeof(v))) return false;
        *out = v;
    } else {
        return false;
    }
    return true;
}

std::string ValueToString(const void* obj, const Property& p) {
    char buf[256] = {};
    if (p.kind == "IntProperty") {
        std::snprintf(buf, sizeof(buf), "%d", Read<std::int32_t>(obj, p.offset));
    } else if (p.kind == "QwordProperty") {
        std::snprintf(buf, sizeof(buf), "%lld", Read<long long>(obj, p.offset));
    } else if (p.kind == "FloatProperty") {
        std::snprintf(buf, sizeof(buf), "%.3f", Read<float>(obj, p.offset));
    } else if (p.kind == "ByteProperty") {
        std::snprintf(buf, sizeof(buf), "%u", Read<std::uint8_t>(obj, p.offset));
    } else if (p.kind == "BoolProperty") {
        std::snprintf(buf, sizeof(buf), "%d", (Read<std::uint32_t>(obj, p.offset) & p.boolMask) ? 1 : 0);
    } else if (p.kind == "NameProperty") {
        return "'" + NameText(Read<std::int32_t>(obj, p.offset)) + "'";
    } else if (p.kind == "ObjectProperty" || p.kind == "ComponentProperty" || p.kind == "ClassProperty") {
        const void* v = ReadPtr(obj, p.offset);
        if (!v) {
            return "None";
        }
        return IsObject(v) ? ClassName(v) + "'" + ObjectName(v) + "'" : "<?>";
    } else {
        return "...";  // structs, arrays, strings: not read
    }
    return buf;
}

bool WriteBytes(void* dst, const void* src, std::size_t bytes) {
    return SafeCopy(dst, src, bytes);
}

bool WriteValue(const void* obj, const Property& p, const std::string& text, std::string* err) {
    auto* dst = const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(obj)) + p.offset;
    char* end = nullptr;
    bool ok = false;
    if (p.kind == "IntProperty") {
        const auto v = static_cast<std::int32_t>(std::strtol(text.c_str(), &end, 0));
        ok = *end == '\0' && SafeCopy(dst, &v, sizeof(v));
    } else if (p.kind == "FloatProperty") {
        const float v = std::strtof(text.c_str(), &end);
        ok = *end == '\0' && SafeCopy(dst, &v, sizeof(v));
    } else if (p.kind == "ByteProperty") {
        const auto v = static_cast<std::uint8_t>(std::strtoul(text.c_str(), &end, 0));
        ok = *end == '\0' && SafeCopy(dst, &v, sizeof(v));
    } else if (p.kind == "BoolProperty") {
        std::uint32_t word = Read<std::uint32_t>(obj, p.offset);
        word = text == "0" ? (word & ~p.boolMask) : (word | p.boolMask);
        ok = SafeCopy(dst, &word, sizeof(word));
    } else {
        *err = "type not writable: " + p.kind;
        return false;
    }
    if (!ok) {
        *err = "invalid value or write refused";
    }
    return ok;
}

std::string NameString(std::int32_t index) {
    return NameText(index);
}

std::int32_t FindName(const std::string& text) {
    const void* data = nullptr;
    std::int32_t num = 0;
    if (!Names(&data, &num)) {
        return -1;
    }
    for (std::int32_t i = 0; i < num; ++i) {
        if (NameText(i) == text) {
            return i;
        }
    }
    return -1;
}

}  // namespace bsap::ue3
