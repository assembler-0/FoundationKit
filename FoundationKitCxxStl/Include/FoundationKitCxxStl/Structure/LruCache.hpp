#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitCxxStl/Base/Optional.hpp>
#include <FoundationKitCxxStl/Base/Hash.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>
#include <FoundationKitMemory/AnyAllocator.hpp>

namespace FoundationKitCxxStl::Structure {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // LruCache<Key, T, Hasher, Alloc>
    //
    // ## Design
    //
    // A fixed-capacity LRU cache backed by a single flat allocation:
    //
    //   [ Slot[0] | Slot[1] | ... | Slot[capacity-1] ]
    //
    // Each Slot holds:
    //   - key, value          — the cached entry
    //   - lru_prev, lru_next  — indices into the slot array for the LRU list
    //   - state               — Empty | Occupied | Tombstone
    //
    // The hash table uses open addressing with linear probing. The LRU list
    // is an intrusive doubly-linked list threaded through the slot array by
    // index (not pointer), so the entire structure lives in one contiguous
    // allocation — one cache miss to reach any slot.
    //
    // ## Why indices instead of pointers for the LRU list
    //
    // The slot array may be reallocated if we ever support resize. More
    // importantly, indices are 4 bytes on 64-bit vs 8 bytes for pointers,
    // halving the list overhead per slot. On a 64-byte cache line, 4-byte
    // indices let us pack more slots per line.
    //
    // ## Sentinel
    //
    // kSentinel (= capacity) is the list head/tail sentinel. It is never a
    // valid slot index. The LRU list is doubly-linked and circular through
    // the sentinel: MRU end = sentinel.next, LRU end = sentinel.prev.
    //
    // ## Eviction
    //
    // When the cache is full and a new key is inserted, the LRU entry
    // (sentinel.prev) is evicted. If an EvictCallback is provided, it is
    // called with the evicted key and value before the slot is reused.
    //
    // ## Complexity
    //
    //   Get     O(1) amortised — hash probe + O(1) LRU splice
    //   Put     O(1) amortised — same, plus optional eviction
    //   Erase   O(1) amortised
    //   Peek    O(1)           — no LRU update
    //
    // ## Load factor
    //
    // The table is sized to capacity * kLoadFactorDenom / kLoadFactorNum
    // slots internally, keeping the load factor at ~75% to bound probe
    // lengths. The user-visible capacity is the number of *values* that
    // can be stored, not the internal slot count.
    //
    // =========================================================================

    /// @brief Fixed-capacity LRU cache.
    ///
    /// @tparam Key    Key type. Must be EqualityComparable and hashable by Hasher.
    /// @tparam T      Value type. Stored by value inside the slot.
    /// @tparam Hasher Hash functor: Hasher()(key) → u64.
    /// @tparam Alloc  Allocator satisfying FoundationKitMemory::IAllocator.
    template <
        typename Key,
        typename T,
        typename Hasher = Hash,
        FoundationKitMemory::IAllocator Alloc = FoundationKitMemory::AnyAllocator
    >
    class LruCache {
        static_assert(EqualityComparable<Key>,
            "LruCache: Key must be EqualityComparable");

        // Internal slot count = capacity * 4/3 (75% load factor).
        // Must be a power of two for the index mask trick.
        static constexpr usize kLoadNum   = 4;
        static constexpr usize kLoadDenom = 3;

        static constexpr u32 kEmpty     = 0;
        static constexpr u32 kOccupied  = 1;
        static constexpr u32 kTombstone = 2;

        // Sentinel index: points to the virtual head of the LRU list.
        // Stored as a u32 in the sentinel node itself; capacity is the value.
        static constexpr u32 kNoSlot = ~u32(0);

        struct Slot {
            Key   key;
            T     value;
            u32   lru_prev; // index of previous slot in LRU list (or sentinel)
            u32   lru_next; // index of next slot in LRU list (or sentinel)
            u32   hash;     // cached hash to avoid rehashing on probe
            u32   state;    // kEmpty | kOccupied | kTombstone
        };

        // The sentinel is a lightweight node that anchors the LRU list.
        // sentinel.lru_next = MRU end (most recently used)
        // sentinel.lru_prev = LRU end (least recently used, eviction candidate)
        struct Sentinel {
            u32 lru_prev;
            u32 lru_next;
        };

