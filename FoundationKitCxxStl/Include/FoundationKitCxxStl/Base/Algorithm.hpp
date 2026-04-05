#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitCxxStl/Base/Pair.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>

namespace FoundationKitCxxStl {

    /// @brief Default comparator.
    struct Less {
        template <typename T, typename U>
        constexpr bool operator()(const T& lhs, const U& rhs) const {
            return lhs < rhs;
        }
    };

    struct Plus {
        template <typename T, typename U>
        constexpr auto operator()(const T& lhs, const U& rhs) const {
            return lhs + rhs;
        }
    };

    template <InputIterator I, typename T>
    constexpr I Find(I first, I last, const T& value) {
        for (; first != last; ++first) {
            if (*first == value) return first;
        }
        return last;
    }

    template <InputIterator I, typename Pred>
    constexpr I FindIf(I first, I last, Pred pred) {
        for (; first != last; ++first) {
            if (pred(*first)) return first;
        }
        return last;
    }

    template <InputIterator I, typename Pred>
    constexpr bool AllOf(I first, I last, Pred pred) {
        for (; first != last; ++first) {
            if (!pred(*first)) return false;
        }
        return true;
    }

    template <InputIterator I, typename Pred>
    constexpr bool AnyOf(I first, I last, Pred pred) {
        return FindIf(first, last, pred) != last;
    }

    template <InputIterator I, typename Pred>
    constexpr bool NoneOf(I first, I last, Pred pred) {
        return FindIf(first, last, pred) == last;
    }

    template <InputIterator I, typename Func>
    constexpr Func ForEach(I first, I last, Func f) {
        for (; first != last; ++first) f(*first);
        return f;
    }

    template <InputIterator I, typename O, typename Func>
    requires OutputIterator<O, IterValue<I>>
    constexpr O Transform(I first, I last, O result, Func f) {
        for (; first != last; ++first, (void)++result) {
            *result = f(*first);
        }
        return result;
    }

    template <InputIterator I, typename T, typename BinaryOp = Plus>
    constexpr T Accumulate(I first, I last, T init, BinaryOp op = BinaryOp()) {
        for (; first != last; ++first) {
            init = op(init, *first);
        }
        return init;
    }

    template <InputIterator I, typename O>
    requires OutputIterator<O, IterValue<I>>
    constexpr O Copy(I first, I last, O result) {
        for (; first != last; ++first, (void)++result) {
            *result = *first;
        }
        return result;
    }

    template <ForwardIterator I, typename T>
    constexpr void Fill(I first, I last, const T& value) {
        for (; first != last; ++first) {
            *first = value;
        }
    }

    template <InputIterator I, typename T>
    constexpr isize Count(I first, I last, const T& value) {
        isize n = 0;
        for (; first != last; ++first) {
            if (*first == value) ++n;
        }
        return n;
    }

    template <InputIterator I, typename Pred>
    constexpr isize CountIf(I first, I last, Pred pred) {
        isize n = 0;
        for (; first != last; ++first) {
            if (pred(*first)) ++n;
        }
        return n;
    }

    template <typename T>
    constexpr const T& Min(const T& a, const T& b) {
    return a < b ? a : b;
    }

    template <typename T>
    constexpr T Min(InitializerList<T> i_list) {
        auto it = i_list.begin();
        T result = *it;
        for (++it; it != i_list.end(); ++it) {
            if (*it < result) result = *it;
        }
        return result;
    }

    template <typename T>
    constexpr const T& Max(const T& a, const T& b) {
        return a < b ? b : a;
    }

    template <typename T>
    constexpr T Max(InitializerList<T> i_list) {
        auto it = i_list.begin();
        T result = *it;
        for (++it; it != i_list.end(); ++it) {
            if (result < *it) result = *it;
        }
        return result;
    }

    template <typename T>
    constexpr const T& Clamp(const T& v, const T& lo, const T& hi) {
        return v < lo ? lo : hi < v ? hi : v;
    }

    /// @brief Reverses the order of elements in the range [first, last).
    template <BidirectionalIterator I>
    constexpr void Reverse(I first, I last) {
        if (first == last) return;
        --last;
        while (first != last) {
            FoundationKitCxxStl::Swap(*first, *last);
            if (++first == last) break;
            --last;
        }
    }

    /// @brief Rotates the elements in the range [first, last) such that n_first becomes the new first element.
    template <ForwardIterator I>
    constexpr I Rotate(I first, I n_first, I last) {
        if (first == n_first) return last;
        if (n_first == last) return first;

        I read = n_first;
        I write = first;
        I next_read = first; // fallback for forward iterator

        while (read != last) {
            if (write == next_read) next_read = read;
            FoundationKitCxxStl::Swap(*write, *read);
            ++write;
            ++read;
        }

        I ret = write;
        read = next_read;
        while (read != last) {
            if (write == next_read) next_read = read;
            FoundationKitCxxStl::Swap(*write, *read);
            ++write;
            ++read;
        }
        return ret;
    }

