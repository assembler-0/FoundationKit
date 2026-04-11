#pragma once

#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>

namespace FoundationKitCxxStl::Sync {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // RcuPtr<T, Domain>
    //
    // ## What makes this actually RCU
    //
    // The three primitives that constitute real RCU usage are all here:
    //
    //   1. PUBLISH  — Swap() does a Release store so readers that subsequently
    //                 load with Acquire see the fully-constructed new object.
    //
    //   2. DEFER    — Swap() immediately calls domain.CallAfterGracePeriod()
    //                 with the reclamation callback for the old pointer. The
    //                 caller supplies the reclaim function; RcuPtr does not own
    //                 the memory and does not call delete/free itself.
    //
    //   3. RECLAIM  — The domain fires the callback only after every online CPU
    //                 has passed a quiescent state (ReportQs()), guaranteeing
    //                 no reader can still hold the old pointer.
    //
    // ## Read side
    //
    // Load() returns the current pointer under Acquire ordering. The caller
    // must be inside an RcuReadLock scope (i.e., between two ReportQs() calls).
    // The pointer is valid for the duration of that scope — the domain will not
    // fire the reclamation callback until after the reader's CPU has reported QS.
    //
    // ## Write side
    //
    // Swap() takes:
    //   - new_ptr:  the fully-constructed replacement object.
    //   - reclaim:  void(*)(void*) that frees the old object (e.g., calls
    //               alloc.Deallocate or placement-destructs).
    //
    // Writers must be serialized externally (SpinLock around Swap()).
    //
    // ## Null semantics
    //
    // A null old pointer is valid (initial publish). Swap() skips registering
    // a reclamation callback when the old pointer is null.
    // =========================================================================

    /// @brief RCU-protected pointer. Owns the full publish→defer→reclaim lifecycle.
    ///
    /// @tparam T      Pointed-to type. RcuPtr does not construct or destroy T.
    /// @tparam Domain An instantiation of RcuDomain<MaxCpus, MaxCallbacks>.
    template <typename T, typename Domain>
    class RcuPtr {
    public:
        /// @param domain  The RCU domain that will manage grace periods.
        /// @param initial Initial pointer value (may be nullptr during early boot).
        explicit RcuPtr(Domain& domain, T* initial = nullptr) noexcept
            : m_ptr(initial), m_domain(domain)
        {}

        RcuPtr(const RcuPtr&)            = delete;
        RcuPtr& operator=(const RcuPtr&) = delete;

        /// @brief Load the current pointer from inside a read-side critical section.
        ///
        /// Acquire: pairs with the Release store in Swap(), ensuring the reader
        /// sees the fully-constructed new object before it sees the new pointer.
        ///
        /// @return Current pointer. Valid until the calling CPU's next QS.
        [[nodiscard]] T* Load() const noexcept {
            return m_ptr.Load(MemoryOrder::Acquire);
        }

        /// @brief Publish a new pointer and defer reclamation of the old one.
        ///
        /// The old pointer is passed to domain.CallAfterGracePeriod(reclaim, old)
        /// immediately. The reclaim callback fires only after every online CPU
        /// has reported a quiescent state — at that point no reader can hold
        /// the old pointer.
        ///
        /// @param new_ptr  Fully-constructed replacement. May be nullptr.
        /// @param reclaim  Called with the old pointer after the grace period.
        ///                 Must not be nullptr if the old pointer is non-null.
        void Swap(T* new_ptr, void (*reclaim)(void*)) noexcept {
            // Release: all writes to *new_ptr happen-before any reader's Acquire load.
            T* old = m_ptr.Exchange(new_ptr, MemoryOrder::Release);

            if (old != nullptr) {
                FK_BUG_ON(reclaim == nullptr,
                    "RcuPtr::Swap: non-null old pointer but null reclaim callback — "
                    "old object at {} will leak", old);
                // Register deferred reclamation. The domain fires this callback
                // only after every CPU has passed a quiescent state.
                m_domain.CallAfterGracePeriod(reclaim, old);
            }
        }

        /// @brief Publish an initial value when the old pointer is known to be null.
        ///
        /// Skips the reclamation registration. Use only for the first publish
        /// or when replacing a null pointer.
        void StoreInitial(T* ptr) noexcept {
            FK_BUG_ON(m_ptr.Load(MemoryOrder::Relaxed) != nullptr,
                "RcuPtr::StoreInitial: pointer is not null — use Swap() with a reclaim callback");
            m_ptr.Store(ptr, MemoryOrder::Release);
        }

        /// @brief Raw relaxed load for diagnostics only. Do not dereference
        ///        the result outside a read-side critical section.
        [[nodiscard]] T* UnsafeGet() const noexcept {
            return m_ptr.Load(MemoryOrder::Relaxed);
        }

    private:
        Atomic<T*> m_ptr;
        Domain&    m_domain;
    };

} // namespace FoundationKitCxxStl::Sync
