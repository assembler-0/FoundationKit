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
