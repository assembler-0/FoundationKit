#pragma once

namespace FoundationKitMemory {

    /// @brief Tries to allocate from P (Primary). If it fails, falls back to F (Fallback).
    /// @warning THREAD-SAFETY: The sub-allocators (primary and fallback) must be independently
    ///          thread-safe or wrapped with SynchronizedAllocator. The FallbackAllocator itself
    ///          does not add locking. For multi-threaded use:
    ///          SynchronizedAllocator<FallbackAllocator<P, F>, SpinLock> safe_fallback(...);
    template <IAllocator P, IAllocator F>
    class FallbackAllocator {
    public:
        using Primary  = P;
        using Fallback = F;

        constexpr FallbackAllocator() noexcept = default;

        constexpr FallbackAllocator(P&& primary, F&& fallback) noexcept
            : m_primary(FoundationKitCxxStl::Move(primary)), m_fallback(FoundationKitCxxStl::Move(fallback)) {}

        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            if (const AllocationResult res = m_primary.Allocate(size, align); res.ok()) return res;
            return m_fallback.Allocate(size, align);
        }

        void Deallocate(void* ptr, usize size) noexcept {
            if (m_primary.Owns(ptr)) {
                m_primary.Deallocate(ptr, size);
            } else {
                m_fallback.Deallocate(ptr, size);
            }
        }

        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            return m_primary.Owns(ptr) || m_fallback.Owns(ptr);
        }

        [[nodiscard]] Primary&  GetPrimary()  noexcept { return m_primary; }
        [[nodiscard]] Fallback& GetFallback() noexcept { return m_fallback; }

    private:
        P m_primary;
        F m_fallback;
    };

} // namespace FoundationKitMemory
