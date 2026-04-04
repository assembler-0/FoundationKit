#pragma once

#include <FoundationKitMemory/PoolAllocator.hpp>
#include <FoundationKitMemory/Segregator.hpp>

namespace FoundationKitMemory {

    /// @brief A high-performance allocator for small objects using multiple size-specific pools.
    /// @desc Manages size classes: 16, 32, 64, 128, 256, 512.
    ///       Larger requests are passed to a fallback allocator (typically a FreeListAllocator).
    /// @warning NOT thread-safe. For multi-threaded use, wrap with:
    ///          SynchronizedAllocator<SlabAllocator<Fallback>, Mutex>
    template <IAllocator Fallback>
    class SlabAllocator {
    public:
        constexpr SlabAllocator() noexcept = default;

        /// @brief Initialize all slabs and the fallback.
        void Initialize(void* buffer, usize size, Fallback&& fallback) noexcept {
            m_fallback = FoundationKitCxxStl::Move(fallback);
            
            const usize raw_pool_size = size / 6; // 6 pools (16, 32, 64, 128, 256, 512)
            byte* ptr = static_cast<byte*>(buffer);
            byte* end = ptr + size;

            auto InitPool = [&](auto& pool, usize pool_size) {
                // Ensure ptr is aligned to the pool's alignment (usually 8 or 16)
                ptr = reinterpret_cast<byte*>(Alignment(16).AlignUp(reinterpret_cast<uptr>(ptr)));
                if (ptr + pool_size <= end) {
                    pool.Initialize(ptr, pool_size);
                    ptr += pool_size;
                }
            };

            InitPool(m_pool16, raw_pool_size);
            InitPool(m_pool32, raw_pool_size);
            InitPool(m_pool64, raw_pool_size);
            InitPool(m_pool128, raw_pool_size);
            InitPool(m_pool256, raw_pool_size);
            InitPool(m_pool512, raw_pool_size);
            
            // Note: Remaining space is lost or could be given to fallback if fallback was a FreeList.
        }

        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            if (size <= 16)  return m_pool16.Allocate(size, align);
            if (size <= 32)  return m_pool32.Allocate(size, align);
            if (size <= 64)  return m_pool64.Allocate(size, align);
            if (size <= 128) return m_pool128.Allocate(size, align);
            if (size <= 256) return m_pool256.Allocate(size, align);
            if (size <= 512) return m_pool512.Allocate(size, align);
            
            return m_fallback.Allocate(size, align);
        }

        void Deallocate(void* ptr, usize size) noexcept {
            if (size <= 16)  { m_pool16.Deallocate(ptr, size); return; }
            if (size <= 32)  { m_pool32.Deallocate(ptr, size); return; }
            if (size <= 64)  { m_pool64.Deallocate(ptr, size); return; }
            if (size <= 128) { m_pool128.Deallocate(ptr, size); return; }
            if (size <= 256) { m_pool256.Deallocate(ptr, size); return; }
            if (size <= 512) { m_pool512.Deallocate(ptr, size); return; }
            
            m_fallback.Deallocate(ptr, size);
        }

        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            return m_pool16.Owns(ptr) || m_pool32.Owns(ptr) || m_pool64.Owns(ptr) ||
                   m_pool128.Owns(ptr) || m_pool256.Owns(ptr) || m_pool512.Owns(ptr) ||
                   m_fallback.Owns(ptr);
        }

    private:
        PoolAllocator<16>  m_pool16;
        PoolAllocator<32>  m_pool32;
        PoolAllocator<64>  m_pool64;
        PoolAllocator<128> m_pool128;
        PoolAllocator<256> m_pool256;
        PoolAllocator<512> m_pool512;
        Fallback           m_fallback;
    };

} // namespace FoundationKitMemory
