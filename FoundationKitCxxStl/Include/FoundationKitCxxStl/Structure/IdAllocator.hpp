#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/KernelError.hpp>
#include <FoundationKitCxxStl/Structure/XArray.hpp>
#include <FoundationKitCxxStl/Structure/AtomicBitmap.hpp>
#include <FoundationKitCxxStl/Sync/SpinLock.hpp>
#include <FoundationKitCxxStl/Sync/Locks.hpp>
#include <FoundationKitMemory/Allocators/AnyAllocator.hpp>

namespace FoundationKitCxxStl::Structure {

    using namespace FoundationKitCxxStl;
    using namespace FoundationKitCxxStl::Sync;
    using namespace FoundationKitMemory;

    // =========================================================================
    // IdAllocator<IdType, ChunkSize, Alloc>
    //
    // ## What it is
    //
    // A high-performance, sparse ID allocator powered by an XArray of atomic
    // bitmaps. It is designed for low-contention allocation of unique IDs
    // (e.g., PIDs, FDs, IRQ numbers) across a potentially massive 64-bit
    // address space without requiring contiguous memory.
    //
    // ## Design
    //
    // - Sparse Storage: Uses XArray to store pointers to Bitmaps only where
    //   IDs are actually allocated.
    // - Chunking: Each leaf in the XArray is an AtomicBitmap<ChunkSize>.
    //   Large ChunkSize (e.g., 1024) amortizes XArray lookup and allocation
    //   overhead.
    // - Thread Safety:
    //   - Structural changes (adding/removing chunks) are protected by a
    //     SpinLock.
    //   - Individual ID allocations within a chunk are lock-free via CAS.
    //
    // ## Complexity
    //
    // - Allocate: O(N_chunks * log₆₄(key)) worst case, but typically O(log₆₄(key))
    //   with the next-chunk hint.
    // - Free: O(log₆₄(key)) — pure pointer chase + atomic bit clear.
    // =========================================================================

    /// @brief Sparse ID allocator using XArray and AtomicBitmaps.
    /// @tparam IdType    Integral type for IDs (e.g., u32, usize).
    /// @tparam ChunkSize Number of IDs per bitmap chunk. Must be power of 2 for speed.
    /// @tparam Alloc     Allocator for internal nodes and chunks.
    template <
        Integral IdType = usize,
        usize ChunkSize = 1024,
        IAllocator Alloc = AnyAllocator
    >
    class IdAllocator {
        static_assert(ChunkSize > 0 && (ChunkSize & (ChunkSize - 1)) == 0,
            "IdAllocator: ChunkSize must be a power of 2");

    public:
        explicit IdAllocator(Alloc alloc = AnyAllocator::FromGlobal()) noexcept
            : m_chunks(alloc), m_alloc(alloc), m_lock(), m_next_chunk_hint(0) {}

        ~IdAllocator() noexcept {
            // Free all allocated bitmaps.
            m_chunks.ForEach([this](usize, AtomicBitmap<ChunkSize>& bitmap) {
                bitmap.~AtomicBitmap<ChunkSize>();
                m_alloc.Deallocate(&bitmap, sizeof(AtomicBitmap<ChunkSize>));
            });
            m_chunks.Clear();
        }

        IdAllocator(const IdAllocator&) = delete;
        IdAllocator& operator=(const IdAllocator&) = delete;

        /// @brief Allocate a new unique ID.
        /// @return The ID or an error (e.g., out of memory).
        [[nodiscard]] KernelResult<IdType> Allocate() noexcept {
            return AllocateInRange(0, static_cast<IdType>(-1));
        }

        /// @brief Allocate a new unique ID within [min, max].
        /// @param min Minimum ID (inclusive).
        /// @param max Maximum ID (inclusive).
        /// @return The ID or an error.
        [[nodiscard]] KernelResult<IdType> AllocateInRange(IdType min, IdType max) noexcept {
            if (min > max) return Unexpected(KernelError::InvalidArgument);

            // 1. Fast path: try the hinted chunk.
            {
                auto* hint_bitmap = m_chunks.Load(m_next_chunk_hint);
                if (hint_bitmap) {
                    const IdType chunk_start = static_cast<IdType>(m_next_chunk_hint * ChunkSize);
                    const IdType chunk_end   = chunk_start + ChunkSize - 1;

                    if (chunk_end >= min && chunk_start <= max) {
                        const usize start_bit = (min > chunk_start) ? (min - chunk_start) : 0;
                        const usize end_bit   = (max < chunk_end)   ? (max - chunk_start) : (ChunkSize - 1);

                        usize bit = hint_bitmap->FindFirstUnsetAndSetInRange(start_bit, end_bit);
                        if (bit < ChunkSize) {
                            return static_cast<IdType>(m_next_chunk_hint * ChunkSize + bit);
                        }
                    }
                }
            }

            // 2. Slow path: Search existing chunks within range.
            IdType found_id = static_cast<IdType>(-1);
            m_chunks.ForEach([&](usize chunk_idx, AtomicBitmap<ChunkSize>& bitmap) {
                if (found_id != static_cast<IdType>(-1)) return;

                const IdType chunk_start = static_cast<IdType>(chunk_idx * ChunkSize);
                const IdType chunk_end   = chunk_start + ChunkSize - 1;

                // Overlap check
                if (chunk_end < min || chunk_start > max) return;

                const usize start_bit = (min > chunk_start) ? (min - chunk_start) : 0;
                const usize end_bit   = (max < chunk_end)   ? (max - chunk_start) : (ChunkSize - 1);

                usize bit = bitmap.FindFirstUnsetAndSetInRange(start_bit, end_bit);
                if (bit < ChunkSize) {
                    found_id = static_cast<IdType>(chunk_idx * ChunkSize + bit);
                    m_next_chunk_hint = chunk_idx;
                }
            });

            if (found_id != static_cast<IdType>(-1)) return found_id;

            // 3. Last resort: Create new chunks in the range.
            return AllocateNewChunk(min, max);
        }

