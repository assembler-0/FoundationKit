#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>

namespace FoundationKitOsl {

    using namespace FoundationKitCxxStl;

    extern "C" {

    /// @brief Reports a fatal bug and halts execution.
    /// @param msg The message to display.
    [[noreturn]] void OslBug(const char *msg);

    /// @brief Logs a message to the kernel's logging system.
    /// @param msg The message to log.
    void OslLog(const char *msg);

    /// @brief Checks if SIMD is enabled.
    /// @return True if features are enabled, false otherwise.
    bool OslIsSimdEnabled();

    /// @brief Get the current CPU ID.
    u32 OslGetCurrentCpuId();

    /// @brief Get the base address for the current CPU's Per-CPU data block.
    /// @return Pointer to the base of the Per-CPU instance region.
    void *OslGetPerCpuBase();

    /// @brief Get the base address of the Per-CPU data block for a specific CPU.
    /// @param cpu_id The logical CPU index.
    /// @return Pointer to the base of that CPU's instance region, or nullptr if cpu_id is invalid.
    void *OslGetPerCpuBaseFor(u32 cpu_id);

    /// @brief Get the current system monotonic ticks.
    u64 OslGetSystemTicks();

    /// @brief Get the frequency of system ticks (ticks per second).
    u64 OslGetSystemFrequency();

    /// @brief Get the wall-clock base time at boot, in nanoseconds since the Unix epoch.
    /// @desc  The kernel reads this from the RTC or firmware (e.g. EFI GetTime) during
    ///        early boot and returns it here. FoundationKit adds the monotonic uptime
    ///        to this value to produce wall-clock time without ever touching hardware.
    ///        Return 0 if wall-clock time is not available (embedded / bare-metal).
    /// @note  iirc, Limine has a request for this
    u64 OslGetWallClockBase();

    /// @brief High-resolution delay in microseconds.
    /// @param microseconds Number of microseconds to wait.
    void OslMicroDelay(u64 microseconds);

    /// @brief Get the current thread ID.
    u64 OslGetCurrentThreadId();

    /// @brief Yield the current thread.
    void OslThreadYield();

    /// @brief Sleep the current thread (wait on condition variable).
    /// @param channel Opaque channel/condition variable pointer.
    void OslThreadSleep(void *channel);

    /// @brief Wake one thread waiting on the channel.
    /// @param channel Opaque channel/condition variable pointer.
    void OslThreadWake(void *channel);

    /// @brief Wake all threads waiting on the channel.
    /// @param channel Opaque channel/condition variable pointer.
    void OslThreadWakeAll(void *channel);

    /// @brief Disable interrupts and return old state.
    uptr OslInterruptDisable();

    /// @brief Restore interrupt state.
    void OslInterruptRestore(uptr state);

    /// @brief Check if interrupts are enabled.
    bool OslIsInterruptEnabled();

    }

} // namespace FoundationKitOsl