    public:
        /// @param capacity  Maximum number of entries. Must be > 0.
        /// @param alloc     Allocator instance.
        explicit LruCache(usize capacity, Alloc alloc = {}) noexcept
            : m_alloc(alloc)
            , m_slots(nullptr)
            , m_capacity(capacity)
            , m_slot_count(0)
            , m_size(0)
            , m_sentinel{kNoSlot, kNoSlot}
        {
            FK_BUG_ON(capacity == 0, "LruCache: capacity must be > 0");
            m_slot_count = NextPow2((capacity * kLoadNum + kLoadDenom - 1) / kLoadDenom);
            FK_BUG_ON(m_slot_count < capacity,
                "LruCache: internal slot count {} < capacity {} (overflow)", m_slot_count, capacity);

            auto res = m_alloc.Allocate(m_slot_count * sizeof(Slot), alignof(Slot));
            FK_BUG_ON(!res.ok(),
                "LruCache: failed to allocate {} slots ({} bytes)",
                m_slot_count, m_slot_count * sizeof(Slot));

            m_slots = static_cast<Slot*>(res.ptr);
            for (usize i = 0; i < m_slot_count; ++i)
                m_slots[i].state = kEmpty;

            // Empty LRU list: sentinel points to itself.
            m_sentinel.lru_prev = kNoSlot;
            m_sentinel.lru_next = kNoSlot;
        }

        ~LruCache() noexcept {
            if (m_slots) {
                // Destroy occupied values.
                for (usize i = 0; i < m_slot_count; ++i) {
                    if (m_slots[i].state == kOccupied) {
                        m_slots[i].key.~Key();
                        m_slots[i].value.~T();
                    }
                }
                m_alloc.Deallocate(m_slots, m_slot_count * sizeof(Slot));
            }
        }

        LruCache(const LruCache&)            = delete;
        LruCache& operator=(const LruCache&) = delete;

        // =====================================================================
        // Write side
        // =====================================================================

        /// @brief Insert or update a key-value pair.
        ///
        /// If the cache is full and the key is new, the LRU entry is evicted.
        /// If the key already exists, its value is updated and it is promoted
        /// to the MRU end.
        ///
        /// @param evict_cb  Optional: called with (key, value) of the evicted
        ///                  entry before it is overwritten. Pass nullptr to ignore.
        void Put(const Key& key, const T& value, void (*evict_cb)(const Key&, const T&) = nullptr) noexcept {
            const u32 h   = static_cast<u32>(Hasher{}(key));
            u32       idx = FindSlot(key, h);

            if (idx != kNoSlot && m_slots[idx].state == kOccupied) {
                // Update existing entry and promote to MRU.
                m_slots[idx].value = value;
                Promote(idx);
                return;
            }

            // New key. Evict LRU if at capacity.
            if (m_size == m_capacity) {
                FK_BUG_ON(m_sentinel.lru_prev == kNoSlot,
                    "LruCache::Put: cache is full but LRU list is empty (size={}, capacity={})",
                    m_size, m_capacity);
                EvictLru(evict_cb);
            }

            // Find an empty or tombstone slot via linear probe.
            u32 insert_idx = ProbeForInsert(h);
            FK_BUG_ON(insert_idx == kNoSlot,
                "LruCache::Put: no free slot found despite size={} < capacity={} "
                "(slot_count={}, possible hash table corruption)",
                m_size, m_capacity, m_slot_count);

            Slot& s = m_slots[insert_idx];
            if (s.state != kOccupied) {
                // Placement-construct key and value into the slot.
                new (&s.key)   Key(key);
                new (&s.value) T(value);
            } else {
                s.key   = key;
                s.value = value;
            }
            s.hash  = h;
            s.state = kOccupied;
            m_size++;

            // Link at MRU end.
            LinkMru(insert_idx);
        }

        /// @brief Look up a key, promote it to MRU, and return a pointer to its value.
        ///
        /// @return Pointer to the value, or nullptr if not found.
        [[nodiscard]] T* Get(const Key& key) noexcept {
            const u32 h   = static_cast<u32>(Hasher{}(key));
            const u32 idx = FindSlot(key, h);
            if (idx == kNoSlot || m_slots[idx].state != kOccupied) return nullptr;
            Promote(idx);
            return &m_slots[idx].value;
        }

        /// @brief Look up a key without updating LRU order.
        [[nodiscard]] const T* Peek(const Key& key) const noexcept {
            const u32 h   = static_cast<u32>(Hasher{}(key));
            const u32 idx = FindSlot(key, h);
            if (idx == kNoSlot || m_slots[idx].state != kOccupied) return nullptr;
            return &m_slots[idx].value;
        }

        /// @brief Remove a key from the cache.
        /// @return true if the key was present and removed.
        bool Erase(const Key& key) noexcept {
            const u32 h   = static_cast<u32>(Hasher{}(key));
            const u32 idx = FindSlot(key, h);
            if (idx == kNoSlot || m_slots[idx].state != kOccupied) return false;

            Unlink(idx);
            m_slots[idx].key.~Key();
            m_slots[idx].value.~T();
            m_slots[idx].state = kTombstone;
            m_size--;
            return true;
        }

