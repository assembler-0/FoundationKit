#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Pair.hpp>
#include <FoundationKitCxxStl/Base/Optional.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Safety.hpp>
#include <FoundationKitCxxStl/Base/Algorithm.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>

namespace FoundationKitCxxStl::Structure {

    // =========================================================================
    // FlatSet<K, N> — sorted, fixed-capacity, allocation-free set
    //
    // Backed by a plain array of K, kept sorted on every Insert so that
    // Contains / Find run in O(log N) via binary search.  Capacity is a
    // compile-time constant; exceeding it is a FK_BUG_ON.
    //
    // K must be TotallyOrdered so that LowerBound can compare elements.
    // =========================================================================

    /// @brief A sorted, fixed-capacity set backed by a stack array.
    /// @tparam K Key type. Must satisfy TotallyOrdered.
    /// @tparam N Maximum number of elements.
    template <TotallyOrdered K, usize N>
    class FlatSet {
        static_assert(N > 0, "FlatSet: N must be greater than zero");
        using _check = TypeSanityCheck<K>;
    public:
        using SizeType = usize;

        constexpr FlatSet() noexcept : m_size(0) {}

        /// @brief Insert a key. No-op if already present. FK_BUG_ON if full.
        /// @param key Key to insert.
        /// @returns true if inserted, false if already present.
        constexpr bool Insert(const K& key) noexcept {
            K* pos = LowerBound(m_data, m_data + m_size, key);
            if (pos != m_data + m_size && *pos == key) return false; // duplicate
            FK_BUG_ON(m_size >= N,
                "FlatSet::Insert: capacity ({}) exceeded ({})", m_size, N);
            // Shift elements right to make room.
            for (K* p = m_data + m_size; p != pos; --p) *p = Move(*(p - 1));
            *pos = key;
            ++m_size;
            return true;
        }

        /// @brief Remove a key. No-op if not present.
        /// @returns true if removed.
        constexpr bool Remove(const K& key) noexcept {
            K* pos = LowerBound(m_data, m_data + m_size, key);
            if (pos == m_data + m_size || !(*pos == key)) return false;
            for (K* p = pos; p + 1 != m_data + m_size; ++p) *p = Move(*(p + 1));
            --m_size;
            return true;
        }

        /// @brief Returns true if key is present.
        [[nodiscard]] constexpr bool Contains(const K& key) const noexcept {
            const K* pos = LowerBound(m_data, m_data + m_size, key);
            return pos != m_data + m_size && *pos == key;
        }

        [[nodiscard]] constexpr SizeType Size()     const noexcept { return m_size; }
        [[nodiscard]] constexpr bool     Empty()    const noexcept { return m_size == 0; }
        [[nodiscard]] static constexpr SizeType Capacity() noexcept { return N; }

        constexpr const K* begin() const noexcept { return m_data; }
        constexpr const K* end()   const noexcept { return m_data + m_size; }

    private:
        K     m_data[N];
        usize m_size;
    };

    // =========================================================================
    // FlatMap<K, V, N> — sorted, fixed-capacity, allocation-free map
    //
    // Stores Pair<K,V> sorted by key.  Lookup is O(log N) binary search.
    // Insert / Remove are O(N) due to the shift, which is acceptable for the
    // small, static tables this targets (syscall tables, IRQ maps, etc.).
    // =========================================================================

    /// @brief A sorted, fixed-capacity key-value map backed by a stack array.
    /// @tparam K Key type. Must satisfy TotallyOrdered.
    /// @tparam V Value type.
    /// @tparam N Maximum number of entries.
    template <TotallyOrdered K, typename V, usize N>
    class FlatMap {
        static_assert(N > 0, "FlatMap: N must be greater than zero");
        using _kcheck = TypeSanityCheck<K>;
        using _vcheck = TypeSanityCheck<V>;
        using Entry   = Pair<K, V>;

        // Comparator that orders Entry by key only.
        struct ByKey {
            constexpr bool operator()(const Entry& e, const K& k) const noexcept { return e.first < k; }
            constexpr bool operator()(const K& k, const Entry& e) const noexcept { return k < e.first; }
        };

    public:
        using SizeType = usize;

