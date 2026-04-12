#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/StaticVector.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>
#include <FoundationKitCxxStl/Sync/SpinLock.hpp>
#include <FoundationKitCxxStl/Sync/Locks.hpp>
#include <FoundationKitOsl/Osl.hpp>

namespace FoundationKitCxxStl::Sync {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // QSBR-based Read-Copy-Update (RCU)
    //
    // ## Model
    //
    // In Quiescent-State-Based Reclamation (QSBR), a "quiescent state" (QS) is
    // any point where a CPU holds no references to RCU-protected data. In a
    // kernel, this is typically the scheduler idle loop or a context switch.
    //
    // A "grace period" is the interval during which the writer waits for every
    // online CPU to pass through at least one QS. Once all CPUs have done so,
    // any reader that held a reference before the grace period started has
    // necessarily dropped it — the old data is safe to reclaim.
    //
    // ## Read-side critical sections
    //
    // Unlike classic lock-based RCU, QSBR read-side critical sections are
    // *implicit*: any code that runs between two calls to RcuReportQs() is
    // inside a read-side critical section. RcuReadLock is therefore a
    // documentation/assertion aid, not a real lock.
    //
    // ## Writer side
    //
    // 1. Publish the new version of the data (atomic pointer swap).
    // 2. Call RcuDomain::Synchronize() — blocks until all CPUs have reported
    //    a QS, then drains the callback list.
    //    OR: call RcuDomain::CallAfterGracePeriod() to register a deferred
    //    callback and return immediately.
    //
    // ## Bitmap layout
    //
    // m_pending_mask[i] tracks CPUs [i*64 .. i*64+63]. A set bit means that
    // CPU has NOT yet reported a QS for the current grace period.
    // When the entire array is zero, the grace period is over.
    //
    // ## MaxCpus
    //
    // A compile-time constant supplied by the kernel. Typical values: 1, 4, 64,
    // 256. The bitmap costs ceil(MaxCpus/64) * 8 bytes.
    //
    // ## Callback storage
    //
    // Callbacks are stored in a StaticVector of capacity MaxCallbacks. If the
    // kernel needs more, it must increase MaxCallbacks or drain more frequently.
    // =========================================================================

    /// @brief A plain function pointer + opaque argument pair for RCU callbacks.
    struct RcuCallback {
        void (*func)(void*);
        void* arg;
    };

    /// @brief QSBR RCU domain.
    ///
    /// @tparam MaxCpus      Maximum number of CPUs the domain tracks.
    /// @tparam MaxCallbacks Maximum number of pending callbacks per grace period.
    template <u32 MaxCpus, usize MaxCallbacks = 64>
    class RcuDomain {
        static_assert(MaxCpus > 0, "RcuDomain: MaxCpus must be > 0");
        static_assert(MaxCallbacks > 0, "RcuDomain: MaxCallbacks must be > 0");

    public:
        static constexpr usize kWords = (MaxCpus + 63u) / 64u;

        RcuDomain() noexcept {
            for (usize i = 0; i < kWords; ++i)
                m_pending_mask[i].Store(0, MemoryOrder::Relaxed);
        }

        RcuDomain(const RcuDomain&)            = delete;
        RcuDomain& operator=(const RcuDomain&) = delete;

        /// @brief Register a callback to be invoked after the next grace period.
        ///
        /// Safe to call from any context that can acquire m_lock (i.e., not from
        /// a hard IRQ unless the lock is an IRQ-safe variant). The callback will
        /// be invoked from within Synchronize() or DrainCallbacks().
        ///
        /// @param cb  Function to call.
        /// @param arg Opaque argument forwarded to cb.
        void CallAfterGracePeriod(void (*cb)(void*), void* arg) noexcept {
            FK_BUG_ON(cb == nullptr, "RcuDomain::CallAfterGracePeriod: null callback");
            LockGuard guard(m_lock);
            FK_BUG_ON(m_callbacks.Full(),
                "RcuDomain::CallAfterGracePeriod: callback list full (MaxCallbacks={}), "
                "drain more frequently or increase MaxCallbacks",
                MaxCallbacks);
            m_callbacks.PushBack(RcuCallback{cb, arg});
        }

