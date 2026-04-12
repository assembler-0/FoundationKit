#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>
#include <FoundationKitMemory/AnyAllocator.hpp>

namespace FoundationKitCxxStl::Structure {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // IntrusiveMinHeap — binary min-heap with O(log n) DecreaseKey/IncreaseKey
    //
    // ## Why intrusive
    //
    // A standard binary heap stores elements by value in a flat array. To
    // cancel a timer or reprioritise a scheduler entity, you need to find the
    // element first — O(n) scan. The intrusive variant embeds a HeapNode in
    // each element that records the element's current array index. This makes
    // DecreaseKey and IncreaseKey O(log n): load the index, sift up or down.
    //
    // This is the same technique used by Linux's timerqueue (hrtimer) and by
    // Dijkstra implementations in network stacks.
    //
    // ## HeapNode
    //
    // Each element T must embed a HeapNode member. The heap stores T* pointers
    // in its internal array and keeps HeapNode::index in sync on every swap.
    // An index of kInvalidIndex means the element is not in any heap.
    //
    // ## Compare
    //
    // Compare(a, b) must return true if a has *higher priority* than b (i.e.,
    // should be closer to the root). For a min-heap on deadlines:
    //   Compare = [](const T& a, const T& b) { return a.deadline < b.deadline; }
    //
    // ## Static vs Dynamic
    //
    // StaticIntrusiveMinHeap<T, NodeOffset, Capacity, Compare>:
    //   - Fixed-size array on the stack / BSS.
    //   - ISR-safe: no allocation, no OS calls.
    //   - FK_BUG_ON on overflow (capacity is a design-time constant).
    //
    // DynamicIntrusiveMinHeap<T, NodeOffset, Compare, Alloc>:
    //   - Heap-backed pointer array, grown on demand.
    //   - NOT ISR-safe.
    //   - Uses FoundationKitMemory::AnyAllocator by default.
    //
    // ## Complexity
    //
    //   Push        O(log n)
    //   Pop         O(log n)
    //   Peek        O(1)
    //   DecreaseKey O(log n)   — sift up
    //   IncreaseKey O(log n)   — sift down
    //   Remove      O(log n)   — swap with last, sift both directions
    //   Contains    O(1)       — check HeapNode::index != kInvalidIndex
    // =========================================================================

    /// @brief Intrusive heap node. Embed one in each element type T.
    struct HeapNode {
        static constexpr usize kInvalidIndex = ~usize(0);
        usize index = kInvalidIndex;

        [[nodiscard]] bool InHeap() const noexcept { return index != kInvalidIndex; }
    };

    namespace detail {

        /// @brief Core heap logic shared by static and dynamic variants.
        ///
        /// Operates on a caller-supplied T** array of length `size`.
        /// All index bookkeeping is done here; callers manage storage.
        template <typename T, usize NodeOffset, typename Compare>
        struct HeapOps {
            static HeapNode* NodeOf(T* entry) noexcept {
                return reinterpret_cast<HeapNode*>(reinterpret_cast<uptr>(entry) + NodeOffset);
            }

            static void SetIndex(T* entry, usize idx) noexcept {
                NodeOf(entry)->index = idx;
            }

            static void Swap(T** arr, usize i, usize j) noexcept {
                T* tmp = arr[i];
                arr[i] = arr[j];
                arr[j] = tmp;
                SetIndex(arr[i], i);
                SetIndex(arr[j], j);
            }

            /// @brief Sift element at `idx` toward the root until the heap
            ///        property is restored. Returns the final index.
            static usize SiftUp(T** arr, usize idx, Compare& cmp) noexcept {
                while (idx > 0) {
                    const usize parent = (idx - 1) / 2;
                    if (cmp(*arr[idx], *arr[parent])) {
                        Swap(arr, idx, parent);
                        idx = parent;
                    } else {
                        break;
                    }
                }
                return idx;
            }

            /// @brief Sift element at `idx` toward the leaves until the heap
            ///        property is restored. Returns the final index.
            static usize SiftDown(T** arr, usize size, usize idx, Compare& cmp) noexcept {
                for (;;) {
                    usize best  = idx;
                    usize left  = 2 * idx + 1;
                    usize right = 2 * idx + 2;
                    if (left  < size && cmp(*arr[left],  *arr[best])) best = left;
                    if (right < size && cmp(*arr[right], *arr[best])) best = right;
                    if (best == idx) break;
                    Swap(arr, idx, best);
                    idx = best;
                }
                return idx;
            }
        };

    } // namespace detail

