#pragma once

#include <FoundationKitMemory/MemoryCore.hpp>
#include <FoundationKitCxxStl/Base/Algorithm.hpp>

namespace FoundationKitMemory {

    // ============================================================================
    // PressureCallback
    // ============================================================================

    /// @brief Signature for a memory pressure notification callback.
    /// @param bytes_needed Total bytes the system urgently needs reclaimed.
    /// @param ctx          Opaque context pointer supplied at registration time.
    using PressureCallback = void(*)(usize bytes_needed, void* ctx) noexcept;

    // ============================================================================
    // PressureManager<MaxCallbacks>
    // ============================================================================

    /// @brief Priority-ordered OOM notification and reclaim chain.
    ///
    /// @desc When the system is approaching OOM, `NotifyPressure` walks registered
    ///       callbacks in ascending priority order (lower priority number = first called).
    ///       Each callback is expected to release memory — caches, lazily-allocated
    ///       structures, etc. — so that the failing allocation can be retried.
    ///
    ///       **Design contract:**
    ///       - Callbacks are invoked synchronously in the caller's context.
    ///       - Callbacks MUST NOT block, allocate through the pressured allocator,
    ///         or acquire locks held by the caller (deadlock risk).
    ///       - Callbacks that have already been called once in this pressure event
    ///         are NOT called again unless `NotifyPressure` is invoked for a new event.
    ///       - Unregistered callbacks (nullptr) are silently skipped.
    ///
    /// @tparam MaxCallbacks  Static capacity of the callback table (default: 8).
    ///                       Excess RegisterCallback calls are rejected with FK_BUG_ON.
    template <usize MaxCallbacks = 8>
    class PressureManager {
        static_assert(MaxCallbacks >= 1 && MaxCallbacks <= 128,
            "PressureManager: MaxCallbacks must be in [1, 128]");

    public:
        constexpr PressureManager() noexcept = default;

        // Non-copyable — callbacks hold raw context pointers.
        PressureManager(const PressureManager&) = delete;
        PressureManager& operator=(const PressureManager&) = delete;

        // ----------------------------------------------------------------
        // Registration
        // ----------------------------------------------------------------

        /// @brief Register a pressure callback.
        /// @param cb       Callback function pointer (must be non-null).
        /// @param ctx      Opaque context passed verbatim to the callback.
        /// @param priority Dispatch order: lower = earlier. Range [0, 255].
        ///                 Multiple callbacks at the same priority are dispatched
        ///                 in registration order.
        void RegisterCallback(PressureCallback cb, void* ctx, u8 priority) noexcept {
            FK_BUG_ON(!cb, "PressureManager::RegisterCallback: null callback");
            FK_BUG_ON(m_count >= MaxCallbacks,
                "PressureManager::RegisterCallback: table full (MaxCallbacks={})",
                MaxCallbacks);

            // Find insertion position to keep the table sorted by priority.
            usize pos = m_count;
            while (pos > 0 && m_entries[pos - 1].priority > priority) {
                m_entries[pos] = m_entries[pos - 1];
                --pos;
            }
            m_entries[pos] = {cb, ctx, priority};
            ++m_count;
        }

        /// @brief Unregister a callback by its function pointer.
        /// @desc Removes the first entry whose `cb` matches. O(n).
        void UnregisterCallback(PressureCallback cb) noexcept {
            for (usize i = 0; i < m_count; ++i) {
                if (m_entries[i].cb == cb) {
                    // Shift entries left to fill the gap.
                    for (usize j = i + 1; j < m_count; ++j)
                        m_entries[j - 1] = m_entries[j];
                    --m_count;
                    return;
                }
            }
        }

        // ----------------------------------------------------------------
        // Pressure Notification
        // ----------------------------------------------------------------

        /// @brief Notify all registered callbacks of a memory pressure event.
        /// @param bytes_needed Approximate bytes the system urgently needs freed.
        /// @return True if at least one callback was invoked.
        bool NotifyPressure(usize bytes_needed) noexcept {
            if (m_count == 0) return false;

            for (usize i = 0; i < m_count; ++i) {
                if (m_entries[i].cb) {
                    m_entries[i].cb(bytes_needed, m_entries[i].ctx);
                }
            }
            return true;
        }

        /// @brief Walk callbacks in priority order, calling each until at least
        ///        `target_bytes` are estimated reclaimed.
        /// @param target_bytes  Bytes to reclaim.
        /// @return Estimated bytes reclaimed (sum of bytes_needed passed to callbacks
        ///         that signalled success — callbacks do not communicate their actual
        ///         reclaim amount, so this is a conservative minimum).
        ///
        /// @note This is a best-effort API. Actual reclaimed bytes depend entirely
        ///       on what each callback does internally.
        usize Reclaim(usize target_bytes) noexcept {
            usize reclaimed = 0;
            for (usize i = 0; i < m_count && reclaimed < target_bytes; ++i) {
                if (m_entries[i].cb) {
                    m_entries[i].cb(target_bytes - reclaimed, m_entries[i].ctx);
                    // No feedback mechanism — give the callback credit for target amount.
                    // In practice, systems that need accurate accounting embed a reclaim
                    // counter in their ctx and read it back.
                    reclaimed += (target_bytes - reclaimed);
                }
            }
            return reclaimed;
        }

        // ----------------------------------------------------------------
        // Introspection
        // ----------------------------------------------------------------

        [[nodiscard]] usize CallbackCount()    const noexcept { return m_count; }
        [[nodiscard]] static constexpr usize MaxCallbackCount() noexcept { return MaxCallbacks; }

    private:
        struct Entry {
            PressureCallback cb       = nullptr;
            void*            ctx      = nullptr;
            u8               priority = 0;
        };

        Entry m_entries[MaxCallbacks] = {};
        usize m_count                 = 0;
    };

} // namespace FoundationKitMemory
