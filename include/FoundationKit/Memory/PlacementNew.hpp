#pragma once

#include <FoundationKit/Base/Types.hpp>

[[nodiscard]] inline void* operator new(FoundationKit::usize, void* ptr) noexcept { return ptr; }
[[nodiscard]] inline void* operator new[](FoundationKit::usize, void* ptr) noexcept { return ptr; }
inline void operator delete(void*, void*) noexcept {}
inline void operator delete[](void*, void*) noexcept {}