    // =========================================================================
    // StaticIntrusiveMinHeap
    // =========================================================================

    /// @brief ISR-safe intrusive min-heap with compile-time capacity.
    ///
    /// @tparam T          Container type embedding a HeapNode member.
    /// @tparam NodeOffset Byte offset of HeapNode within T (use FOUNDATIONKITCXXSTL_OFFSET_OF).
    /// @tparam Capacity   Maximum number of elements. Fixed at compile time.
    /// @tparam Compare    Strict-weak-ordering functor: Compare(a, b) → bool.
    template <typename T, usize NodeOffset, usize Capacity, typename Compare>
    class StaticIntrusiveMinHeap {
        static_assert(Capacity > 0,
            "StaticIntrusiveMinHeap: Capacity must be > 0");
        static_assert(NodeOffset < 65536,
            "StaticIntrusiveMinHeap: NodeOffset exceeds 65535 bytes — did you swap T and NodeOffset?");

        using Ops = detail::HeapOps<T, NodeOffset, Compare>;

    public:
        explicit StaticIntrusiveMinHeap(Compare cmp = {}) noexcept
            : m_size(0), m_cmp(cmp) {}

        StaticIntrusiveMinHeap(const StaticIntrusiveMinHeap&)            = delete;
        StaticIntrusiveMinHeap& operator=(const StaticIntrusiveMinHeap&) = delete;

        /// @brief Insert an element. O(log n). ISR-safe.
        void Push(T* entry) noexcept {
            FK_BUG_ON(entry == nullptr,
                "StaticIntrusiveMinHeap::Push: null entry");
            FK_BUG_ON(Ops::NodeOf(entry)->InHeap(),
                "StaticIntrusiveMinHeap::Push: entry is already in a heap (double-insert)");
            FK_BUG_ON(m_size >= Capacity,
                "StaticIntrusiveMinHeap::Push: heap is full (Capacity={}, size={})",
                Capacity, m_size);

            const usize idx = m_size++;
            m_arr[idx] = entry;
            Ops::SetIndex(entry, idx);
            Ops::SiftUp(m_arr, idx, m_cmp);
        }

        /// @brief Remove and return the minimum element. O(log n). ISR-safe.
        /// @return The minimum element, or nullptr if empty.
        T* Pop() noexcept {
            if (m_size == 0) return nullptr;
            T* top = m_arr[0];
            RemoveAt(0);
            return top;
        }

        /// @brief Return the minimum element without removing it. O(1).
        [[nodiscard]] T* Peek() const noexcept {
            return m_size > 0 ? m_arr[0] : nullptr;
        }

        /// @brief Re-establish the heap property after the key of `entry`
        ///        decreased (i.e., its priority increased). O(log n).
        ///
        /// Call this after modifying the field that Compare reads, when the
        /// new value makes `entry` higher priority (closer to root) than before.
        void DecreaseKey(T* entry) noexcept {
            FK_BUG_ON(entry == nullptr,
                "StaticIntrusiveMinHeap::DecreaseKey: null entry");
            FK_BUG_ON(!Ops::NodeOf(entry)->InHeap(),
                "StaticIntrusiveMinHeap::DecreaseKey: entry is not in this heap");
            const usize idx = Ops::NodeOf(entry)->index;
            FK_BUG_ON(idx >= m_size,
                "StaticIntrusiveMinHeap::DecreaseKey: index {} out of range (size={})",
                idx, m_size);
            Ops::SiftUp(m_arr, idx, m_cmp);
        }

        /// @brief Re-establish the heap property after the key of `entry`
        ///        increased (i.e., its priority decreased). O(log n).
        void IncreaseKey(T* entry) noexcept {
            FK_BUG_ON(entry == nullptr,
                "StaticIntrusiveMinHeap::IncreaseKey: null entry");
            FK_BUG_ON(!Ops::NodeOf(entry)->InHeap(),
                "StaticIntrusiveMinHeap::IncreaseKey: entry is not in this heap");
            const usize idx = Ops::NodeOf(entry)->index;
            FK_BUG_ON(idx >= m_size,
                "StaticIntrusiveMinHeap::IncreaseKey: index {} out of range (size={})",
                idx, m_size);
            Ops::SiftDown(m_arr, m_size, idx, m_cmp);
        }