        /// @brief Call func(key, value) for every entry, MRU → LRU order.
        template <Invocable<const Key&, const T&> Func>
        void ForEach(Func&& func) const noexcept {
            u32 idx = m_sentinel.lru_next;
            while (idx != kNoSlot) {
                FK_BUG_ON(m_slots[idx].state != kOccupied,
                    "LruCache::ForEach: non-occupied slot {} in LRU list (state={})",
                    idx, m_slots[idx].state);
                func(m_slots[idx].key, m_slots[idx].value);
                idx = m_slots[idx].lru_next;
            }
        }

        [[nodiscard]] bool  Empty()    const noexcept { return m_size == 0; }
        [[nodiscard]] usize Size()     const noexcept { return m_size; }
        [[nodiscard]] usize Capacity() const noexcept { return m_capacity; }

    private:
        // =====================================================================
        // Hash table probing
        // =====================================================================

        // Linear probe: find the slot occupied by (key, h), or kNoSlot.
        [[nodiscard]] u32 FindSlot(const Key& key, u32 h) const noexcept {
            const usize mask = m_slot_count - 1;
            usize       i    = h & mask;
            for (usize probe = 0; probe < m_slot_count; ++probe, i = (i + 1) & mask) {
                const Slot& s = m_slots[i];
                if (s.state == kEmpty) return kNoSlot; // probe chain broken
                if (s.state == kOccupied && s.hash == h && s.key == key)
                    return static_cast<u32>(i);
            }
            return kNoSlot;
        }

        // Linear probe: find an empty or tombstone slot for insertion.
        [[nodiscard]] u32 ProbeForInsert(u32 h) const noexcept {
            const usize mask      = m_slot_count - 1;
            usize       i         = h & mask;
            u32         tombstone = kNoSlot;
            for (usize probe = 0; probe < m_slot_count; ++probe, i = (i + 1) & mask) {
                const Slot& s = m_slots[i];
                if (s.state == kEmpty)
                    return tombstone != kNoSlot ? tombstone : static_cast<u32>(i);
                if (s.state == kTombstone && tombstone == kNoSlot)
                    tombstone = static_cast<u32>(i);
            }
            return tombstone;
        }

        // =====================================================================
        // LRU list manipulation (O(1) each)
        // =====================================================================

        // Link slot `idx` at the MRU end (sentinel.next).
        void LinkMru(u32 idx) noexcept {
            const u32 old_mru = m_sentinel.lru_next;
            m_slots[idx].lru_prev = kNoSlot;   // prev = sentinel
            m_slots[idx].lru_next = old_mru;
            if (old_mru != kNoSlot)
                m_slots[old_mru].lru_prev = idx;
            else
                m_sentinel.lru_prev = idx;      // list was empty; idx is also LRU
            m_sentinel.lru_next = idx;
        }

        // Unlink slot `idx` from the LRU list.
        void Unlink(u32 idx) noexcept {
            const u32 prev = m_slots[idx].lru_prev;
            const u32 next = m_slots[idx].lru_next;

            if (prev != kNoSlot) m_slots[prev].lru_next = next;
            else                 m_sentinel.lru_next    = next;

            if (next != kNoSlot) m_slots[next].lru_prev = prev;
            else                 m_sentinel.lru_prev    = prev;
        }

        // Move slot `idx` to the MRU end.
        void Promote(u32 idx) noexcept {
            if (m_sentinel.lru_next == idx) return; // already MRU
            Unlink(idx);
            LinkMru(idx);
        }

        // Evict the LRU entry (sentinel.prev).
        void EvictLru(void (*evict_cb)(const Key&, const T&)) noexcept {
            const u32 lru_idx = m_sentinel.lru_prev;
            FK_BUG_ON(lru_idx == kNoSlot,
                "LruCache::EvictLru: LRU sentinel.prev is kNoSlot (list empty but size={} > 0)",
                m_size);
            FK_BUG_ON(m_slots[lru_idx].state != kOccupied,
                "LruCache::EvictLru: LRU slot {} is not occupied (state={})",
                lru_idx, m_slots[lru_idx].state);

            if (evict_cb) evict_cb(m_slots[lru_idx].key, m_slots[lru_idx].value);

            Unlink(lru_idx);
            m_slots[lru_idx].key.~Key();
            m_slots[lru_idx].value.~T();
            m_slots[lru_idx].state = kTombstone;
            m_size--;
        }

        // =====================================================================
        // Utilities
        // =====================================================================

        [[nodiscard]] static usize NextPow2(usize n) noexcept {
            if (n == 0) return 1;
            n--;
            n |= n >> 1;
            n |= n >> 2;
            n |= n >> 4;
            n |= n >> 8;
            n |= n >> 16;
            n |= n >> 32;
            return n + 1;
        }

        Alloc    m_alloc;
        Slot*    m_slots;
        usize    m_capacity;   // user-visible max entries
        usize    m_slot_count; // internal table size (power of two)
        usize    m_size;       // current number of occupied entries
        Sentinel m_sentinel;   // LRU list head (not a slot)
    };

} // namespace FoundationKitCxxStl::Structure