        /// @brief Report a quiescent state for the calling CPU.
        ///
        /// Must be called from a context where the CPU holds no RCU read-side
        /// references (e.g., scheduler idle, context-switch epilogue).
        /// Calling this from inside an RcuReadLock scope is a logic error.
        ///
        /// When the last CPU clears its bit, all pending callbacks are invoked
        /// inline on the calling CPU. This is intentional: in QSBR the CPU that
        /// completes the grace period is the natural reclamation site.
        void ReportQs() noexcept {
            const u32 cpu = FoundationKitOsl::OslGetCurrentCpuId();
            FK_BUG_ON(cpu >= MaxCpus,
                "RcuDomain::ReportQs: cpu_id {} >= MaxCpus {}", cpu, MaxCpus);

            const usize word = cpu / 64u;
            const u64   bit  = static_cast<u64>(1) << (cpu % 64u);

            // AcqRel: the Release half publishes that this CPU has passed a
            // quiescent state, pairing with the Acquire loads in the grace-period
            // scan below and in Synchronize(). The Acquire half is mandatory: it
            // ensures the subsequent scan of all pending_mask words observes the
            // Release stores from *other* CPUs' FetchAnd calls. Without Acquire
            // here, the zero-check loop can read stale non-zero values from other
            // CPUs and miss grace-period completion entirely (silent livelock).
            const u64 prev = m_pending_mask[word].FetchAnd(~bit, MemoryOrder::AcqRel);

            // If our bit was already clear, we are not part of the current grace
            // period (e.g. CPU came online after Synchronize armed the mask).
            if (!(prev & bit)) return;

            for (usize i = 0; i < kWords; ++i) {
                if (m_pending_mask[i].Load(MemoryOrder::Acquire) != 0) return;
            }

            DrainCallbacks();
        }

        /// @brief Block until a full grace period has elapsed, then drain callbacks.
        ///
        /// Arms a new grace period (marks all online CPUs as pending), then
        /// spin-waits for all CPUs to call ReportQs(). This is the synchronous
        /// writer path. Do NOT call from interrupt context.
        ///
        /// @param online_mask Bitmask of currently online CPUs, one bit per CPU.
        ///                    The kernel must supply this; FoundationKit does not
        ///                    own the CPU topology.
        void Synchronize(u64 const (&online_mask)[kWords]) noexcept {
            {
                LockGuard guard(m_lock);
                for (usize i = 0; i < kWords; ++i)
                    m_pending_mask[i].Store(online_mask[i], MemoryOrder::Release);
            }

            // Spin until every CPU has reported a quiescent state. CpuPause is
            // placed at the top of the retry path so it fires on every failed
            // scan iteration, including those that exit the inner loop early via
            // break. Without this, a partial scan (first non-zero word found
            // immediately) restarts the outer loop with no pause, saturating the
            // memory bus with back-to-back Acquire loads on a hot cache line.
            bool done = false;
            while (!done) {
                CompilerBuiltins::CpuPause();
                done = true;
                for (usize i = 0; i < kWords; ++i) {
                    if (m_pending_mask[i].Load(MemoryOrder::Acquire) != 0) {
                        done = false;
                        break;
                    }
                }
            }

            DrainCallbacks();
        }

        /// @brief Arm the pending mask without spinning — for use in single-threaded
        ///        test harnesses only. Sets the pending bits then returns immediately.
        ///        The caller must subsequently call ReportQs() for each armed CPU to
        ///        trigger DrainCallbacks().
        ///
        /// @param online_mask Same semantics as Synchronize().
        void ArmForTesting(const u64 (&online_mask)[kWords]) noexcept {
            LockGuard guard(m_lock);
            for (usize i = 0; i < kWords; ++i)
                m_pending_mask[i].Store(online_mask[i], MemoryOrder::Release);
        }