        /// @brief Remove an arbitrary element. O(log n).
        ///
        /// This is the timer-cancellation path: given a pointer to a timer
        /// that is somewhere in the heap, remove it in O(log n) without
        /// scanning the array.
        void Remove(T* entry) noexcept {
            FK_BUG_ON(entry == nullptr,
                "StaticIntrusiveMinHeap::Remove: null entry");
            FK_BUG_ON(!Ops::NodeOf(entry)->InHeap(),
                "StaticIntrusiveMinHeap::Remove: entry is not in this heap");
            const usize idx = Ops::NodeOf(entry)->index;
            FK_BUG_ON(idx >= m_size,
                "StaticIntrusiveMinHeap::Remove: index {} out of range (size={})",
                idx, m_size);
            RemoveAt(idx);
        }

        /// @brief Returns true if `entry` is currently in this heap. O(1).
        [[nodiscard]] bool Contains(const T* entry) const noexcept {
            if (!entry) return false;
            const HeapNode* n = reinterpret_cast<const HeapNode*>(
                reinterpret_cast<uptr>(entry) + NodeOffset);
            return n->InHeap() && n->index < m_size && m_arr[n->index] == entry;
        }

        [[nodiscard]] bool  Empty()    const noexcept { return m_size == 0; }
        [[nodiscard]] usize Size()     const noexcept { return m_size; }
        static constexpr usize MaxCapacity() noexcept { return Capacity; }

    private:
        void RemoveAt(usize idx) noexcept {
            // Mark the removed element as no longer in the heap.
            Ops::NodeOf(m_arr[idx])->index = HeapNode::kInvalidIndex;

            const usize last = --m_size;
            if (idx == last) return; // removed the last element, nothing to fix

            // Move the last element into the vacated slot, then sift both ways.
            // We must try both directions because the last element may be either
            // smaller or larger than the removed element's former neighbours.
            m_arr[idx] = m_arr[last];
            Ops::SetIndex(m_arr[idx], idx);

            const usize after_up = Ops::SiftUp(m_arr, idx, m_cmp);
            if (after_up == idx) {
                // SiftUp did nothing — the element may need to go down.
                Ops::SiftDown(m_arr, m_size, idx, m_cmp);
            }
        }

        T*      m_arr[Capacity];
        usize   m_size;
        Compare m_cmp;
    };

    // =========================================================================
    // DynamicIntrusiveMinHeap
    // =========================================================================

    /// @brief Allocator-backed intrusive min-heap with runtime capacity.
    ///
    /// NOT ISR-safe. Grows by doubling when full.
    ///
    /// @tparam T          Container type embedding a HeapNode member.
    /// @tparam NodeOffset Byte offset of HeapNode within T.
    /// @tparam Compare    Strict-weak-ordering functor.
    /// @tparam Alloc      Allocator satisfying FoundationKitMemory::IAllocator.
    template <typename T, usize NodeOffset, typename Compare,
              FoundationKitMemory::IAllocator Alloc = FoundationKitMemory::AnyAllocator>
    class DynamicIntrusiveMinHeap {
        static_assert(NodeOffset < 65536,
            "DynamicIntrusiveMinHeap: NodeOffset exceeds 65535 bytes — did you swap T and NodeOffset?");

        using Ops = detail::HeapOps<T, NodeOffset, Compare>;

        static constexpr usize kInitialCapacity = 16;

    public:
        explicit DynamicIntrusiveMinHeap(Compare cmp = {}, Alloc alloc = {}) noexcept
            : m_alloc(alloc), m_cmp(cmp), m_arr(nullptr), m_size(0), m_capacity(0)
        {
            Grow(kInitialCapacity);
        }

        ~DynamicIntrusiveMinHeap() noexcept {
            if (m_arr) m_alloc.Deallocate(m_arr, m_capacity * sizeof(T*), alignof(T*));
        }

        DynamicIntrusiveMinHeap(const DynamicIntrusiveMinHeap&)            = delete;
        DynamicIntrusiveMinHeap& operator=(const DynamicIntrusiveMinHeap&) = delete;

        /// @brief Insert an element. O(log n). NOT ISR-safe (may allocate).
        void Push(T* entry) noexcept {
            FK_BUG_ON(entry == nullptr,
                "DynamicIntrusiveMinHeap::Push: null entry");
            FK_BUG_ON(Ops::NodeOf(entry)->InHeap(),
                "DynamicIntrusiveMinHeap::Push: entry is already in a heap (double-insert)");

            if (m_size == m_capacity) Grow(m_capacity * 2);

            const usize idx = m_size++;
            m_arr[idx] = entry;
            Ops::SetIndex(entry, idx);
            Ops::SiftUp(m_arr, idx, m_cmp);
        }

