#pragma once

// Memory from the game's allocator (GMalloc) without knowing its address: an UnrealScript function
// returning a string allocates it with GMalloc, so we call Object.Left(S, n) through ProcessEvent.
// For bytes, ask for n characters = 2n bytes. Only such memory may go into the game's TArrays,
// which free it with appFree.
//
// Game thread only.

#include <cstddef>
#include <cstdint>
#include <string>

namespace bsap::alloc {

// FString = TArray<wchar_t>: { Data, Num (terminating zero included), Max }.
struct FStringHeader {
    wchar_t* data;
    std::int32_t num;
    std::int32_t max;
};

// context: any valid object (Left is a static function of Object).
bool NewString(const void* context, const std::wstring& text, FStringHeader* out);

// At least `bytes` bytes, zero filled. *capacity receives the size actually usable.
void* NewBlock(const void* context, std::size_t bytes, std::size_t* capacity);

// Makes room for `extra` more elements in the game's TArray at `arrayAt`. When capacity is
// short, a new storage is allocated BY THE GAME, the content is copied there and the old one is
// no longer referenced (a few hundred bytes, harmless).
bool EnsureArrayCapacity(const void* context, const void* arrayAt, std::size_t elemSize, int extra);

}  // namespace bsap::alloc
