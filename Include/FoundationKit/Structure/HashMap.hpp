#pragma once

#include <FoundationKit/Base/Types.hpp>
#include <FoundationKit/Base/Utility.hpp>
#include <FoundationKit/Base/Hash.hpp>
#include <FoundationKit/Base/Vector.hpp>
#include <FoundationKit/Base/Optional.hpp>
#include <FoundationKit/Base/Pair.hpp>

namespace FoundationKit::Structure {

    /// @brief A hash map implementation using open addressing and linear probing.
    /// @tparam K Key type.
    /// @tparam V Value type.
    /// @tparam H Hash function.
    /// @tparam Alloc Allocator for the underlying storage.
    template <typename K, typename V, typename H = Hash, Memory::IAllocator Alloc = Memory::AnyAllocator>
    class HashMap {
    public:
        enum class BucketStatus : u8 {
            Empty,
            Occupied,
            Deleted
        };

        struct Bucket {
            K            key;
            V            value;
            BucketStatus status = BucketStatus::Empty;

            Bucket() = default;
        };

        explicit HashMap(Alloc allocator = Alloc())
            : m_buckets(FoundationKit::Move(allocator)), m_size(0) {
            // Start with a reasonable power-of-two capacity
            m_buckets.Resize(16);
        }

        ~HashMap() = default;

        template <typename... Args>
        bool Insert(const K& key, Args&&... args) {
            if (m_size >= m_buckets.Size() * 0.7) {
                if (!Rehash(m_buckets.Size() * 2)) return false;
            }

            usize idx = FindBucket(key);
            if (m_buckets[idx].status == BucketStatus::Occupied) {
                m_buckets[idx].value = V(FoundationKit::Forward<Args>(args)...);
                return true;
            }

            m_buckets[idx].key = key;
            m_buckets[idx].value = V(FoundationKit::Forward<Args>(args)...);
            m_buckets[idx].status = BucketStatus::Occupied;
            m_size++;
            return true;
        }

        Optional<V&> Get(const K& key) {
            usize idx = FindBucket(key);
            if (m_buckets[idx].status == BucketStatus::Occupied) {
                return m_buckets[idx].value;
            }
            return {};
        }

        Optional<const V&> Get(const K& key) const {
            usize idx = FindBucket(key);
            if (m_buckets[idx].status == BucketStatus::Occupied) {
                return m_buckets[idx].value;
            }
            return {};
        }

        bool Remove(const K& key) {
            usize idx = FindBucket(key);
            if (m_buckets[idx].status == BucketStatus::Occupied) {
                m_buckets[idx].status = BucketStatus::Deleted;
                m_size--;
                return true;
            }
            return false;
        }

        [[nodiscard]] usize Size() const noexcept { return m_size; }
        [[nodiscard]] bool Empty() const noexcept { return m_size == 0; }

    private:
        usize FindBucket(const K& key) const {
            const u64 h = H{}(key);
            const usize mask = m_buckets.Size() - 1;
            auto idx = h & mask;
            auto first_deleted = static_cast<usize>(-1);

            while (m_buckets[idx].status != BucketStatus::Empty) {
                if (m_buckets[idx].status == BucketStatus::Occupied && m_buckets[idx].key == key) {
                    return idx;
                }
                if (m_buckets[idx].status == BucketStatus::Deleted && first_deleted == static_cast<usize>(-1)) {
                    first_deleted = idx;
                }
                idx = (idx + 1) & mask;
            }

            return first_deleted != static_cast<usize>(-1) ? first_deleted : idx;
        }

        bool Rehash(usize new_capacity) {
            Vector<Bucket, Alloc> old_buckets = FoundationKit::Move(m_buckets);
            if (!m_buckets.Resize(new_capacity)) {
                m_buckets = FoundationKit::Move(old_buckets);
                return false;
            }

            m_size = 0;
            for (auto& bucket : old_buckets) {
                if (bucket.status == BucketStatus::Occupied) {
                    Insert(bucket.key, FoundationKit::Move(bucket.value));
                }
            }
            return true;
        }

        Vector<Bucket, Alloc> m_buckets;
        usize                 m_size;
    };

} // namespace FoundationKit::Structure