        /// @brief Reserve a specific ID.
        /// @return true if successful; false if already allocated or OOM.
        bool Reserve(IdType id) noexcept {
            const usize chunk_idx = static_cast<usize>(id / ChunkSize);
            const usize bit_idx   = static_cast<usize>(id % ChunkSize);

            AtomicBitmap<ChunkSize>* bitmap = GetOrCreateChunk(chunk_idx);
            if (!bitmap) return false;

            return !bitmap->TestAndSet(bit_idx);
        }

        /// @brief Free an ID, making it available for future allocations.
        void Free(IdType id) noexcept {
            const usize chunk_idx = static_cast<usize>(id / ChunkSize);
            const usize bit_idx   = static_cast<usize>(id % ChunkSize);

            auto* bitmap = m_chunks.Load(chunk_idx);
            if (bitmap) {
                bitmap->Reset(bit_idx);
                // Hint: this chunk now definitely has a free bit.
                m_next_chunk_hint = chunk_idx;
            }
        }

        /// @brief Check if an ID is currently allocated.
        [[nodiscard]] bool IsAllocated(IdType id) const noexcept {
            const usize chunk_idx = static_cast<usize>(id / ChunkSize);
            const usize bit_idx   = static_cast<usize>(id % ChunkSize);

            auto* bitmap = m_chunks.Load(chunk_idx);
            return bitmap && bitmap->Test(bit_idx);
        }

    private:
        [[nodiscard]] KernelResult<IdType> AllocateNewChunk(IdType min, IdType max) noexcept {
            LockGuard lock(m_lock);

            // Re-check existing chunks under lock just in case another thread added one.
            for (IdType id = min; id <= max; ) {
                const usize chunk_idx = static_cast<usize>(id / ChunkSize);
                const IdType chunk_start = static_cast<IdType>(chunk_idx * ChunkSize);
                const IdType chunk_end   = chunk_start + ChunkSize - 1;
                
                auto* bitmap = m_chunks.Load(chunk_idx);
                if (!bitmap) {
                    bitmap = CreateChunkUnderLock(chunk_idx);
                    if (!bitmap) return Unexpected(KernelError::OutOfMemory);
                }

                const usize start_bit = (min > chunk_start) ? (min - chunk_start) : 0;
                const usize end_bit   = (max < chunk_end)   ? (max - chunk_start) : (ChunkSize - 1);

                usize bit = bitmap->FindFirstUnsetAndSetInRange(start_bit, end_bit);
                if (bit < ChunkSize) {
                    m_next_chunk_hint = chunk_idx;
                    return static_cast<IdType>(chunk_idx * ChunkSize + bit);
                }

                // Advance to next chunk
                id = static_cast<IdType>((chunk_idx + 1) * ChunkSize);
                if (id == 0) break; // Overflow
            }

            return Unexpected(KernelError::NotFound); 
        }

        [[nodiscard]] AtomicBitmap<ChunkSize>* GetOrCreateChunk(usize chunk_idx) noexcept {
            auto* bitmap = m_chunks.Load(chunk_idx);
            if (bitmap) return bitmap;

            LockGuard lock(m_lock);
            // Double check
            bitmap = m_chunks.Load(chunk_idx);
            if (bitmap) return bitmap;

            return CreateChunkUnderLock(chunk_idx);
        }

        [[nodiscard]] AtomicBitmap<ChunkSize>* CreateChunkUnderLock(usize chunk_idx) noexcept {
            auto res = m_alloc.Allocate(sizeof(AtomicBitmap<ChunkSize>), alignof(AtomicBitmap<ChunkSize>));
            if (!res.ok()) return nullptr;

            auto* bitmap = new (res.ptr) AtomicBitmap<ChunkSize>();
            if (!m_chunks.Store(chunk_idx, bitmap)) {
                bitmap->~AtomicBitmap<ChunkSize>();
                m_alloc.Deallocate(bitmap, sizeof(AtomicBitmap<ChunkSize>));
                return nullptr;
            }

            return bitmap;
        }

    private:
        XArray<AtomicBitmap<ChunkSize>, Alloc> m_chunks;
        Alloc                                   m_alloc;
        mutable SpinLock                        m_lock;
        mutable usize                           m_next_chunk_hint;
    };

} // namespace FoundationKitCxxStl::Structure
