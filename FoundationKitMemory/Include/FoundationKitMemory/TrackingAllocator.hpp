#pragma once

#include <FoundationKitMemory/MemoryCore.hpp>
#include <FoundationKitMemory/MemoryOperations.hpp>
#include <FoundationKitMemory/MemorySafety.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // ============================================================================
    // TrackingAllocator: Header-Based Size-Tracking Decorator
    // ============================================================================

    /// @brief Wraps any IAllocator to track allocation sizes using a header.
    /// @desc Enables $O(1)$ size-less deallocation for ANY kernel allocator.
    ///       Stores metadata immediately before the user payload.
    /// @warning NOT thread-safe. Inherits unsafety from BaseAllocator. For multi-threaded use, wrap with:
    ///          SynchronizedAllocator<TrackingAllocator<BaseAllocator>, SpinLock>
    /// @tparam BaseAllocator Must satisfy IAllocator<BaseAllocator>
    template <IAllocator BaseAllocator>
    class TrackingAllocator {
    public:
        // ====================================================================
        // Allocation Header (metadata stored before payload)
        // ====================================================================

        struct Header {
            u32   magic;      // Safety magic: 0x5452414B ('TRAK')
            u32   padding;    // Padding used to maintain alignment
            usize size;       // Total size allocated from base (including header/padding)
            usize user_size;  // Size requested by user
        };

        static constexpr u32 HeaderMagic = 0x5452414B;

        // ====================================================================
        // Construction
        // ====================================================================

        explicit constexpr TrackingAllocator(BaseAllocator& base) noexcept
            : m_base(base) {}

        // Non-copyable
        TrackingAllocator(const TrackingAllocator&) = delete;
        TrackingAllocator& operator=(const TrackingAllocator&) = delete;

        // ====================================================================
        // IAllocator Implementation
        // ====================================================================

        /// @brief Allocate memory with a tracking header.
        [[nodiscard]] AllocationResult Allocate(usize size, usize align) noexcept {
            if (size == 0) return AllocationResult::Failure(MemoryError::InvalidSize);
            FK_BUG_ON(align == 0 || (align & (align - 1)) != 0,
                "TrackingAllocator::Allocate: alignment ({}) must be a non-zero power of two", align);

            const usize header_size = sizeof(Header);
            const usize alignment_needed = align > alignof(Header) ? align : alignof(Header);

            if (size > (~static_cast<usize>(0)) - (header_size + alignment_needed)) {
                return AllocationResult::Failure(MemoryError::AllocationTooLarge);
            }

            const usize total_size = header_size + size + alignment_needed;

            AllocationResult res = m_base.Allocate(total_size, alignment_needed);
            if (!res) return res;
            AssertAllocResultValid(res, total_size, alignment_needed);

            const uptr raw_ptr = reinterpret_cast<uptr>(res.ptr);
            const uptr payload_ptr = Alignment(align).AlignUp(raw_ptr + header_size);
            const uptr header_ptr = payload_ptr - header_size;

            // Sanity: header must fit between raw_ptr and payload_ptr.
            FK_BUG_ON(header_ptr < raw_ptr,
                "TrackingAllocator::Allocate: header placement underflows raw allocation");

            auto* header = reinterpret_cast<Header*>(header_ptr);
            header->magic = HeaderMagic;
            header->padding = static_cast<u32>(payload_ptr - raw_ptr);
            header->size = res.size;
            header->user_size = size;

            return AllocationResult::Success(reinterpret_cast<void*>(payload_ptr), size);
        }

        /// @brief Deallocate with size (verifies tracking info).
        void Deallocate(void* ptr, usize size) noexcept {
            if (!ptr) return;

            Header* header = GetHeader(ptr);
            FK_BUG_ON(header->magic != HeaderMagic,
                "TrackingAllocator: Header magic mismatch (expected: {} got: {})", HeaderMagic, header->magic);
            FK_BUG_ON(header->user_size != size && size != 0,
                "TrackingAllocator: Deallocate size ({}) mismatch ({})", header->user_size, size);

            void* raw_ptr = reinterpret_cast<byte*>(header) + sizeof(Header) - header->padding;
            m_base.Deallocate(raw_ptr, header->size);
        }

        /// @brief Deallocate without size (uses tracked size).
        void Deallocate(void* ptr) noexcept {
            Deallocate(ptr, 0);
        }

        /// @brief Check ownership.
        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            if (!ptr) return false;
            if (!m_base.Owns(ptr)) return false;

            // Guard: the header lives sizeof(Header) bytes before the payload.
            // If ptr is too close to the start of the allocator's range, the
            // header address would underflow into memory before the buffer —
            // a silent fault in a kernel. Only read the header if the base
            // allocator also owns the header region.
            const auto* header_addr = static_cast<const byte*>(ptr) - sizeof(Header);
            if (!m_base.Owns(header_addr)) return false;

            const auto* header = reinterpret_cast<const Header*>(header_addr);
            if (header->magic != HeaderMagic) return false;

            // Reconstruct raw_ptr: in Allocate(), raw_ptr is the base allocation
            // and padding = payload_ptr - raw_ptr, so raw_ptr = payload_ptr - padding.
            const auto* raw_ptr = static_cast<const byte*>(ptr) - header->padding;
            return m_base.Owns(raw_ptr);
        }

        // ====================================================================
        // Extended Capabilities
        // ====================================================================

        [[nodiscard]] AllocationResult Reallocate(void* ptr, usize old_size, usize new_size, usize align) noexcept {
            FK_BUG_ON(ptr == nullptr && old_size > 0,
                "TrackingAllocator::Reallocate: null ptr with non-zero old_size ({})", old_size);

            if (new_size == 0) {
                Deallocate(ptr, old_size);
                return AllocationResult::Failure();
            }

            if (!ptr) return Allocate(new_size, align);

            Header* header = GetHeader(ptr);
            FK_BUG_ON(header->magic != HeaderMagic,
                "TrackingAllocator::Reallocate: header magic mismatch (expected: {} got: {})", HeaderMagic, header->magic);
            FK_BUG_ON(old_size != 0 && old_size != header->user_size,
                "TrackingAllocator::Reallocate: old_size ({}) does not match tracked size ({})", old_size, header->user_size);

            if (new_size <= header->user_size && new_size > header->user_size / 2) {
                header->user_size = new_size;
                return AllocationResult::Success(ptr, new_size);
            }

            AllocationResult new_alloc = Allocate(new_size, align);
            if (!new_alloc) return new_alloc;

            // AssertNoOverlap: old and new buffers must not overlap before the copy.
            const usize copy_size = old_size < new_size ? old_size : new_size;
            AssertNoOverlap(ptr, copy_size, new_alloc.ptr, new_alloc.size);
            MemoryCopy(new_alloc.ptr, ptr, copy_size);

            Deallocate(ptr, old_size);
            return new_alloc;
        }

    private:
        [[nodiscard]] Header* GetHeader(void* ptr) const noexcept {
            return reinterpret_cast<Header*>(static_cast<byte*>(ptr) - sizeof(Header));
        }

        BaseAllocator& m_base;
    };

} // namespace FoundationKitMemory
