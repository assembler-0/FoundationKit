#include <FoundationKitOsl/Osl.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
    using namespace FoundationKitCxxStl;

    [[noreturn]] void OslBug(const char* msg) {
        fprintf(stderr, "%s", msg);
        fflush(stderr);
        exit(1);
    }

    bool OslIsSimdEnabled() {
        return true; 
    }

    void OslLog(const char* msg) {
        printf("%s", msg);
    }

    // ============================================================================
    // Interrupt Control (Mock for testing)
    // ============================================================================

    static bool g_interrupts_enabled = true;

    uptr OslInterruptDisable() {
        bool old = g_interrupts_enabled;
        g_interrupts_enabled = false;
        return (uptr)old;
    }

    void OslInterruptRestore(uptr state) {
        g_interrupts_enabled = static_cast<bool>(state);
    }

    bool OslIsInterruptEnabled() {
        return g_interrupts_enabled;
    }

    // ============================================================================
    // Threading & Scheduling (Mock for testing)
    // ============================================================================

    u64 OslGetCurrentThreadId() {
        return 1; // Single-threaded mock
    }

    void OslThreadYield() {
        // No-op in single-threaded mock
    }

    void OslThreadSleep(void* channel) {
        (void)channel;
        // No-op in single-threaded mock
    }

    void OslThreadWake(void* channel) {
        (void)channel;
        // No-op in single-threaded mock
    }

    void OslThreadWakeAll(void* channel) {
        (void)channel;
        // No-op in single-threaded mock
    }
    static usize g_current_cpu_id = 0;

    usize OslGetCurrentCpuId() noexcept {
        return g_current_cpu_id;
    }
}
