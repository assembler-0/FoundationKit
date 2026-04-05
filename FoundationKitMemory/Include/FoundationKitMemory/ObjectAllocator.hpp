#pragma once

#include <FoundationKitMemory/MemoryOperations.hpp>
#include <FoundationKitMemory/SynchronizedAllocator.hpp>
#include <FoundationKitCxxStl/Base/Expected.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // ============================================================================
    // MemoryObjectType — User-extensible type tag
    // ============================================================================

    /// @brief Type tag for all managed objects.
    /// @desc Enables typed introspection (ForEachObject) and diagnostic crash walks.
    ///       Values 0x0000–0x00FF are reserved by FoundationKit.
    ///       Values 0x0100–0xFFFF are available for user-defined types.
    enum class MemoryObjectType : u16 {
        Unknown       = 0x0000,
        KernelStack   = 0x0001,
        PageTable     = 0x0002,
        TaskControl   = 0x0003,
        NetworkBuffer = 0x0004,
        FileHandle    = 0x0005,
        IrqContext    = 0x0006,
        // 0x0007–0x00FF reserved for future FoundationKit use.
        UserBase      = 0x0100, ///< First user-extensible value.
    };

    // ============================================================================
    // MemoryObjectFlags
    // ============================================================================

    enum class MemoryObjectFlags : u16 {
        None         = 0,
        Zeroed       = 1 << 0, ///< Memory was zero-initialised on allocation.
        Pinned       = 1 << 1, ///< Must not be reclaimed under memory pressure.
        DmaCoherent  = 1 << 2, ///< DMA-coherent; must not be cached in a write-back region.
        Executable   = 1 << 3, ///< Mapped in executable memory.
    };

    [[nodiscard]] constexpr MemoryObjectFlags operator|(MemoryObjectFlags a, MemoryObjectFlags b) noexcept {
        return static_cast<MemoryObjectFlags>(
            static_cast<u16>(a) | static_cast<u16>(b));
    }

    [[nodiscard]] constexpr bool HasFlag(MemoryObjectFlags flags, MemoryObjectFlags flag) noexcept {
        return (static_cast<u16>(flags) & static_cast<u16>(flag)) != 0;
    }

    // ============================================================================
    // MemoryObjectHeader — Inline metadata prepended to every managed object
    // ============================================================================

    /// @brief Header prepended immediately before each ObjectAllocator-managed payload.
    /// @desc Layout:
    ///   [MemoryObjectHeader]
    ///   [payload — the T instance]
    ///   (optional tail padding from alignment)
    ///
    ///       The `next` pointer forms a per-type intrusive singly-linked list walked
    ///       by ForEachObject. Heads are stored in ObjectAllocator::m_heads[].
    struct MemoryObjectHeader {
        static constexpr u32 kMagic = 0x4D4F424A; ///< 'MOBJ'

        u32                magic;      ///< Corruption sentinel.
        MemoryObjectType   type;       ///< Logical object type tag.
        MemoryObjectFlags  flags;      ///< Allocation attribute flags.
        u32                user_size;  ///< sizeof(T) as allocated.
        u32                checksum;   ///< CRC32 over {magic, type, flags, user_size}.
        MemoryObjectHeader* next;      ///< Next live object of the same type (intrusive list).

        /// @brief Compute a simple CRC32 over the header identity fields.
        /// @desc Not cryptographic — this is a corruption-detection sentinel only.
        [[nodiscard]] static constexpr u32 ComputeChecksum(
            u32 magic_v, MemoryObjectType type_v,
            MemoryObjectFlags flags_v, u32 user_size_v
        ) noexcept {
            // FNV-1a variant over the four fields, keeping it freestanding.
            constexpr u32 FNV_OFFSET = 0x811c9dc5u;
            constexpr u32 FNV_PRIME  = 0x01000193u;

            auto mix = [&](u32 acc, u32 word) constexpr noexcept -> u32 {
                for (int b = 0; b < 4; ++b) {
                    acc ^= static_cast<u32>(word & 0xFF);
                    acc *= FNV_PRIME;
                    word >>= 8;
                }
                return acc;
            };

            u32 acc = FNV_OFFSET;
            acc = mix(acc, magic_v);
            acc = mix(acc, static_cast<u32>(type_v));
            acc = mix(acc, static_cast<u32>(flags_v));
            acc = mix(acc, user_size_v);
            return acc;
        }

        void Verify() const noexcept {
            FK_BUG_ON(magic != kMagic,
                "MemoryObjectHeader: magic corruption (found: {:x}, expected: {:x})",
                magic, kMagic);

            const u32 expected = ComputeChecksum(magic, type, flags, user_size);
            FK_BUG_ON(checksum != expected,
                "MemoryObjectHeader: checksum mismatch (found: {:x}, expected: {:x}) "
                "— possible header corruption or use-after-free",
                checksum, expected);
        }
    };

    // ============================================================================
    // ObjectAllocator<Alloc, MaxTypes>
    // ============================================================================

    /// @brief Typed, tagged allocator for introspectable, leak-trackable objects.
    ///
    /// @desc Every allocation prepends a `MemoryObjectHeader` carrying the type tag,
    ///       checksum, and an intrusive next-pointer forming a per-type live-object list.
    ///       `ForEachObject<Type>()` walks this list to enumerate all live objects of a
    ///       given type — useful for GC, leak detection, and kernel diagnostics.
    ///
    ///       **Thread safety:** ObjectAllocator itself is NOT thread-safe. For concurrent
    ///       use, wrap the backing allocator with SynchronizedAllocator before passing:
    ///
    ///       ```cpp
    ///       FreeListAllocator                                 heap(buf, sizeof(buf));
    ///       SynchronizedAllocator<FreeListAllocator, SpinLock> safe_heap(heap);
    ///       ObjectAllocator<decltype(safe_heap)>              obj_alloc(safe_heap);
    ///       ```
    ///
    ///       The intrusive list heads (m_heads[]) are not atomically updated here;
    ///       if you need concurrent ForEachObject, the caller must hold a read lock.
    ///
    /// @tparam Alloc     Backing IAllocator.  Passed by reference — outlives ObjectAllocator.
    /// @tparam MaxTypes  Maximum distinct MemoryObjectType values tracked (default: 64).
    ///                   If a type index exceeds MaxTypes it is still allocated correctly
    ///                   but will NOT appear in ForEachObject walks.
    template <IAllocator Alloc, usize MaxTypes = 64>
    class ObjectAllocator {
    public:
        static_assert(MaxTypes >= 1,
            "ObjectAllocator: MaxTypes must be >= 1");

        explicit constexpr ObjectAllocator(Alloc& alloc) noexcept
            : m_alloc(alloc)
        {
            for (usize i = 0; i < MaxTypes; ++i) m_heads[i] = nullptr;
        }

        // Non-copyable — holds raw intrusive list.
        ObjectAllocator(const ObjectAllocator&) = delete;
        ObjectAllocator& operator=(const ObjectAllocator&) = delete;

        // ----------------------------------------------------------------
        // Allocate<Type, T>
        // ----------------------------------------------------------------

        /// @brief Allocate and construct a typed managed object.
        /// @tparam Type  MemoryObjectType tag.
        /// @tparam T     C++ type to construct.
        /// @tparam Args  Constructor parameter types (forwarded).
        /// @param  flags Optional allocation flags.
        /// @param  args  Arguments forwarded to T's constructor.
        /// @return Expected<T*, MemoryError>. On OOM: MemoryError::OutOfMemory.
        template <MemoryObjectType Type, typename T, typename... Args>
        [[nodiscard]] Expected<T*, MemoryError>
        Allocate(MemoryObjectFlags flags, Args&&... args) noexcept {
            // Check for overflow: Header + T + potential alignment padding
            constexpr usize header_size = sizeof(MemoryObjectHeader);
            if (sizeof(T) > (~static_cast<usize>(0)) - (header_size + alignof(T))) {
                return Unexpected(MemoryError::AllocationTooLarge);
            }

            constexpr usize total = header_size + sizeof(T);
            constexpr usize align = alignof(T) > alignof(MemoryObjectHeader)
                                    ? alignof(T)
                                    : alignof(MemoryObjectHeader);

            AllocationResult res = m_alloc.Allocate(total, align);
            if (!res) return Unexpected(MemoryError::OutOfMemory);

            // Ensure memory is zeroed BEFORE construction if requested.
            if (HasFlag(flags, MemoryObjectFlags::Zeroed)) {
                MemoryZero(res.ptr, total);
            }

            auto* hdr          = static_cast<MemoryObjectHeader*>(res.ptr);
            hdr->magic         = MemoryObjectHeader::kMagic;
            hdr->type          = Type;
            hdr->flags         = flags;
            hdr->user_size     = static_cast<u32>(sizeof(T));
            hdr->checksum      = MemoryObjectHeader::ComputeChecksum(
                hdr->magic, hdr->type, hdr->flags, hdr->user_size);
            hdr->next          = nullptr;

            // Link into per-type list.
            const usize type_idx = static_cast<usize>(Type);
            if (type_idx < MaxTypes) {
                hdr->next       = m_heads[type_idx];
                m_heads[type_idx] = hdr;
            }

            // Construct T in-place immediately after the header.
            T* obj = FoundationKitCxxStl::ConstructAt<T>(
                reinterpret_cast<T*>(hdr + 1),
                FoundationKitCxxStl::Forward<Args>(args)...
            );

            return obj;
        }

        /// @brief Convenience: Allocate with no flags.
        template <MemoryObjectType Type, typename T, typename... Args>
        [[nodiscard]] Expected<T*, MemoryError>
        Allocate(Args&&... args) noexcept {
            return Allocate<Type, T>(MemoryObjectFlags::None,
                                     FoundationKitCxxStl::Forward<Args>(args)...);
        }

        // ----------------------------------------------------------------
        // Deallocate<T>
        // ----------------------------------------------------------------

        /// @brief Destroy and deallocate a managed object.
        /// @desc Verifies the header magic + checksum before proceeding.
        ///       Unlinks the object from its per-type intrusive list.
        template <typename T>
        void Deallocate(T* ptr) noexcept {
            if (!ptr) return;

            auto* hdr = reinterpret_cast<MemoryObjectHeader*>(ptr) - 1;
            hdr->Verify();

            // Unlink from per-type list.
            const usize type_idx = static_cast<usize>(hdr->type);
            if (type_idx < MaxTypes) {
                MemoryObjectHeader** curr = &m_heads[type_idx];
                while (*curr && *curr != hdr) {
                    curr = &(*curr)->next;
                }
                if (*curr) {
                    *curr = hdr->next;
                }
            }

            // Destroy the object.
            ptr->~T();

            // Poison the header to catch use-after-free.
            hdr->magic = 0xDEADBEEF;

            constexpr usize total = sizeof(MemoryObjectHeader) + sizeof(T);
            m_alloc.Deallocate(hdr, total);
        }

        // ----------------------------------------------------------------
        // ForEachObject<Type>
        // ----------------------------------------------------------------

        /// @brief Walk all live objects of a given type and invoke func(T*) for each.
        /// @desc The intrusive list is iterated in reverse-allocation order (LIFO).
        ///       Do NOT allocate or deallocate objects of the same type inside `func`
        ///       unless using an external lock; doing so mutates the list under iteration.
        ///
        /// @tparam Type  The MemoryObjectType to walk.
        /// @tparam T     The managed C++ type (must match the type used in Allocate).
        /// @tparam Func  Callable of signature `void(T*)` or `void(T*, MemoryObjectHeader*)`.
        template <MemoryObjectType Type, typename T, typename Func>
        void ForEachObject(Func&& func) noexcept {
            const usize type_idx = static_cast<usize>(Type);
            if (type_idx >= MaxTypes) return;

            MemoryObjectHeader* hdr = m_heads[type_idx];
            while (hdr) {
                FK_BUG_ON(hdr->type != Type,
                    "ObjectAllocator::ForEachObject: type mismatch in intrusive list "
                    "(expected: {} found: {})",
                    static_cast<u16>(Type), static_cast<u16>(hdr->type));
                hdr->Verify();

                T* obj = reinterpret_cast<T*>(hdr + 1);

                if constexpr (requires { func(obj, hdr); }) {
                    func(obj, hdr);
                } else {
                    func(obj);
                }

                hdr = hdr->next;
            }
        }

        /// @brief Count live objects of a given type.
        [[nodiscard]] usize CountObjects(MemoryObjectType type) const noexcept {
            const usize type_idx = static_cast<usize>(type);
            if (type_idx >= MaxTypes) return 0;

            usize count = 0;
            const MemoryObjectHeader* hdr = m_heads[type_idx];
            while (hdr) {
                ++count;
                hdr = hdr->next;
            }
            return count;
        }

    private:
        Alloc&              m_alloc;
        MemoryObjectHeader* m_heads[MaxTypes];
    };

} // namespace FoundationKitMemory
