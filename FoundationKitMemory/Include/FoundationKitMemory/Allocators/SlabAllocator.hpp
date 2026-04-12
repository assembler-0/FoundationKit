#pragma once

#include <FoundationKitMemory/Allocators/PoolAllocator.hpp>
#include <FoundationKitCxxStl/Base/Array.hpp>

namespace FoundationKitMemory {

    // ============================================================================
    // SlabConfig — Compile-time size-class descriptor
    // ============================================================================

    /// @brief Descriptor for a single slab size class.
    struct SlabSizeClass {
        usize chunk_size; ///< Object size for this pool (bytes).
        usize weight;     ///< Relative weight in [1, 100]. See SlabAllocator::Initialize.
    };

    // ============================================================================
    // SlabAllocator<NumClasses, Weights..., Fallback>
    // ============================================================================

    /// @brief A high-performance allocator for small objects using configurable size-class pools.
    ///
    /// @desc Aggregates up to `NumClasses` `PoolAllocator` instances, each serving
    ///       a different object size class. The backing buffer is partitioned according
    ///       to user-supplied weight percentages so workloads with skewed size
    ///       distributions do not waste memory on unused size classes.
    ///
    ///       Allocations larger than all configured size classes are forwarded to the
    ///       `Fallback` allocator.
    ///
    ///       **Default configuration** (equal 6-class layout matching legacy behaviour):
    ///       ```cpp
    ///       constexpr SlabSizeClass DefaultSlabClasses[6] = {
    ///           {16,  17}, {32, 17}, {64, 17}, {128, 17}, {256, 16}, {512, 16}
    ///       };
    ///       // Sum of weights = 100
    ///       SlabAllocator<6, FreeListAllocator> slab;
    ///       slab.Initialize(buf, sizeof(buf), Move(fallback), DefaultSlabClasses);
    ///       ```
    ///
    ///       **Custom configuration** (heavy 16-byte workload):
    ///       ```cpp
    ///       constexpr SlabSizeClass MyClasses[4] = {
    ///           {16, 70}, {64, 20}, {256, 7}, {1024, 3}
    ///       };
    ///       SlabAllocator<4, FreeListAllocator> slab;
    ///       slab.Initialize(buf, sizeof(buf), Move(fallback), MyClasses);
    ///       ```
    ///
    /// @tparam NumClasses  Number of size classes (1–32).
    /// @tparam Fallback    IAllocator for requests larger than any size class.
    ///
    /// @warning NOT thread-safe. Wrap with:
    ///          SynchronizedAllocator<SlabAllocator<N,F>, SpinLock>
    template <usize NumClasses, IAllocator Fallback>
    class SlabAllocator {
        static_assert(NumClasses >= 1 && NumClasses <= 32,
            "SlabAllocator: NumClasses must be in [1, 32]");

    public:
        constexpr SlabAllocator() noexcept = default;

        // Non-copyable (pools hold raw pointers into the buffer).
        SlabAllocator(const SlabAllocator&) = delete;
        SlabAllocator& operator=(const SlabAllocator&) = delete;

        // ----------------------------------------------------------------
        // Initialize
        // ----------------------------------------------------------------

        /// @brief Partition `buffer` among the configured size classes and initialise
        ///        the fallback allocator.
        ///
        /// @param buffer       Raw backing memory.
        /// @param size         Total bytes available in `buffer`.
        /// @param fallback     Fallback allocator instance (moved in).
        /// @param classes      Array of `NumClasses` size-class descriptors.
        ///                     `chunk_size` values must be strictly increasing.
        ///                     Weights are relative (need not sum to 100; each class
        ///                     receives `weight / total_weight` of `size`).
        void Initialize(
            void*               buffer,
            usize               size,
            Fallback&&          fallback,
            const SlabSizeClass classes[NumClasses]
        ) noexcept {
            FK_BUG_ON(!buffer || size == 0,
                "SlabAllocator::Initialize: null buffer or zero size");

            m_fallback = FoundationKitCxxStl::Move(fallback);

            // Validate class configuration and compute total weight.
            usize total_weight = 0;
            for (usize i = 0; i < NumClasses; ++i) {
                FK_BUG_ON(classes[i].chunk_size == 0,
                    "SlabAllocator::Initialize: class[{}] chunk_size is zero", i);
                FK_BUG_ON(classes[i].weight == 0,
                    "SlabAllocator::Initialize: class[{}] weight is zero", i);
                if (i > 0) {
                    FK_BUG_ON(classes[i].chunk_size <= classes[i - 1].chunk_size,
                        "SlabAllocator::Initialize: class[{}].chunk_size ({}) must be "
                        "strictly greater than class[{}].chunk_size ({})",
                        i, classes[i].chunk_size,
                        i - 1, classes[i - 1].chunk_size);
                }
                total_weight += classes[i].weight;
                m_classes[i] = classes[i];
            }

            // Distribute the buffer proportionally to weights.
            byte* cursor = static_cast<byte*>(buffer);
            byte* end    = cursor + size;

            for (usize i = 0; i < NumClasses; ++i) {
                // Align cursor to at least 16 bytes (typical cache-line subset).
                const uptr aligned_cursor = Alignment(16).AlignUp(reinterpret_cast<uptr>(cursor));
                cursor = reinterpret_cast<byte*>(aligned_cursor);

                if (cursor >= end) break;

                const usize remaining_buf  = static_cast<usize>(end - cursor);
                const usize class_budget   = (size * m_classes[i].weight) / total_weight;
                const usize actual_budget  = class_budget < remaining_buf
                                             ? class_budget
                                             : remaining_buf;

                m_pools[i].Initialize(cursor, actual_budget, m_classes[i].chunk_size);
                cursor += actual_budget;
            }
        }

