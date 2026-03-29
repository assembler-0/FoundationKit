#pragma once

#include <FoundationKit/Memory/AnyAllocator.hpp>
#include <FoundationKit/Memory/PlacementNew.hpp>
#include <FoundationKit/Base/Compiler.hpp>

namespace FoundationKit::Memory {

    /// @brief Manages the system-wide default allocator.
    class GlobalAllocator {
    public:
        /// @brief Set the allocator to be used by global new/delete and default smart pointers.
        static void Set(const AnyAllocator alloc) noexcept {
            Instance() = alloc;
        }

        /// @brief Get the current global allocator.
        static AnyAllocator Get() noexcept {
            return Instance();
        }

    private:
        static AnyAllocator& Instance() noexcept {
            static AnyAllocator s_instance{};
            return s_instance;
        }
    };

    /// @brief Global helper to allocate using the global allocator without 'new' syntax.
    template <typename T, typename... Args>
    [[nodiscard]] T* New(Args&&... args) noexcept {
        return Memory::New<T>(GlobalAllocator::Get(), Forward<Args>(args)...);
    }

    /// @brief Global helper to deallocate using the global allocator.
    template <typename T>
    void Delete(T* ptr) noexcept {
        Memory::Delete(GlobalAllocator::Get(), ptr);
    }

} // namespace FoundationKit::Memory

[[nodiscard]] void* operator new(FoundationKit::usize size);
[[nodiscard]] void* operator new[](FoundationKit::usize size);
void operator delete(void* ptr) noexcept;
void operator delete[](void* ptr) noexcept;

#ifdef FOUNDATIONKIT_IMPLEMENT_GLOBAL_NEW
FOUNDATIONKIT_DIAG_PUSH
FOUNDATIONKIT_DIAG_IGNORE("-Wnew-returns-null")
FOUNDATIONKIT_DIAG_IGNORE("-Wnonnull")

[[nodiscard]] void* operator new(FoundationKit::usize size) {
    const auto alloc = FoundationKit::Memory::GlobalAllocator::Get();
    if (!alloc) return nullptr;

    const FoundationKit::usize total_size = size + sizeof(FoundationKit::usize);
    const auto res = alloc.Allocate(total_size, 16);
    if (!res) return nullptr;

    auto* header = static_cast<FoundationKit::usize*>(res.ptr);
    *header = size;
    return header + 1;
}

[[nodiscard]] void* operator new[](FoundationKit::usize size) {
    return operator new(size);
}

void operator delete(void* ptr) noexcept {
    if (!ptr) return;

    const auto alloc = FoundationKit::Memory::GlobalAllocator::Get();
    if (!alloc) return;

    auto* header = static_cast<FoundationKit::usize*>(ptr) - 1;
    const FoundationKit::usize size = *header;
    alloc.Deallocate(header, size + sizeof(FoundationKit::usize));
}

void operator delete[](void* ptr) noexcept {
    operator delete(ptr);
}

FOUNDATIONKIT_DIAG_POP
#endif
