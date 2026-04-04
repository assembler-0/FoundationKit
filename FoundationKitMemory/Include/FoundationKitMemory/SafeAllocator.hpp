#pragma once

#include <FoundationKitMemory/MemoryCore.hpp>
#include <FoundationKitMemory/MemoryOperations.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // ============================================================================
    // SafeAllocator: Bounds-Checking Decorator
    // ============================================================================

    /// @brief Wraps an allocator and adds canaries around each allocation.
    /// @desc Detects buffer overflows and underflows during deallocation.
    ///       Uses an internal header to recover the original raw pointer.
    /// @warning NOT thread-safe. Inherits unsafety from BaseAllocator. For multi-threaded use, wrap with:
    ///          SynchronizedAllocator<SafeAllocator<BaseAllocator>, SpinLock>
    /// @tparam BaseAllocator Must satisfy IAllocator<BaseAllocator>
    /// @tparam CanarySize Number of bytes for the guard bands (default 16)
    template <IAllocator BaseAllocator, usize CanarySize = 16>
    class SafeAllocator {
    public:
        // ====================================================================
        // Allocation Header (metadata stored for recovery)
        // ====================================================================

        struct Header {
            u32   magic;          // Safety magic: 0x53414645 ('SAFE')
            u32   padding;        // Offset from raw_ptr to user_ptr
            void* raw_ptr;        // Original pointer from base allocator
            usize total_size;     // Original size from base allocator
            usize user_size;      // Size requested by user
        };

        static constexpr u32 HeaderMagic = 0x53414645;
        static constexpr byte HeadCanaryValue = 0xDE;
        static constexpr byte TailCanaryValue = 0xAD;

        // ====================================================================
        // Construction
        // ====================================================================

        explicit constexpr SafeAllocator(BaseAllocator& base) noexcept
            : m_base(base) {}

        // Non-copyable
        SafeAllocator(const SafeAllocator&) = delete;
        SafeAllocator& operator=(const SafeAllocator&) = delete;

        // ====================================================================
        // IAllocator Implementation
        // ====================================================================

        /// @brief Allocate memory with guard bands and metadata.
        [[nodiscard]] AllocationResult Allocate(usize size, const usize align) noexcept {
            if (size == 0) return AllocationResult::Failure(MemoryError::InvalidSize);

            // We need space for: Header + Head Canary + User Payload + Tail Canary
            // Plus extra for alignment alignment.
            constexpr usize header_and_canary = sizeof(Header) + CanarySize;
            const usize alignment_buffer = align > alignof(Header) ? align : alignof(Header);
            const usize total_requested = header_and_canary + size + CanarySize + alignment_buffer;

            AllocationResult res = m_base.Allocate(total_requested, alignof(Header));
            if (!res) return res;

            // Calculate user payload pointer with requested alignment
            const uptr raw_ptr = reinterpret_cast<uptr>(res.ptr);
            const uptr user_ptr = raw_ptr + header_and_canary + align - 1 & ~(align - 1);
            
            // Placement of other components relative to user_ptr
            const uptr head_canary_ptr = user_ptr - CanarySize;
            const uptr header_ptr = head_canary_ptr - sizeof(Header);
            const uptr tail_canary_ptr = user_ptr + size;

            // Fill Metadata
            auto* header = reinterpret_cast<Header*>(header_ptr);
            header->magic = HeaderMagic;
            header->padding = static_cast<u32>(user_ptr - raw_ptr);
            header->raw_ptr = res.ptr;
            header->total_size = res.size;
            header->user_size = size;

            // Fill Canaries
            MemorySet(reinterpret_cast<void*>(head_canary_ptr), HeadCanaryValue, CanarySize);
            MemorySet(reinterpret_cast<void*>(tail_canary_ptr), TailCanaryValue, CanarySize);

            return AllocationResult::Success(reinterpret_cast<void*>(user_ptr), size);
        }

        /// @brief Deallocate and verify integrity.
        void Deallocate(void* ptr, usize size) noexcept {
            if (!ptr) return;

            Header* header = GetHeader(ptr);
            FK_BUG_ON(header->magic != HeaderMagic,
                "SafeAllocator: Header magic mismatch (expected: {} got: {})", HeaderMagic, header->magic);
            FK_BUG_ON(header->user_size != size && size != 0,
                "SafeAllocator: Deallocate size ({}) mismatch ({})", header->user_size, size);

            // Verify integrity
            const byte* head_canary = static_cast<const byte*>(ptr) - CanarySize;
            const byte* tail_canary = static_cast<const byte*>(ptr) + header->user_size;

            for (usize i = 0; i < CanarySize; ++i) {
                FK_BUG_ON(head_canary[i] != HeadCanaryValue,
                    "SafeAllocator: Canaty corruption (underflow, expected: {} got: {})", HeadCanaryValue, head_canary[i]);
                FK_BUG_ON(tail_canary[i] != TailCanaryValue,
                    "SafeAllocator: Canaty corruption (overflow, expected: {} got: {})", TailCanaryValue, tail_canary[i]);
            }

            // Perform actual deallocation using original values
            m_base.Deallocate(header->raw_ptr, header->total_size);
        }

        /// @brief Check ownership.
        [[nodiscard]] bool Owns(const void* ptr) const noexcept {
            if (!ptr) return false;

            // First, check if the base allocator even considers this pointer in its range.
            // This is a safety check to avoid dereferencing wild/unmapped pointers.
            if (!m_base.Owns(ptr)) return false;

            const auto* header = reinterpret_cast<const Header*>(
                reinterpret_cast<const byte*>(ptr) - CanarySize - sizeof(Header)
            );

            if (header->magic != HeaderMagic) return false;
            return m_base.Owns(header->raw_ptr);
        }


        // ====================================================================
        // Private Helpers
        // ====================================================================

    private:
        [[nodiscard]] static Header* GetHeader(void* ptr) noexcept {
            return reinterpret_cast<Header*>(static_cast<byte*>(ptr) - CanarySize - sizeof(Header));
        }

        BaseAllocator& m_base;
    };

} // namespace FoundationKitMemory