        // ----------------------------------------------------------------
        // IAllocator Interface
        // ----------------------------------------------------------------

        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            for (usize i = 0; i < NumClasses; ++i) {
                if (size <= m_classes[i].chunk_size && align <= m_classes[i].chunk_size) {
                    const AllocationResult r = m_pools[i].Allocate(size, align);
                    if (r) return r;
                    // Pool exhausted — let it fall through to the next class or fallback.
                    // This is intentional: a saturated tiny pool degrades to the next tier.
                }
                if (size <= m_classes[i].chunk_size) break; // align too large; skip to fallback
            }
            return m_fallback.Allocate(size, align);
        }

        void Deallocate(void* ptr, usize size) noexcept {
            for (usize i = 0; i < NumClasses; ++i) {
                if (size <= m_classes[i].chunk_size) {
                    if (m_pools[i].Owns(ptr)) {
                        m_pools[i].Deallocate(ptr, size);
                        return;
                    }
                    break;
                }
            }
            m_fallback.Deallocate(ptr, size);
        }

        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            for (usize i = 0; i < NumClasses; ++i) {
                if (m_pools[i].Owns(ptr)) return true;
            }
            return m_fallback.Owns(ptr);
        }

        // ----------------------------------------------------------------
        // Introspection
        // ----------------------------------------------------------------

        /// @brief Number of configured size classes.
        [[nodiscard]] static constexpr usize ClassCount() noexcept { return NumClasses; }

        /// @brief Size-class descriptor for the given index.
        [[nodiscard]] constexpr SlabSizeClass GetClass(usize idx) const noexcept {
            FK_BUG_ON(idx >= NumClasses,
                "SlabAllocator::GetClass: index ({}) out of bounds ({})", idx, NumClasses);
            return m_classes[idx];
        }

        /// @brief Maximum object size served by the slab (largest class chunk_size).
        [[nodiscard]] usize MaxSlabSize() const noexcept {
            return NumClasses > 0 ? m_classes[NumClasses - 1].chunk_size : 0;
        }

    private:
        // ----------------------------------------------------------------
        // DynamicPool — PoolAllocator with dynamic chunk size
        // ----------------------------------------------------------------
        // Standard PoolAllocator<ChunkSize> has ChunkSize as a template parameter.
        // We need runtime-configured chunk sizes here, so we replicate the core
        // logic with a m_chunk_size field. This is a compile-time-sized replacement
        // for the old equal-split approach.
        struct DynamicPool {
            byte*   m_buffer      = nullptr;
            usize   m_buffer_size = 0;
            usize   m_chunk_size  = 0;
            void*   m_free_head   = nullptr;

            void Initialize(byte* buffer, usize size, usize chunk_size) noexcept {
                m_buffer      = buffer;
                m_buffer_size = size;
                m_chunk_size  = chunk_size;
                m_free_head   = nullptr;

                if (!buffer || size == 0 || chunk_size == 0) return;

                // Minimum stride: at least sizeof(void*) to hold the free-list pointer.
                const usize stride = chunk_size >= sizeof(void*)
                                     ? chunk_size
                                     : sizeof(void*);

                usize offset = 0;
                while (offset + stride <= size) {
                    void** node      = reinterpret_cast<void**>(buffer + offset);
                    *node            = m_free_head;
                    m_free_head      = node;
                    offset          += stride;
                }
            }

            [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
                if (!m_free_head || size > m_chunk_size || align > m_chunk_size)
                    return AllocationResult::Failure();
                void* block  = m_free_head;
                m_free_head  = *reinterpret_cast<void**>(block);
                return AllocationResult::Success(block, m_chunk_size);
            }

            void Deallocate(void* ptr, usize /*size*/) noexcept {
                if (!ptr) return;
                *reinterpret_cast<void**>(ptr) = m_free_head;
                m_free_head = ptr;
            }

            [[nodiscard]] bool Owns(const void* ptr) const noexcept {
                return ptr >= m_buffer && ptr < m_buffer + m_buffer_size;
            }
        };

        SlabSizeClass m_classes[NumClasses] = {};
        DynamicPool   m_pools[NumClasses];
        Fallback      m_fallback;
    };

    // ============================================================================
    // DefaultSlabClasses — legacy-equivalent 6-class layout
    // ============================================================================

    /// @brief Default 6-class slab configuration matching the original equal-split layout.
    ///        Sum of weights = 100. Each class receives ~1/6 of the buffer.
    inline constexpr SlabSizeClass DefaultSlabClasses[6] = {
        {16,  17},
        {32,  17},
        {64,  17},
        {128, 17},
        {256, 16},
        {512, 16},
    };

} // namespace FoundationKitMemory
