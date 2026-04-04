#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>

#ifndef FOUNDATIONKITMEMORY_NO_PLACEMENT_NEW

#if !defined(__PLACEMENT_NEW_INLINE) && !defined(_NEW) && !defined(_NEW_) && !defined(_GLIBCXX_NEW) && !defined(_LIBCPP_NEW)
#ifndef FOUNDATIONKITMEMORY_PLACEMENT_NEW_DEFINED
#define FOUNDATIONKITMEMORY_PLACEMENT_NEW_DEFINED

[[nodiscard]] inline void* operator new(FoundationKitCxxStl::usize, void* ptr) noexcept { return ptr; }
[[nodiscard]] inline void* operator new[](FoundationKitCxxStl::usize, void* ptr) noexcept { return ptr; }
inline void operator delete(void*, void*) noexcept {}
inline void operator delete[](void*, void*) noexcept {}

#endif
#endif

#endif // FOUNDATIONKITMEMORY_NO_PLACEMENT_NEW
