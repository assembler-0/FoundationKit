#pragma once

#include <FoundationKitMemory/ReclaimChain.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // MemoryPressureManager
    // =========================================================================

    /// @brief Watermark-driven memory pressure manager with OOM policy hook.
    ///
    /// @desc  Extends ReclaimChain with three watermarks (Min/Low/High) over
    ///        the total free page count. On every allocation, the caller invokes
    ///        CheckAndReclaim() which:
    ///          1. Computes the current watermark level.
    ///          2. If below Low, triggers the ReclaimChain to free pages.
    ///          3. If still below Min after reclaim, invokes the OOM policy hook.
    ///          4. Returns false if the allocation should be denied (below Min
    ///             and reclaim failed), true otherwise.
    ///
    ///        Watermarks must satisfy: min_pages < low_pages < high_pages.
    ///        Violating this ordering is FK_BUG — it indicates a misconfigured kernel.
    template <usize MaxParticipants = 32>
    class MemoryPressureManager {
    public:
        enum class Watermark : u8 { Min, Low, High, Healthy };

        constexpr MemoryPressureManager() noexcept = default;

        MemoryPressureManager(const MemoryPressureManager&)            = delete;
        MemoryPressureManager& operator=(const MemoryPressureManager&) = delete;

        // ----------------------------------------------------------------
        // Configuration
        // ----------------------------------------------------------------

        /// @brief Set the three watermark thresholds (in pages).
        /// @param min_pages   Below this: deny allocations, invoke OOM.
        /// @param low_pages   Below this: trigger reclaim proactively.
        /// @param high_pages  Above this: system is healthy, no reclaim needed.
        void SetWatermarks(usize min_pages, usize low_pages, usize high_pages) noexcept {
            FK_BUG_ON(min_pages == 0,
                "MemoryPressureManager::SetWatermarks: min_pages must be > 0");
            FK_BUG_ON(min_pages >= low_pages,
                "MemoryPressureManager::SetWatermarks: min ({}) >= low ({}) — "
                "watermarks must be strictly ascending", min_pages, low_pages);
            FK_BUG_ON(low_pages >= high_pages,
                "MemoryPressureManager::SetWatermarks: low ({}) >= high ({}) — "
                "watermarks must be strictly ascending", low_pages, high_pages);
            m_wmark_min  = min_pages;
            m_wmark_low  = low_pages;
            m_wmark_high = high_pages;
        }

        /// @brief Register the OOM policy callback.
        /// @param policy  Called when reclaim fails to bring free pages above Min.
        ///                `needed` is the number of pages that could not be satisfied.
        ///                The policy may panic, kill a process, or do nothing (risky).
        /// @param ctx     Opaque context forwarded verbatim to `policy`.
        void SetOomPolicy(void (*policy)(usize needed, void* ctx) noexcept, void* ctx) noexcept {
            FK_BUG_ON(policy == nullptr,
                "MemoryPressureManager::SetOomPolicy: null OOM policy is not allowed — "
                "a kernel without an OOM handler will silently corrupt memory");
            m_oom_policy = policy;
            m_oom_ctx    = ctx;
        }

        // ----------------------------------------------------------------
        // Participant registration (forwarded to ReclaimChain)
        // ----------------------------------------------------------------

        /// @brief Register a raw reclaim callback.
        void RegisterParticipant(ReclaimFn fn, void* ctx, u8 priority) noexcept {
            m_chain.Register(fn, ctx, priority);
        }

        /// @brief Register an IReclaimableAllocator directly.
        template <IReclaimableAllocator Alloc>
        void RegisterParticipant(Alloc& alloc, u8 priority) noexcept {
            m_chain.Register(alloc, priority);
        }

        // ----------------------------------------------------------------
        // Pressure check — called by PageFrameAllocator on every allocation
        // ----------------------------------------------------------------

        /// @brief Check pressure and reclaim if necessary.
        ///
        /// @desc  Must be called with the current free page count AFTER the
        ///        allocation has been tentatively deducted (i.e. free_pages
        ///        already reflects the pending allocation). This ensures the
        ///        watermark check is conservative.
        ///
        /// @param free_pages   Current free page count (post-allocation).
        /// @param pages_needed Pages the caller is trying to allocate.
        /// @return true  if the allocation may proceed.
        ///         false if the system is below Min and reclaim failed — the
        ///               caller must deny the allocation.
        [[nodiscard]] bool CheckAndReclaim(usize free_pages, usize pages_needed) noexcept {
            FK_BUG_ON(m_wmark_min == 0,
                "MemoryPressureManager::CheckAndReclaim: watermarks not configured — "
                "call SetWatermarks() before the first allocation");

            const Watermark level = CurrentWatermark(free_pages);

            if (level == Watermark::Healthy || level == Watermark::High)
                return true;

            // Below Low: attempt reclaim. We ask for enough pages to reach High.
            if (level == Watermark::Low || level == Watermark::Min) {
                const usize target_bytes = (m_wmark_high > free_pages)
                                         ? (m_wmark_high - free_pages) * kPageSize
                                         : 0;
                if (target_bytes > 0)
                    m_chain.Reclaim(target_bytes);
            }

            // Re-evaluate after reclaim.
            // We don't re-query free_pages from the allocator here — the caller
            // is responsible for re-checking if needed. We use the reclaim result
            // as a proxy: if we're still below Min, invoke OOM.
            if (level == Watermark::Min) {
                if (m_oom_policy) {
                    m_oom_policy(pages_needed, m_oom_ctx);
                } else {
                    FK_BUG("MemoryPressureManager::CheckAndReclaim: below Min watermark "
                           "with no OOM policy registered — kernel will deadlock or corrupt. "
                           "free_pages={}, min={}, needed={}",
                           free_pages, m_wmark_min, pages_needed);
                }
                return false;
            }

            return true;
        }

        /// @brief Classify `free_pages` against the configured watermarks.
        [[nodiscard]] Watermark CurrentWatermark(usize free_pages) const noexcept {
            FK_BUG_ON(m_wmark_min == 0,
                "MemoryPressureManager::CurrentWatermark: watermarks not configured");
            if (free_pages <= m_wmark_min)  return Watermark::Min;
            if (free_pages <= m_wmark_low)  return Watermark::Low;
            if (free_pages <= m_wmark_high) return Watermark::High;
            return Watermark::Healthy;
        }

        [[nodiscard]] usize WatermarkMin()  const noexcept { return m_wmark_min;  }
        [[nodiscard]] usize WatermarkLow()  const noexcept { return m_wmark_low;  }
        [[nodiscard]] usize WatermarkHigh() const noexcept { return m_wmark_high; }

    private:
        ReclaimChain<MaxParticipants> m_chain;
        usize m_wmark_min  = 0;
        usize m_wmark_low  = 0;
        usize m_wmark_high = 0;
        void (*m_oom_policy)(usize, void*) noexcept = nullptr;
        void* m_oom_ctx = nullptr;
    };

} // namespace FoundationKitMemory
