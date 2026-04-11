#pragma once

#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>
#include <FoundationKitOsl/Osl.hpp>

namespace FoundationKitOsl {

    using namespace FoundationKitCxxStl;

    /// @brief Constraint for types that may safely reside in a per-CPU block.
    ///
    /// ## What is and is NOT required
    ///
    /// StandardLayout is NOT required. The `offsetof()` well-definedness requirement
    /// applies to the *kernel's outer per-CPU block struct*, which the kernel author
    /// writes — not to T itself. T's internal layout is irrelevant to offsetof.
    ///
    /// What IS required:
    ///
    /// - !Polymorphic: a vptr points into a vtable in .rodata at a fixed virtual
    ///   address. If the per-CPU block is mapped at a different VA on each CPU
    ///   (a valid kernel design), the vptr becomes a dangling pointer on every CPU
    ///   except the one that constructed the object. This is the real layout hazard
    ///   that StandardLayout was an overly conservative proxy for.
    ///
    /// - TriviallyDestructible: there is no destructor call site when a CPU goes
    ///   offline — the block is reclaimed as raw memory.
    ///
    /// - !Reference, !Void: references are aliases to a fixed address, meaningless
    ///   in a per-CPU context. Void is not an object type.
    template<typename T>
    concept PerCpuStorable = !Polymorphic<T> && TriviallyDestructible<T> && !Reference<T> && !Void<T>;

    /// @brief A typed handle to a field at a fixed byte offset within each CPU's
    ///        per-CPU data block.
    ///
    /// ## Design rationale
    ///
    /// FoundationKit does NOT own the per-CPU block layout, the CPU count, or the
    /// arch register that points at the block (gs on x86-64, tpidr_el1 on ARM64,
    /// tp on RISC-V). The host kernel sets all of that up before any FoundationKit
    /// code runs. FoundationKit consumes the two OSL primitives:
    ///   - OslGetPerCpuBase()          — base for the *current* CPU
    ///   - OslGetPerCpuBaseFor(cpu_id) — base for an *arbitrary* CPU
    ///
    /// A PerCpu<T> object is nothing more than a usize holding the byte offset of
    /// the T field within the block. Get() is a single pointer add and cast.
    ///
    /// ## Thread / interrupt safety
    ///
    /// PerCpu<T> does NOT add implicit volatile or atomic semantics. If T is
    /// accessed with interrupts enabled and the field is not itself atomic, the
    /// caller must hold an InterruptGuard or use an atomic T. Making the return
    /// type volatile would be wrong for aggregate T and would hide the real hazard.
    ///
    /// ## Usage
    ///
    /// ```cpp
    /// // In the kernel's per-CPU block definition:
    /// struct PerCpuBlock {
    ///     SpinLock scheduler_lock;
    ///     u64      preempt_count;
    /// };
    ///
    /// // Declare handles (typically as globals or statics):
    /// static constexpr PerCpu<SpinLock> g_sched_lock {
    ///     FOUNDATIONKITCXXSTL_OFFSET_OF(PerCpuBlock, scheduler_lock)
    /// };
    /// static constexpr PerCpu<u64> g_preempt_count {
    ///     FOUNDATIONKITCXXSTL_OFFSET_OF(PerCpuBlock, preempt_count)
    /// };
    ///
    /// // Access on the current CPU:
    /// g_sched_lock.Get().Lock();
    /// g_preempt_count.Get()++;
    ///
    /// // Access on a specific CPU (e.g., for cross-CPU inspection):
    /// g_preempt_count.GetFor(3);
    /// ```
    template<PerCpuStorable T>
    class PerCpu {
    public:
        /// @brief Construct a per-CPU handle for the field at byte offset `field_offset`
        ///        within the kernel's per-CPU block.
        /// @param field_offset Result of offsetof(PerCpuBlock, field).
        explicit constexpr PerCpu(usize field_offset) noexcept : m_offset(field_offset) {}

        /// @brief Access the T instance for the currently executing CPU.
        ///
        /// Interrupts may be enabled; if T is not atomic and the field can be
        /// modified by an interrupt handler, the caller must hold an InterruptGuard.
        [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE T &Get() const noexcept {
            void *const base = OslGetPerCpuBase();
            FK_BUG_ON(base == nullptr,
                      "PerCpu::Get: OslGetPerCpuBase() returned nullptr — "
                      "per-CPU block not initialised for CPU {}",
                      OslGetCurrentCpuId());
            return *reinterpret_cast<T *>(static_cast<byte *>(base) + m_offset);
        }

        /// @brief Access the T instance for a specific CPU by logical ID.
        ///
        /// Intended for cross-CPU inspection (diagnostics, work stealing, migration).
        /// The caller is responsible for ensuring the target CPU's block is not
        /// concurrently modified without appropriate synchronisation.
        ///
        /// @param cpu_id Logical CPU index as understood by the host kernel.
        [[nodiscard]] T &GetFor(u32 cpu_id) const noexcept {
            void *const base = OslGetPerCpuBaseFor(cpu_id);
            FK_BUG_ON(base == nullptr,
                      "PerCpu::GetFor: OslGetPerCpuBaseFor({}) returned nullptr — "
                      "invalid CPU id or block not yet initialised",
                      cpu_id);
            return *reinterpret_cast<T *>(static_cast<byte *>(base) + m_offset);
        }

        /// @brief The byte offset this handle was constructed with.
        [[nodiscard]] constexpr usize Offset() const noexcept { return m_offset; }

        /// @brief Convenience: logical ID of the currently executing CPU.
        [[nodiscard]] static u32 CurrentCpuId() noexcept { return OslGetCurrentCpuId(); }

    private:
        usize m_offset;
    };

} // namespace FoundationKitOsl
