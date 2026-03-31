#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>

#ifndef FOUNDATIONKITCXXSTL_NO_PLACEMENT_NEW

#if !defined(__PLACEMENT_NEW_INLINE) && !defined(_NEW) && !defined(_NEW_) && !defined(_GLIBCXX_NEW) && !defined(_LIBCPP_NEW)
#ifndef FOUNDATIONKITCXXSTL_PLACEMENT_NEW_DEFINED
#define FOUNDATIONKITCXXSTL_PLACEMENT_NEW_DEFINED

[[nodiscard]] inline void* operator new(FoundationKitCxxStl::usize, void* ptr) noexcept { return ptr; }
[[nodiscard]] inline void* operator new[](FoundationKitCxxStl::usize, void* ptr) noexcept { return ptr; }
inline void operator delete(void*, void*) noexcept {}
inline void operator delete[](void*, void*) noexcept {}

#endif
#endif

#endif // FOUNDATIONKITCXXSTL_NO_PLACEMENT_NEW
