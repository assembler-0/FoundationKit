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
    usize g_current_cpu_id = 0;

    u32 OslGetCurrentCpuId() {
        return static_cast<u32>(g_current_cpu_id);
    }

    // ============================================================================
    // Per-CPU block stubs
    // ============================================================================

    // 4 simulated CPUs, each with a 4 KiB block of zeroed storage.
    static constexpr u32   k_cpu_count  = 4;
    static constexpr usize k_block_size = 4096;
    byte g_per_cpu_blocks[k_cpu_count][k_block_size]{};

    void* OslGetPerCpuBase() {
        return g_per_cpu_blocks[g_current_cpu_id];
    }

    void* OslGetPerCpuBaseFor(u32 cpu_id) {
        if (cpu_id >= k_cpu_count) return nullptr;
        return g_per_cpu_blocks[cpu_id];
    }

    // ============================================================================
    // Timing stubs
    // ============================================================================

    u64 OslGetSystemTicks() {
        return 0;
    }

    u64 OslGetSystemFrequency() {
        return 1000000000ULL; // 1 GHz
    }

    void OslMicroDelay(u64 microseconds) {
        (void)microseconds;
    }

    const char* OslGetHostOsName() {
        return "WhateverOS12345";
    }
}
