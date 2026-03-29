#pragma once

#include <FoundationKit/Base/Types.hpp>

#ifndef FOUNDATIONKIT_NO_PLACEMENT_NEW

#if !defined(__PLACEMENT_NEW_INLINE) && !defined(_NEW) && !defined(_NEW_) && !defined(_GLIBCXX_NEW) && !defined(_LIBCPP_NEW)
#ifndef FOUNDATIONKIT_PLACEMENT_NEW_DEFINED
#define FOUNDATIONKIT_PLACEMENT_NEW_DEFINED

[[nodiscard]] inline void* operator new(FoundationKit::usize, void* ptr) noexcept { return ptr; }
[[nodiscard]] inline void* operator new[](FoundationKit::usize, void* ptr) noexcept { return ptr; }
inline void operator delete(void*, void*) noexcept {}
inline void operator delete[](void*, void*) noexcept {}

#endif
#endif

#endif // FOUNDATIONKIT_NO_PLACEMENT_NEW
