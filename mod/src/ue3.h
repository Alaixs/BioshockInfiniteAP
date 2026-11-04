#pragma once

// Reading BioShock Infinite's UE3 reflection: names, classes, properties. Reads are guarded: a bad
// address fails instead of crashing. Game thread only: GC and level loads move objects, so never
// keep a pointer across frames.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace bsap::ue3 {

struct Property {
    std::string owner;
    std::string kind;  // IntProperty, FloatProperty, ObjectProperty...
    std::string name;
    std::uint32_t offset = 0;
    std::uint32_t boolMask = 0;
};

bool NamesReady();
bool IsObject(const void* p);
// Text of an FName (index in GNames), without its number.
std::string NameString(std::int32_t index);
// Index of the FName with this text, -1 if none (full walk of GNames: cache it).
std::int32_t FindName(const std::string& text);
std::string ObjectName(const void* obj);
std::string ClassName(const void* obj);

// "Outer.Name": the short name is not enough to tell two objects apart.
std::string FullName(const void* obj);
// Full path "Package.Level.Object": stable id of an object placed in a map.
std::string PathName(const void* obj);
const void* ClassOf(const void* obj);
const void* OuterOf(const void* obj);

bool ReadBytes(const void* src, void* dst, std::size_t bytes);
bool WriteBytes(void* dst, const void* src, std::size_t bytes);

struct ArrayHeader {
    std::uint8_t* data;
    std::int32_t num;
    std::int32_t max;
};

// GObjects. Some slots may be empty.
int GlobalObjectCount();
const void* GlobalObject(int index);
int IndexOf(const void* obj);
// The UClass with this name (full walk of GObjects: cache it).
const void* FindClass(const std::string& name);
// Fast walk of the objects whose class is EXACTLY cls (one read per object).
void ForEachObjectOfClass(const void* cls, const std::function<void(int, const void*)>& fn);
// Every object (any class): only for infrequent searches.
void ForEachObject(const std::function<void(int, const void*)>& fn);

void ForEachClassProperty(const void* cls, const std::function<void(const Property&)>& fn);
// Only the fields declared by this struct (e.g. the parameters of a function).
void ForEachOwnProperty(const void* strct, const std::function<void(const Property&)>& fn);
// A UFunction of the object's class (or a parent), by name.
const void* FindFunction(const void* obj, const std::string& name);
// "Class.Function(Type Param, ...)" signatures whose name contains filter.
void ForEachFunction(const void* obj, const std::string& filter, const std::function<void(const std::string&)>& fn);
void ForEachProperty(const void* obj, const std::function<void(const Property&)>& fn);
bool FindProperty(const void* obj, const std::string& name, Property* out);

// "Pawn.XHealth": follows the ObjectProperties down to the second-to-last link.
// "#1234.Field" starts from object #1234 of GObjects instead of root.
// Returns the object holding the last property, or nullptr (err says why).
const void* ResolvePath(const void* root, const std::string& path, Property* last, std::string* err);

// nullptr if empty or invalid.
const void* ObjectField(const void* obj, const Property& p);

// Int/Float/Byte; false for other types.
bool ReadNumber(const void* obj, const Property& p, double* out);

std::string ValueToString(const void* obj, const Property& p);
// Only Int/Float/Byte/Bool: blindly writing a pointer or a struct would break the game.
bool WriteValue(const void* obj, const Property& p, const std::string& text, std::string* err);

}  // namespace bsap::ue3