        /// @brief Remove and return the minimum element. O(log n).
        T* Pop() noexcept {
            if (m_size == 0) return nullptr;
            T* top = m_arr[0];
            RemoveAt(0);
            return top;
        }

        [[nodiscard]] T* Peek() const noexcept {
            return m_size > 0 ? m_arr[0] : nullptr;
        }

        /// @brief Re-establish heap property after key decrease. O(log n).
        void DecreaseKey(T* entry) noexcept {
            FK_BUG_ON(entry == nullptr,
                "DynamicIntrusiveMinHeap::DecreaseKey: null entry");
            FK_BUG_ON(!Ops::NodeOf(entry)->InHeap(),
                "DynamicIntrusiveMinHeap::DecreaseKey: entry is not in this heap");
            const usize idx = Ops::NodeOf(entry)->index;
            FK_BUG_ON(idx >= m_size,
                "DynamicIntrusiveMinHeap::DecreaseKey: index {} out of range (size={})",
                idx, m_size);
            Ops::SiftUp(m_arr, idx, m_cmp);
        }

        /// @brief Re-establish heap property after key increase. O(log n).
        void IncreaseKey(T* entry) noexcept {
            FK_BUG_ON(entry == nullptr,
                "DynamicIntrusiveMinHeap::IncreaseKey: null entry");
            FK_BUG_ON(!Ops::NodeOf(entry)->InHeap(),
                "DynamicIntrusiveMinHeap::IncreaseKey: entry is not in this heap");
            const usize idx = Ops::NodeOf(entry)->index;
            FK_BUG_ON(idx >= m_size,
                "DynamicIntrusiveMinHeap::IncreaseKey: index {} out of range (size={})",
                idx, m_size);
            Ops::SiftDown(m_arr, m_size, idx, m_cmp);
        }

        /// @brief Remove an arbitrary element. O(log n).
        void Remove(T* entry) noexcept {
            FK_BUG_ON(entry == nullptr,
                "DynamicIntrusiveMinHeap::Remove: null entry");
            FK_BUG_ON(!Ops::NodeOf(entry)->InHeap(),
                "DynamicIntrusiveMinHeap::Remove: entry is not in this heap");
            const usize idx = Ops::NodeOf(entry)->index;
            FK_BUG_ON(idx >= m_size,
                "DynamicIntrusiveMinHeap::Remove: index {} out of range (size={})",
                idx, m_size);
            RemoveAt(idx);
        }

        [[nodiscard]] bool Contains(const T* entry) const noexcept {
            if (!entry) return false;
            const HeapNode* n = reinterpret_cast<const HeapNode*>(
                reinterpret_cast<uptr>(entry) + NodeOffset);
            return n->InHeap() && n->index < m_size && m_arr[n->index] == entry;
        }

        [[nodiscard]] bool  Empty()    const noexcept { return m_size == 0; }
        [[nodiscard]] usize Size()     const noexcept { return m_size; }
        [[nodiscard]] usize Capacity() const noexcept { return m_capacity; }

    private:
        void RemoveAt(usize idx) noexcept {
            Ops::NodeOf(m_arr[idx])->index = HeapNode::kInvalidIndex;
            const usize last = --m_size;
            if (idx == last) return;

            m_arr[idx] = m_arr[last];
            Ops::SetIndex(m_arr[idx], idx);

            const usize after_up = Ops::SiftUp(m_arr, idx, m_cmp);
            if (after_up == idx) Ops::SiftDown(m_arr, m_size, idx, m_cmp);
        }

        void Grow(usize new_cap) noexcept {
            FK_BUG_ON(new_cap == 0,
                "DynamicIntrusiveMinHeap::Grow: new_cap is zero");
            const auto res = m_alloc.Allocate(new_cap * sizeof(T*), alignof(T*));
            FK_BUG_ON(!res.ok(),
                "DynamicIntrusiveMinHeap::Grow: allocation of {} pointer slots failed", new_cap);

            T** new_arr = static_cast<T**>(res.ptr);

            // Copy existing pointers; indices are already correct.
            for (usize i = 0; i < m_size; ++i) new_arr[i] = m_arr[i];

            if (m_arr) m_alloc.Deallocate(m_arr, m_capacity * sizeof(T*), alignof(T*));
            m_arr      = new_arr;
            m_capacity = new_cap;
        }

        Alloc  m_alloc;
        Compare m_cmp;
        T**    m_arr;
        usize  m_size;
        usize  m_capacity;
    };

} // namespace FoundationKitCxxStl::Structure
