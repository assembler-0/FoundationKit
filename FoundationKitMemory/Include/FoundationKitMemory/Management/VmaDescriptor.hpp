#pragma once

#include <FoundationKitMemory/Management/AddressTypes.hpp>
#include <FoundationKitMemory/Management/RegionDescriptor.hpp>
#include <FoundationKitMemory/Core/MemoryObject.hpp>
#include <FoundationKitMemory/Ptr/SharedPtr.hpp>
#include <FoundationKitMemory/Management/VmObject.hpp>
#include <FoundationKitCxxStl/Structure/IntrusiveRedBlackTree.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // VmaProt — virtual memory area protection bits
    // =========================================================================

    /// @brief Page-level protection bits for a VMA.
    ///
    /// @desc  Separate from RegionFlags to allow mprotect-style runtime changes.
    ///        Protection changes are bounded by max_prot — you cannot mprotect
    ///        a mapping to be writable if it was created with max_prot = Read.
    enum class VmaProt : u8 {
        None    = 0,
        Read    = 1 << 0,
        Write   = 1 << 1,
        Execute = 1 << 2,
        User    = 1 << 3,

        // Common combinations
        ReadWrite       = Read | Write,
        ReadExecute     = Read | Execute,
        ReadWriteExecute = Read | Write | Execute,
        UserReadWrite   = User | Read | Write,
        UserReadExecute = User | Read | Execute,
        UserAll         = User | Read | Write | Execute,
    };

    [[nodiscard]] constexpr VmaProt operator|(VmaProt a, VmaProt b) noexcept {
        return static_cast<VmaProt>(static_cast<u8>(a) | static_cast<u8>(b));
    }
    [[nodiscard]] constexpr VmaProt operator&(VmaProt a, VmaProt b) noexcept {
        return static_cast<VmaProt>(static_cast<u8>(a) & static_cast<u8>(b));
    }
    [[nodiscard]] constexpr VmaProt operator~(VmaProt a) noexcept {
        return static_cast<VmaProt>(~static_cast<u8>(a));
    }
    [[nodiscard]] constexpr bool HasVmaProt(VmaProt flags, VmaProt flag) noexcept {
        return (static_cast<u8>(flags) & static_cast<u8>(flag)) != 0;
    }

    // =========================================================================
    // VmaFlags — mmap-style mapping flags
    // =========================================================================

    /// @brief Mapping behaviour flags, modeled after mmap(2) flags.
    enum class VmaFlags : u16 {
        None         = 0,
        Shared       = 1 << 0,   ///< Changes are visible to other processes mapping the same object.
        Private      = 1 << 1,   ///< CoW — changes are private to this address space.
        Anonymous    = 1 << 2,   ///< Not backed by a file — zero-filled on first fault.
        Fixed        = 1 << 3,   ///< Map at exactly the requested VA (no hint search).
        Stack        = 1 << 4,   ///< Stack mapping — grows downward, may have guard page.
        Guard        = 1 << 5,   ///< Guard page — faults are intentional (stack overflow trap).
        GrowsDown    = 1 << 6,   ///< Stack-style: auto-expand downward on fault below base.
        DontExpand   = 1 << 7,   ///< Do not auto-expand this VMA (mremap(2) MREMAP_DONTUNMAP).
        Locked       = 1 << 8,   ///< Pages are locked in memory (mlock(2) equivalent).
        DeviceMemory = 1 << 9,   ///< MMIO device memory — uncacheable, non-pageable.
        Populate     = 1 << 10,  ///< Pre-fault all pages at map time (MAP_POPULATE).
    };

    [[nodiscard]] constexpr VmaFlags operator|(VmaFlags a, VmaFlags b) noexcept {
        return static_cast<VmaFlags>(static_cast<u16>(a) | static_cast<u16>(b));
    }
    [[nodiscard]] constexpr VmaFlags operator&(VmaFlags a, VmaFlags b) noexcept {
        return static_cast<VmaFlags>(static_cast<u16>(a) & static_cast<u16>(b));
    }
    [[nodiscard]] constexpr bool HasVmaFlag(VmaFlags flags, VmaFlags flag) noexcept {
        return (static_cast<u16>(flags) & static_cast<u16>(flag)) != 0;
    }

    // =========================================================================
    // VmaDescriptor
    // =========================================================================

    /// @brief Describes a single virtual memory area (VMA) in an address space.
    ///
    /// @desc  Embedded directly in the VirtualAddressSpace augmented RB-tree via
    ///        the `rb` intrusive node. The `subtree_max_gap` field is the augmentation
    ///        value: it stores the largest free gap (in bytes) anywhere in this node's
    ///        subtree. VirtualAddressSpace::Propagate() maintains this invariant after
    ///        every insert/remove/rotation, enabling O(log n) free-range search — the
    ///        same algorithm used by Linux mmap.c.
    ///
    ///        Enhanced with:
    ///        - `prot` / `max_prot` — mprotect-style protection (mutable / immutable ceiling).
    ///        - `vma_flags` — mmap-style behaviour flags (Shared, Private, Anonymous, etc.)
    ///        - `name` — optional debug identifier for diagnostic output.
    struct VmaDescriptor {
        static constexpr MemoryObjectType kObjectType = MemoryObjectType::VirtualMemoryArea;

        RbNode         rb;               ///< Intrusive RB-tree node.
        VirtualAddress base;             ///< Start of the mapped region (page-aligned).
        usize          size        = 0;  ///< Length in bytes (page-aligned, > 0 when live).
        usize          subtree_max_gap = 0; ///< Augmented: largest free gap in this subtree.

        // --- Protection ---
        VmaProt        prot        = VmaProt::None;  ///< Current effective protection.
        VmaProt        max_prot    = VmaProt::None;  ///< Maximum allowed protection (ceiling).

        // --- Behaviour flags ---
        VmaFlags       vma_flags   = VmaFlags::None;

        // --- Legacy compatibility (derived from prot + vma_flags) ---
        RegionFlags    flags       = RegionFlags::None;

        // --- Backing store ---
        SharedPtr<VmObject> backing;             ///< null = not yet faulted anonymous; else VmObject.
        u64            backing_offset = 0;       ///< Byte offset into backing store.

        // --- Debug ---
        char           name[32]    = {};         ///< Optional human-readable label.

        // ----------------------------------------------------------------
        // Geometry
        // ----------------------------------------------------------------

        /// @brief Exclusive end address of this VMA.
        [[nodiscard]] constexpr VirtualAddress End() const noexcept {
            return {base.value + size};
        }

        /// @brief True if `va` falls within [base, base+size).
        [[nodiscard]] constexpr bool Contains(VirtualAddress va) const noexcept {
            return va.value >= base.value && va.value < base.value + size;
        }

        /// @brief True if [va, va+sz) overlaps this VMA.
        [[nodiscard]] constexpr bool Overlaps(VirtualAddress va, usize sz) const noexcept {
            return va.value < base.value + size && va.value + sz > base.value;
        }

        /// @brief Number of pages in this VMA.
        [[nodiscard]] constexpr usize PageCount() const noexcept {
            return size / kPageSize;
        }

        // ----------------------------------------------------------------
        // Protection API
        // ----------------------------------------------------------------

        /// @brief Change protection to new_prot. FK_BUG if it exceeds max_prot.
        void SetProtection(VmaProt new_prot) noexcept {
            FK_BUG_ON((static_cast<u8>(new_prot) & ~static_cast<u8>(max_prot)) != 0,
                "VmaDescriptor::SetProtection: requested prot ({:#x}) exceeds max_prot ({:#x}) "
                "at VMA base {:#x}",
                static_cast<u8>(new_prot), static_cast<u8>(max_prot), base.value);
            prot = new_prot;
        }

        // ----------------------------------------------------------------
        // Flag predicates
        // ----------------------------------------------------------------

        [[nodiscard]] bool IsShared()    const noexcept { return HasVmaFlag(vma_flags, VmaFlags::Shared); }
        [[nodiscard]] bool IsPrivate()   const noexcept { return HasVmaFlag(vma_flags, VmaFlags::Private); }
        [[nodiscard]] bool IsAnonymous() const noexcept { return HasVmaFlag(vma_flags, VmaFlags::Anonymous); }
        [[nodiscard]] bool IsGuard()     const noexcept { return HasVmaFlag(vma_flags, VmaFlags::Guard); }
        [[nodiscard]] bool IsStack()     const noexcept { return HasVmaFlag(vma_flags, VmaFlags::Stack); }
        [[nodiscard]] bool IsLocked()    const noexcept { return HasVmaFlag(vma_flags, VmaFlags::Locked); }
        [[nodiscard]] bool IsDevice()    const noexcept { return HasVmaFlag(vma_flags, VmaFlags::DeviceMemory); }
        [[nodiscard]] bool IsPopulate()  const noexcept { return HasVmaFlag(vma_flags, VmaFlags::Populate); }

        // ----------------------------------------------------------------
        // Name
        // ----------------------------------------------------------------

        /// @brief Set the debug name. Truncated to 31 characters + null.
        void SetName(const char* src) noexcept {
            if (!src) {
                name[0] = '\0';
                return;
            }
            usize i = 0;
            while (i < sizeof(name) - 1 && src[i] != '\0') {
                name[i] = src[i];
                ++i;
            }
            name[i] = '\0';
        }
    };

} // namespace FoundationKitMemory