        constexpr FlatMap() noexcept : m_size(0) {}

        /// @brief Insert or update a key-value pair.
        /// @returns true if inserted (new key), false if updated (existing key).
        constexpr bool Insert(const K& key, const V& value) noexcept {
            Entry* first = m_data;
            Entry* last  = m_data + m_size;
            // LowerBound with key comparator: find first entry whose key >= key.
            Entry* pos = first;
            {
                isize count = static_cast<isize>(m_size);
                while (count > 0) {
                    isize step = count / 2;
                    Entry* it = pos + step;
                    if (it->first < key) { pos = it + 1; count -= step + 1; }
                    else count = step;
                }
            }
            if (pos != last && pos->first == key) { pos->second = value; return false; }
            FK_BUG_ON(m_size >= N,
                "FlatMap::Insert: capacity ({}) exceeded — increase N", N);
            for (Entry* p = m_data + m_size; p != pos; --p) *p = Move(*(p - 1));
            pos->first  = key;
            pos->second = value;
            ++m_size;
            return true;
        }

        /// @brief Remove an entry by key.
        /// @returns true if removed.
        constexpr bool Remove(const K& key) noexcept {
            Entry* pos = FindEntry(key);
            if (!pos) return false;
            for (Entry* p = pos; p + 1 != m_data + m_size; ++p) *p = Move(*(p + 1));
            --m_size;
            return true;
        }

        /// @brief Look up a value by key.
        /// @returns Optional reference to the value, empty if not found.
        [[nodiscard]] constexpr Optional<V&> Find(const K& key) noexcept {
            Entry* e = FindEntry(key);
            if (!e) return NullOpt;
            return e->second;
        }

        [[nodiscard]] constexpr Optional<const V&> Find(const K& key) const noexcept {
            const Entry* e = FindEntry(key);
            if (!e) return NullOpt;
            return e->second;
        }

        /// @brief Returns true if the key exists.
        [[nodiscard]] constexpr bool Contains(const K& key) const noexcept {
            return FindEntry(key) != nullptr;
        }

        /// @brief Subscript operator — FK_BUG_ON if key not found.
        [[nodiscard]] constexpr V& operator[](const K& key) noexcept {
            Entry* e = FindEntry(key);
            FK_BUG_ON(!e, "FlatMap::operator[]: key not found");
            return e->second;
        }

        [[nodiscard]] constexpr const V& operator[](const K& key) const noexcept {
            const Entry* e = FindEntry(key);
            FK_BUG_ON(!e, "FlatMap::operator[]: key not found");
            return e->second;
        }

        [[nodiscard]] constexpr SizeType Size()     const noexcept { return m_size; }
        [[nodiscard]] constexpr bool     Empty()    const noexcept { return m_size == 0; }
        [[nodiscard]] static constexpr SizeType Capacity() noexcept { return N; }

        constexpr Entry*       begin()       noexcept { return m_data; }
        constexpr Entry*       end()         noexcept { return m_data + m_size; }
        constexpr const Entry* begin() const noexcept { return m_data; }
        constexpr const Entry* end()   const noexcept { return m_data + m_size; }

    private:
        [[nodiscard]] constexpr Entry* FindEntry(const K& key) noexcept {
            Entry* pos = m_data;
            auto count = static_cast<isize>(m_size);
            while (count > 0) {
                isize step = count / 2;
                Entry* it = pos + step;
                if (it->first < key) { pos = it + 1; count -= step + 1; }
                else count = step;
            }
            if (pos != m_data + m_size && pos->first == key) return pos;
            return nullptr;
        }

        [[nodiscard]] constexpr const Entry* FindEntry(const K& key) const noexcept {
            const Entry* pos = m_data;
            auto count = static_cast<isize>(m_size);
            while (count > 0) {
                isize step = count / 2;
                const Entry* it = pos + step;
                if (it->first < key) { pos = it + 1; count -= step + 1; }
                else count = step;
            }
            if (pos != m_data + m_size && pos->first == key) return pos;
            return nullptr;
        }

        Entry m_data[N];
        usize m_size;
    };

} // namespace FoundationKitCxxStl::Structure
