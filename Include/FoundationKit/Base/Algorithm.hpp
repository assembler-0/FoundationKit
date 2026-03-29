#pragma once

#include <FoundationKit/Base/Types.hpp>
#include <FoundationKit/Base/Utility.hpp>
#include <FoundationKit/Meta/Concepts.hpp>

namespace FoundationKit {

    /// @brief Default comparator.
    struct Less {
        template <typename T, typename U>
        constexpr bool operator()(const T& lhs, const U& rhs) const {
            return lhs < rhs;
        }
    };

    template <typename T>
    constexpr const T& Min(const T& a, const T& b) {
        return (a < b) ? a : b;
    }

    template <typename T>
    constexpr const T& Max(const T& a, const T& b) {
        return (a < b) ? b : a;
    }

    template <typename T>
    constexpr const T& Clamp(const T& v, const T& lo, const T& hi) {
        return (v < lo) ? lo : (hi < v) ? hi : v;
    }

    namespace Detail {
        // --- Insertion Sort ---
        template <RandomAccessIterator I, typename Comp>
        constexpr void InsertionSort(I first, I last, Comp comp) {
            if (first == last) return;
            for (I i = first + 1; i != last; ++i) {
                auto val = FoundationKit::Move(*i);
                I j = i;
                while (j > first && comp(val, *(j - 1))) {
                    *j = FoundationKit::Move(*(j - 1));
                    --j;
                }
                *j = FoundationKit::Move(val);
            }
        }

        // --- Heapsort ---
        template <RandomAccessIterator I, typename Comp>
        constexpr void SiftDown(I first, isize start, isize end, Comp comp) {
            isize root = start;
            while (root * 2 + 1 <= end) {
                isize child = root * 2 + 1;
                isize swap = root;
                if (comp(*(first + swap), *(first + child))) swap = child;
                if (child + 1 <= end && comp(*(first + swap), *(first + child + 1))) swap = child + 1;
                if (swap == root) return;
                FoundationKit::Swap(*(first + root), *(first + swap));
                root = swap;
            }
        }

        template <RandomAccessIterator I, typename Comp>
        constexpr void HeapSort(I first, I last, Comp comp) {
            isize count = last - first;
            for (isize start = (count - 2) / 2; start >= 0; --start) {
                SiftDown(first, start, count - 1, comp);
            }
            for (isize end = count - 1; end > 0; --end) {
                FoundationKit::Swap(*(first + end), *first);
                SiftDown(first, 0, end - 1, comp);
            }
        }

        // --- Introsort ---
        template <RandomAccessIterator I, typename Comp>
        constexpr I Partition(I first, I last, Comp comp) {
            auto pivot = FoundationKit::Move(*(first + (last - first) / 2));
            I i = first - 1;
            I j = last;
            while (true) {
                do { ++i; } while (comp(*i, pivot));
                do { --j; } while (comp(pivot, *j));
                if (i >= j) return j;
                FoundationKit::Swap(*i, *j);
            }
        }

        template <RandomAccessIterator I, typename Comp>
        constexpr void IntroSortRecursive(I first, I last, i32 depth_limit, Comp comp) {
            while (last - first > 16) {
                if (depth_limit == 0) {
                    HeapSort(first, last, comp);
                    return;
                }
                --depth_limit;
                I p = Partition(first, last, comp);
                IntroSortRecursive(p + 1, last, depth_limit, comp);
                last = p + 1;
            }
        }
    }

    /// @brief Performs a hybrid sort (Introsort).
    template <RandomAccessIterator I, typename Comp = Less>
    constexpr void Sort(I first, I last, Comp comp = Comp()) {
        if (first == last) return;
        isize n = last - first;
        i32 depth_limit = 0;
        for (isize i = n; i > 1; i >>= 1) depth_limit++;
        Detail::IntroSortRecursive(first, last, depth_limit * 2, comp);
        Detail::InsertionSort(first, last, comp);
    }

    template <ForwardIterator I, typename T, typename Comp = Less>
    constexpr I LowerBound(I first, I last, const T& value, Comp comp = Comp()) {
        isize count = last - first;
        while (count > 0) {
            isize step = count / 2;
            I it = first + step;
            if (comp(*it, value)) {
                first = ++it;
                count -= step + 1;
            } else {
                count = step;
            }
        }
        return first;
    }

    template <ForwardIterator I, typename T, typename Comp = Less>
    constexpr I UpperBound(I first, I last, const T& value, Comp comp = Comp()) {
        isize count = last - first;
        while (count > 0) {
            isize step = count / 2;
            I it = first + step;
            if (!comp(value, *it)) {
                first = ++it;
                count -= step + 1;
            } else {
                count = step;
            }
        }
        return first;
    }

    template <ForwardIterator I, typename T, typename Comp = Less>
    constexpr bool BinarySearch(I first, I last, const T& value, Comp comp = Comp()) {
        first = LowerBound(first, last, value, comp);
        return (first != last && !comp(value, *first));
    }

} // namespace FoundationKit