        /// @brief Invoke and clear all pending callbacks without waiting.
        ///
        /// Useful when the caller has already ensured a grace period has elapsed
        /// (e.g., after Synchronize() returns, or in a deferred work handler).
        void DrainCallbacks() noexcept {
            // Snapshot the list under the lock, then invoke outside it.
            // This prevents a callback from re-entering CallAfterGracePeriod
            // while we hold the lock, which would deadlock.
            StaticVector<RcuCallback, MaxCallbacks> local;
            {
                LockGuard guard(m_lock);
                local = Move(m_callbacks);
                m_callbacks.Clear();
            }
            for (usize i = 0; i < local.Size(); ++i) {
                FK_BUG_ON(local[i].func == nullptr,
                    "RcuDomain::DrainCallbacks: null callback at index {}", i);
                local[i].func(local[i].arg);
            }
        }

    private:
        // Separate cache lines: m_pending_mask is written by every CPU on every
        // QS report; m_lock + m_callbacks are written only by writers.
        alignas(64) Atomic<u64>                      m_pending_mask[kWords];
        alignas(64) SpinLock                         m_lock;
                    StaticVector<RcuCallback, MaxCallbacks> m_callbacks;
    };

    // =========================================================================
    // RcuReadLock — documentation/assertion RAII guard for read-side sections.
    //
    // In QSBR, the read-side critical section is implicit (any code between two
    // ReportQs() calls). RcuReadLock does NOT issue any memory barrier or atomic
    // operation. Its sole purpose is to make read-side sections visible in code
    // review and to catch the logic error of calling ReportQs() while a lock is
    // held (detectable in debug builds via a per-CPU nesting counter).
    //
    // The nesting counter is stored in a caller-supplied u32 (typically a
    // per-CPU field) to avoid any dependency on a specific per-CPU mechanism.
    // =========================================================================

    /// @brief RAII marker for an RCU read-side critical section (QSBR).
    ///
    /// Increments a caller-supplied nesting counter on construction and
    /// decrements it on destruction. ReportQs() should FK_BUG_ON if the
    /// counter is non-zero.
    ///
    /// @param nesting_counter Reference to a per-CPU u32 tracking read-side depth.
    class RcuReadLock {
    public:
        /// @param nesting_counter Per-CPU read-side nesting depth (must outlive this guard).
        explicit RcuReadLock(u32& nesting_counter) noexcept
            : m_counter(nesting_counter)
        {
            ++m_counter;
        }

        ~RcuReadLock() noexcept {
            FK_BUG_ON(m_counter == 0,
                "RcuReadLock: nesting counter underflow — mismatched RcuReadLock/~RcuReadLock");
            --m_counter;
        }

        RcuReadLock(const RcuReadLock&)            = delete;
        RcuReadLock& operator=(const RcuReadLock&) = delete;

    private:
        u32& m_counter;
    };

    // =========================================================================
    // RcuGuard — RAII writer-side synchronization.
    //
    // Calls RcuDomain::Synchronize() on destruction, ensuring the old version
    // of the data is safe to reclaim after the guard goes out of scope.
    //
    // Usage:
    //   auto* old_ptr = g_ptr.Exchange(new_ptr, MemoryOrder::Release);
    //   {
    //       RcuGuard<MyDomain> guard(g_rcu_domain, g_online_mask);
    //   } // grace period elapsed here
    //   Deallocate(old_ptr);
    // =========================================================================

    /// @brief RAII guard that waits for a full RCU grace period on destruction.
    ///
    /// @tparam Domain An instantiation of RcuDomain<MaxCpus, MaxCallbacks>.
    template <typename Domain>
    class RcuGuard {
    public:
        /// @param domain      The RCU domain to synchronize against.
        /// @param online_mask Bitmask of currently online CPUs (kernel-supplied).
        ///                    Array size must equal Domain::kWords.
        RcuGuard(Domain& domain, const u64 (&online_mask)[Domain::kWords]) noexcept
            : m_domain(domain), m_mask(online_mask)
        {}

        ~RcuGuard() noexcept {
            m_domain.Synchronize(m_mask);
        }

        RcuGuard(const RcuGuard&)            = delete;
        RcuGuard& operator=(const RcuGuard&) = delete;

    private:
        Domain&                              m_domain;
        // Reference-to-array preserves the array type through the member;
        // a raw pointer would lose the size and break Synchronize's overload.
        const u64 (&m_mask)[Domain::kWords];
    };

} // namespace FoundationKitCxxStl::Sync
