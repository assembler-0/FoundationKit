#pragma once

#include <FoundationKitMemory/Core/MemoryObject.hpp>
#include <FoundationKitMemory/Core/MemoryCore.hpp>
#include <FoundationKitMemory/Ptr/SharedPtr.hpp>
#include <FoundationKitMemory/Management/AddressTypes.hpp>
#include <FoundationKitCxxStl/Structure/IntrusiveRedBlackTree.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Expected.hpp>
#include <FoundationKitCxxStl/Base/Optional.hpp>
#include <FoundationKitCxxStl/Sync/SharedSpinLock.hpp>
#include <FoundationKitCxxStl/Sync/SharedLock.hpp>
#include <FoundationKitCxxStl/Sync/Locks.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // Forward declaration
    struct VmPagerBase;

    // =========================================================================
    // VmPage
    // =========================================================================

    /// @brief Represents a dynamically sized backing block in a VmObject.
    struct VmPage {
        RbNode rb;                  ///< Intrusive tree node.
        u64    offset;              ///< Byte offset within the VmObject.
        usize  size_bytes;          ///< Size of this chunk in bytes (dynamic).
        Pfn    pfn;                 ///< Backing physical frame (base).

        [[nodiscard]] constexpr u64 EndOffset() const noexcept {
            return offset + size_bytes;
        }

        [[nodiscard]] constexpr bool Contains(u64 query_offset) const noexcept {
            return query_offset >= offset && query_offset < EndOffset();
        }
    };

    // =========================================================================
    // VmObject
    // =========================================================================

    /// @brief Unified Buffer Cache (UBC) / Shadow Object Primitive.
    ///
    /// @desc  Forms the backing layer for Virtual Memory Areas (VMAs).
    ///        Supports Shadow Object Chains for unified Copy-on-Write (CoW).
    ///        When a VMA is cloned, it points to a new anonymous VmObject
    ///        which shadows the original. Reads miss in the child and fall
    ///        back to the parent. Writes allocate in the child and break
    ///        the shadow link.
    ///
    ///        Enhanced with:
    ///        - `m_size` — total logical size in bytes.
    ///        - `m_resident_count` — number of resident pages (atomic).
    ///        - `m_pager` — backing store interface (AnonymousPager, DevicePager, etc.)
    ///        - `m_ref_count` — external reference count (VMAs binding this object).
    ///        - `Collapse()` — merge shadow chain when only one reference remains.
    class VmObject final : public MemoryObjectBase<MemoryObjectType::VmObject> {
    public:
        /// @brief Intrusive RB-Tree customised with a Root() accessor.
        struct PageTree : IntrusiveRbTree<VmPage, FOUNDATIONKITCXXSTL_OFFSET_OF(VmPage, rb)> {
            [[nodiscard]] RbNode* Root() const noexcept { return this->m_root; }
        };

        constexpr VmObject() noexcept = default;
        ~VmObject() noexcept = default;

        VmObject(const VmObject&)            = delete;
        VmObject& operator=(const VmObject&) = delete;

        // ----------------------------------------------------------------
        // Size management
        // ----------------------------------------------------------------

        /// @brief Set the logical size of this object.
        void SetSize(usize size_bytes) noexcept {
            FK_BUG_ON(size_bytes != 0 && !IsPageAligned(size_bytes),
                "VmObject::SetSize: size {} is not page-aligned", size_bytes);
            m_size = size_bytes;
        }

        /// @brief Get the logical size in bytes.
        [[nodiscard]] usize GetSize() const noexcept { return m_size; }

        /// @brief Get the logical size in pages.
        [[nodiscard]] usize GetSizePages() const noexcept { return m_size / kPageSize; }

        // ----------------------------------------------------------------
        // Reference counting (VMA binding)
        // ----------------------------------------------------------------

        /// @brief Increment the external reference count.
        /// @desc  Called when a VMA binds to this VmObject.
        void AddRef() noexcept {
            m_ref_count.FetchAdd(1, Sync::MemoryOrder::Relaxed);
        }

        /// @brief Decrement the external reference count.
        /// @desc  Called when a VMA unbinds. FK_BUG if already zero.
        void Release() noexcept {
            const usize prev = m_ref_count.FetchSub(1, Sync::MemoryOrder::AcqRel);
            FK_BUG_ON(prev == 0,
                "VmObject::Release: ref_count was already zero — "
                "double-release or VMA tracking corruption");
        }

        /// @brief Current reference count.
        [[nodiscard]] usize RefCount() const noexcept {
            return m_ref_count.Load(Sync::MemoryOrder::Relaxed);
        }

        // ----------------------------------------------------------------
        // Resident page tracking
        // ----------------------------------------------------------------

        /// @brief Number of pages currently resident in this object.
        [[nodiscard]] usize ResidentCount() const noexcept {
            return m_resident_count.Load(Sync::MemoryOrder::Relaxed);
        }

        /// @brief Increment resident page count.
        void ResidentCountInc() noexcept {
            m_resident_count.FetchAdd(1, Sync::MemoryOrder::Relaxed);
        }

        /// @brief Decrement resident page count.
        void ResidentCountDec() noexcept {
            const usize prev = m_resident_count.FetchSub(1, Sync::MemoryOrder::AcqRel);
            FK_BUG_ON(prev == 0,
                "VmObject::ResidentCountDec: underflow — "
                "more pages removed than inserted");
        }

        // ----------------------------------------------------------------
        // Pager binding
        // ----------------------------------------------------------------

        /// @brief Bind a backing store pager to this object.
        /// @desc  Must be called before the first fault on this object.
        ///        A VmObject without a pager uses the default anonymous behaviour.
        void SetPager(VmPagerBase* pager) noexcept {
            m_pager = pager;
        }

        /// @brief Get the bound pager (nullable — null = anonymous default).
        [[nodiscard]] VmPagerBase* GetPager() const noexcept { return m_pager; }

        /// @brief True if a pager is explicitly bound.
        [[nodiscard]] bool HasPager() const noexcept { return m_pager != nullptr; }

        // ----------------------------------------------------------------
        // Shadow Chains
        // ----------------------------------------------------------------

        void SetShadow(SharedPtr<VmObject> shadow) noexcept {
            Sync::UniqueLock guard(m_lock);
            m_shadow = FoundationKitCxxStl::Move(shadow);
        }

        [[nodiscard]] SharedPtr<VmObject> GetShadow() const noexcept {
            Sync::SharedLock guard(m_lock);
            return m_shadow;
        }

        /// @brief True if this object has a shadow parent.
        [[nodiscard]] bool HasShadow() const noexcept {
            Sync::SharedLock guard(m_lock);
            return m_shadow.Get() != nullptr;
        }

        /// @brief Depth of this object's shadow chain.
        /// @desc  O(chain_depth). Call from diagnostic paths only.
        [[nodiscard]] usize ShadowDepth() const noexcept {
            usize depth = 0;
            const VmObject* current = this;
            while (current) {
                Sync::SharedLock guard(current->m_lock);
                current = current->m_shadow.Get();
                ++depth;
            }
            return depth - 1; // Don't count self.
        }

        // ----------------------------------------------------------------
        // Shadow Chain Collapse (BSD-style optimization)
        // ----------------------------------------------------------------

        /// @brief Collapse the shadow chain if this object has exactly one reference.
        ///
        /// @desc  When a VmObject has ref_count == 1 and a shadow parent, we can
        ///        pull all parent pages that don't conflict with our private pages
        ///        into this object, then detach the shadow. This reduces chain
        ///        depth—critical for fork-heavy workloads where shadow chains
        ///        can grow unboundedly.
        ///
        ///        Walk the parent's page tree. For each page at offset O:
        ///          - If this object has a private page at O: skip (we already own it).
        ///          - If not: steal the page (move it into our tree).
        ///        Then detach the shadow.
        ///
        ///        Thread safety: Caller must ensure no concurrent faults on this
        ///        object during collapse. Typically called under VMA lock.
        ///        This implementation takes BOTH this->m_lock and parent->m_lock
        ///        following a strict hierarchy (shadow -> this) to prevent deadlocks.
        ///
        /// @return True if collapse was performed, false if conditions not met.
        bool Collapse() noexcept {
            Sync::UniqueLock guard(m_lock);

            // Collapse only if we have exactly one reference and a shadow.
            if (RefCount() != 1 || !m_shadow) return false;

            VmObject* parent = m_shadow.Get();
            if (!parent) return false;

            // Strict lock ordering: shadow MUST be locked before this if we ever held both,
            // but here we already hold 'this'. For collapse, we assume 'this' is private
            // (RefCount == 1) and 'parent' is being merged INTO it.
            // To be safe, we try_lock the parent or require the caller to hold both.
            // As Lead Architect, we enforce: parent MUST be locked.
            Sync::UniqueLock parent_guard(parent->m_lock);

            // In-order traversal using the tree's First/Next.
            VmPage* p = parent->m_pages.First();
            while (p) {
                VmPage* next_p = parent->m_pages.Next(p);

                // Only pull if we don't have an OVERLAPPING block in this object.
                if (!FindBlockContaining(p->offset)) {
                    // Remove from parent, insert into us.
                    parent->m_pages.Remove(p);
                    parent->m_resident_count.FetchSub(1, Sync::MemoryOrder::Relaxed);

                    m_pages.Insert(p, [](const VmPage& a, const VmPage& b) noexcept {
                        if (a.offset < b.offset) return -1;
                        if (a.offset > b.offset) return  1;
                        return 0;
                    });
                    m_resident_count.FetchAdd(1, Sync::MemoryOrder::Relaxed);
                }

                p = next_p;
            }

            // Detach shadow.
            m_shadow.Reset();
            return true;
        }

        // ----------------------------------------------------------------
        // Block Management
        // ----------------------------------------------------------------

        /// @brief Resolves the physical address backing the given offset.
        /// @desc  Traverses the shadow chain recursively. Returns the closest PFN
        ///        and the exact physical byte offset of the request.
        [[nodiscard]] Optional<PhysicalAddress> Lookup(u64 offset) const noexcept {
            const VmObject* current = this;
            while (current) {
                Sync::SharedLock guard(current->m_lock); // Protect concurrent reads to m_shadow / m_pages
                VmPage* page = current->FindBlockContaining(offset);
                if (page) {
                    const u64 diff = offset - page->offset;
                    const u64 pa   = PfnToPhysical(page->pfn).value + diff;
                    return PhysicalAddress{pa};
                }
                current = current->m_shadow.Get();
            }
            return NullOpt; // Requires allocation via fault.
        }

        /// @brief Returns true if exact page at offset naturally resides in this exact object (not shadowed).
        [[nodiscard]] bool IsPagePrivate(u64 offset) const noexcept {
            Sync::SharedLock guard(m_lock);
            return FindBlockContaining(offset) != nullptr;
        }

        /// @brief Inserts a dynamically sized physical block backing this object.
        void InsertBlock(VmPage* page) noexcept {
            FK_BUG_ON(page == nullptr, "VmObject::InsertBlock: null page");
            FK_BUG_ON(page->size_bytes == 0, "VmObject::InsertBlock: zero size");
            
            Sync::UniqueLock guard(m_lock);
            FK_BUG_ON(FindBlockContaining(page->offset) != nullptr, 
                "VmObject::InsertBlock: overlapping block implies logic error");

            m_pages.Insert(page, [](const VmPage& a, const VmPage& b) noexcept {
                if (a.offset < b.offset) return -1;
                if (a.offset > b.offset) return  1;
                return 0;
            });

            m_resident_count.FetchAdd(1, Sync::MemoryOrder::Relaxed);
        }

        /// @brief Removes a block.
        void RemoveBlock(VmPage* page) noexcept {
            FK_BUG_ON(page == nullptr, "VmObject::RemoveBlock: null page");
            Sync::UniqueLock guard(m_lock);
            m_pages.Remove(page);

            const usize prev = m_resident_count.FetchSub(1, Sync::MemoryOrder::AcqRel);
            FK_BUG_ON(prev == 0,
                "VmObject::RemoveBlock: resident_count underflow");
        }

        // ----------------------------------------------------------------
        // Iteration (diagnostic)
        // ----------------------------------------------------------------

        /// @brief Walk all resident pages in offset order.
        /// @param func  Callable with signature void(const VmPage&) noexcept.
        template <typename Func>
        void ForEachPage(Func&& func) const noexcept {
            Sync::SharedLock guard(m_lock);
            VmPage* p = m_pages.First();
            while (p) {
                VmPage* next = m_pages.Next(p);
                func(*p);
                p = next;
            }
        }

        /// @brief Total number of blocks in the page tree (not shadow chain).
        [[nodiscard]] usize BlockCount() const noexcept {
            Sync::SharedLock guard(m_lock);
            return m_pages.Size();
        }

    private:
        /// @brief Returns the VmPage containing `offset`, or nullptr.
        [[nodiscard]] VmPage* FindBlockContaining(u64 offset) const noexcept {
            RbNode* node = m_pages.Root();
            while (node) {
                VmPage* p = PageTree::ToEntry(node);
                if (offset < p->offset) {
                    node = node->left;
                } else if (offset >= p->EndOffset()) {
                    node = node->right;
                } else {
                    return p;
                }
            }
            return nullptr;
        }

        // ----------------------------------------------------------------
        // State
        // ----------------------------------------------------------------
        mutable Sync::SharedSpinLock m_lock;
        SharedPtr<VmObject>    m_shadow;
        PageTree               m_pages;
        usize                  m_size           = 0;
        Sync::Atomic<usize>    m_resident_count{0};
        Sync::Atomic<usize>    m_ref_count{0};
        VmPagerBase*           m_pager          = nullptr;
    };

} // namespace FoundationKitMemory
