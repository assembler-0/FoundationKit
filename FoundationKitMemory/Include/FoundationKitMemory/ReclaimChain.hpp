#pragma once

#include <FoundationKitMemory/MemoryCore.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // ReclaimFn — raw reclaim callback signature
    // =========================================================================

    /// @brief Reclaim callback: attempt to free up to `target` bytes.
    /// @param target  Bytes the chain still needs freed at the time of this call.
    ///                Decreases with each successful callback; use it to decide
    ///                how aggressively to reclaim.
    /// @param ctx     Opaque context pointer supplied at registration time.
    /// @return        Actual bytes freed. Must not lie — the chain stops early
    ///                once the running total meets the original target.
    using ReclaimFn = usize(*)(usize target, void* ctx) noexcept;

    // =========================================================================
    // ReclaimChain<MaxParticipants>
    // =========================================================================

    /// @brief Priority-ordered reclaim chain with accurate byte accounting.
    ///
    /// @desc  Replaces PressureManager. Key differences:
    ///          - Callbacks return actual bytes freed, not void.
    ///          - Reclaim() stops calling further participants once the target
    ///            is met — no wasted work.
    ///          - IReclaimableAllocator instances register directly without a
    ///            hand-written wrapper: a trampoline is generated at registration
    ///            time and stored as a plain function pointer.
    ///          - NotifyAll() broadcasts unconditionally (pressure hint, not OOM).
    ///
    ///        Registration is sorted by priority (lower number = called first).
    ///        Participants at equal priority are called in registration order.
    ///
    /// @tparam MaxParticipants  Static capacity of the participant table.
    template <usize MaxParticipants = 16>
    class ReclaimChain {
        static_assert(MaxParticipants >= 1 && MaxParticipants <= 256,
            "ReclaimChain: MaxParticipants must be in [1, 256]");

    public:
        constexpr ReclaimChain() noexcept = default;

        ReclaimChain(const ReclaimChain&)            = delete;
        ReclaimChain& operator=(const ReclaimChain&) = delete;

        // ----------------------------------------------------------------
        // Registration — raw callback
        // ----------------------------------------------------------------

        /// @brief Register a raw reclaim callback.
        /// @param fn       Non-null function pointer satisfying ReclaimFn.
        /// @param ctx      Opaque context forwarded verbatim to fn.
        /// @param priority Dispatch order: lower = earlier. Range [0, 255].
        void Register(ReclaimFn fn, void* ctx, u8 priority) noexcept {
            FK_BUG_ON(fn == nullptr,
                "ReclaimChain::Register: null callback");
            FK_BUG_ON(m_count >= MaxParticipants,
                "ReclaimChain::Register: table full (MaxParticipants={})",
                MaxParticipants);

            InsertSorted({fn, ctx, priority});
        }

        // ----------------------------------------------------------------
        // Registration — IReclaimableAllocator
        // ----------------------------------------------------------------

        /// @brief Register an IReclaimableAllocator directly.
        /// @desc  Generates a trampoline at registration time: a static function
        ///        that casts ctx back to Alloc* and calls Reclaim(target).
        ///        The allocator must outlive the ReclaimChain.
        /// @param alloc    Reference to the allocator. Must outlive this chain.
        /// @param priority Dispatch order: lower = earlier.
        template <IReclaimableAllocator Alloc>
        void Register(Alloc& alloc, u8 priority) noexcept {
            FK_BUG_ON(m_count >= MaxParticipants,
                "ReclaimChain::Register: table full (MaxParticipants={})",
                MaxParticipants);

            // The trampoline is a stateless function — no closure, no heap.
            // Alloc is captured only as a void* context pointer.
            ReclaimFn trampoline = [](usize target, void* ctx) noexcept -> usize {
                return static_cast<Alloc*>(ctx)->Reclaim(target);
            };

            InsertSorted({trampoline, static_cast<void*>(&alloc), priority});
        }

        // ----------------------------------------------------------------
        // Reclaim — stops early once target is met
        // ----------------------------------------------------------------

        /// @brief Walk participants in priority order until `target_bytes` are freed.
        /// @desc  Each participant receives the remaining target (original minus
        ///        what has already been freed), so it can calibrate its effort.
        ///        Stops as soon as the running total >= target_bytes.
        /// @return Actual bytes freed (sum of all invoked callback return values).
        [[nodiscard]] usize Reclaim(usize target_bytes) noexcept {
            usize reclaimed = 0;
            for (usize i = 0; i < m_count && reclaimed < target_bytes; ++i) {
                const usize remaining = target_bytes - reclaimed;
                reclaimed += m_entries[i].fn(remaining, m_entries[i].ctx);
            }
            return reclaimed;
        }

        // ----------------------------------------------------------------
        // NotifyAll — unconditional broadcast
        // ----------------------------------------------------------------

        /// @brief Invoke every participant with `bytes_needed`, ignoring return values.
        /// @desc  Use for soft pressure hints where you want all subsystems to
        ///        trim caches proactively, not just until a target is met.
        void NotifyAll(usize bytes_needed) noexcept {
            for (usize i = 0; i < m_count; ++i)
                m_entries[i].fn(bytes_needed, m_entries[i].ctx);
        }

        // ----------------------------------------------------------------
        // Unregister
        // ----------------------------------------------------------------

        /// @brief Remove the first entry whose ctx matches `participant`.
        /// @desc  Use to unregister an allocator that is being destroyed.
        ///        O(n) scan. Shifts remaining entries left.
        void Unregister(void* participant) noexcept {
            for (usize i = 0; i < m_count; ++i) {
                if (m_entries[i].ctx == participant) {
                    for (usize j = i + 1; j < m_count; ++j)
                        m_entries[j - 1] = m_entries[j];
                    --m_count;
                    return;
                }
            }
        }

        /// @brief Remove the first entry whose fn matches `callback`.
        void Unregister(ReclaimFn callback) noexcept {
            for (usize i = 0; i < m_count; ++i) {
                if (m_entries[i].fn == callback) {
                    for (usize j = i + 1; j < m_count; ++j)
                        m_entries[j - 1] = m_entries[j];
                    --m_count;
                    return;
                }
            }
        }

        // ----------------------------------------------------------------
        // Introspection
        // ----------------------------------------------------------------

        [[nodiscard]] usize ParticipantCount() const noexcept { return m_count; }
        [[nodiscard]] static constexpr usize Capacity() noexcept { return MaxParticipants; }

    private:
        struct Entry {
            ReclaimFn fn       = nullptr;
            void*     ctx      = nullptr;
            u8        priority = 0;
        };

        /// @brief Insert `e` into m_entries[], maintaining ascending priority order.
        void InsertSorted(Entry e) noexcept {
            usize pos = m_count;
            while (pos > 0 && m_entries[pos - 1].priority > e.priority) {
                m_entries[pos] = m_entries[pos - 1];
                --pos;
            }
            m_entries[pos] = e;
            ++m_count;
        }

        Entry m_entries[MaxParticipants] = {};
        usize m_count                    = 0;
    };

} // namespace FoundationKitMemory
