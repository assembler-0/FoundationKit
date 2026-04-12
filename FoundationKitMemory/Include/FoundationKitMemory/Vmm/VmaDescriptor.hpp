#pragma once

#include <FoundationKitMemory/Vmm/AddressTypes.hpp>
#include <FoundationKitMemory/RegionDescriptor.hpp>
#include <FoundationKitMemory/MemoryObject.hpp>
#include <FoundationKitCxxStl/Structure/IntrusiveRedBlackTree.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // VmaDescriptor
    // =========================================================================

    /// @brief Describes a single virtual memory area (VMA) in the kernel's address space.
    ///
    /// @desc  Embedded directly in the VirtualAddressSpace augmented RB-tree via
    ///        the `rb` intrusive node. The `subtree_max_gap` field is the augmentation
    ///        value: it stores the largest free gap (in bytes) anywhere in this node's
    ///        subtree. VirtualAddressSpace::Propagate() maintains this invariant after
    ///        every insert/remove/rotation, enabling O(log n) free-range search — the
    ///        same algorithm used by Linux mmap.c.
    ///
    ///        `backing == nullptr` means anonymous mapping.
    ///        `backing != nullptr` is opaque to the VMM; the kernel interprets it
    ///        (e.g. as a file inode pointer or device handle).
    struct VmaDescriptor {
        static constexpr MemoryObjectType kObjectType = MemoryObjectType::VirtualMemoryArea;
        RbNode         rb;               ///< Intrusive RB-tree node (must be first for zero-offset fast path).
        VirtualAddress base;             ///< Start of the mapped region (page-aligned).
        usize          size        = 0;  ///< Length in bytes (page-aligned, > 0 when live).
        usize          subtree_max_gap = 0; ///< Augmented: largest free gap in this subtree.
        RegionFlags    flags       = RegionFlags::None;
        void*          backing     = nullptr; ///< nullptr = anonymous; else kernel-supplied opaque handle.
        u64            backing_offset = 0;    ///< Byte offset into backing store.

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
    };

} // namespace FoundationKitMemory