    /// @brief Removes consecutive duplicate elements from the range [first, last).
    template <ForwardIterator I>
    constexpr I Unique(I first, I last) {
        if (first == last) return last;
        I result = first;
        while (++first != last) {
            if (!(*result == *first)) {
                *++result = FoundationKitCxxStl::Move(*first);
            }
        }
        return ++result;
    }

    /// @brief Removes all elements satisfying specific criteria from the range [first, last).
    template <ForwardIterator I, typename Pred>
    constexpr I RemoveIf(I first, I last, Pred pred) {
        I it = first;
        while (it != last && !pred(*it)) ++it;
        if (it == last) return it;

        I result = it;
        while (++it != last) {
            if (!pred(*it)) {
                *result = FoundationKitCxxStl::Move(*it);
                ++result;
            }
        }
        return result;
    }

    template <InputIterator I1, InputIterator I2>
    constexpr Pair<I1, I2> Mismatch(I1 first1, I1 last1, I2 first2) {
        while (first1 != last1 && *first1 == *first2) {
            ++first1;
            ++first2;
        }
        return {first1, first2};
    }

    template <InputIterator I1, InputIterator I2, typename Pred>
    constexpr Pair<I1, I2> Mismatch(I1 first1, I1 last1, I2 first2, Pred pred) {
        while (first1 != last1 && pred(*first1, *first2)) {
            ++first1;
            ++first2;
        }
        return {first1, first2};
    }

    template <ForwardIterator I, typename Pred>
    constexpr I Partition(I first, I last, Pred pred) {
        first = FindIfNot(first, last, pred);
        if (first == last) return first;

        for (I i = Next(first); i != last; ++i) {
            if (pred(*i)) {
                FoundationKitCxxStl::Swap(*first, *i);
                ++first;
            }
        }
        return first;
    }

    template <InputIterator I, typename Pred>
    constexpr I FindIfNot(I first, I last, Pred pred) {
        for (; first != last; ++first) {
            if (!pred(*first)) return first;
        }
        return last;
    }

    template <InputIterator I>
    constexpr I Next(I it, isize n = 1) {
        while (n > 0) { ++it; --n; }
        while (n < 0) { --it; ++n; }
        return it;
    }

    template <InputIterator I>
    constexpr I Prev(I it, isize n = 1) {
        return Next(it, -n);
    }

    namespace Detail {
        // --- Insertion Sort ---
        template <RandomAccessIterator I, typename Comp>
        constexpr void InsertionSort(I first, I last, Comp comp) {
            if (first == last) return;
            for (I i = first + 1; i != last; ++i) {
                auto val = FoundationKitCxxStl::Move(*i);
                I j = i;
                while (j > first && comp(val, *(j - 1))) {
                    *j = FoundationKitCxxStl::Move(*(j - 1));
                    --j;
                }
                *j = FoundationKitCxxStl::Move(val);
            }
        }

        // --- Heapsort ---
        template <RandomAccessIterator I, typename Comp>
        constexpr void SiftDown(I first, const isize start, const isize end, Comp comp) {
            isize root = start;
            while (root * 2 + 1 <= end) {
                isize child = root * 2 + 1;
                isize swap = root;
                if (comp(*(first + swap), *(first + child))) swap = child;
                if (child + 1 <= end && comp(*(first + swap), *(first + child + 1))) swap = child + 1;
                if (swap == root) return;
                FoundationKitCxxStl::Swap(*(first + root), *(first + swap));
                root = swap;
            }
        }

        template <RandomAccessIterator I, typename Comp>
        constexpr void HeapSort(I first, I last, Comp comp) {
            const isize count = last - first;
            for (isize start = (count - 2) / 2; start >= 0; --start) {
                SiftDown(first, start, count - 1, comp);
            }
            for (isize end = count - 1; end > 0; --end) {
                FoundationKitCxxStl::Swap(*(first + end), *first);
                SiftDown(first, 0, end - 1, comp);
            }
        }

        // --- Introsort ---
        template <RandomAccessIterator I, typename Comp>
        constexpr I Partition(I first, I last, Comp comp) {
            auto pivot = FoundationKitCxxStl::Move(*(first + (last - first) / 2));
            I i = first - 1;
            I j = last;
            while (true) {
                do { ++i; } while (comp(*i, pivot));
                do { --j; } while (comp(pivot, *j));
                if (i >= j) return j;
                FoundationKitCxxStl::Swap(*i, *j);
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
        const isize n = last - first;
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
        return first != last && !comp(value, *first);
    }

} // namespace FoundationKitCxxStl
