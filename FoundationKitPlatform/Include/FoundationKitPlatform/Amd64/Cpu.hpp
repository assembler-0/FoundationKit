#pragma once

#include <FoundationKitPlatform/HostArchitecture.hpp>

#ifdef FOUNDATIONKITPLATFORM_ARCH_X86_64

#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitMemory/MemoryOperations.hpp>

namespace FoundationKitPlatform::Amd64 {

    using namespace FoundationKitCxxStl;

    /// @brief Raw result of a CPUID instruction.
    struct CpuidResult {
        u32 eax, ebx, ecx, edx;
    };

    /// @brief Executes CPUID with a leaf and sub-leaf.
    /// @param leaf   EAX input.
    /// @param subleaf ECX input (0 for leaves that ignore it).
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE CpuidResult Cpuid(u32 leaf, u32 subleaf = 0) noexcept {
        CpuidResult r{};
        __asm__ volatile("cpuid" : "=a"(r.eax), "=b"(r.ebx), "=c"(r.ecx), "=d"(r.edx) : "a"(leaf), "c"(subleaf));
        return r;
    }

    /// @brief Returns the maximum supported standard CPUID leaf.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u32 CpuidMaxLeaf() noexcept { return Cpuid(0x0).eax; }

    /// @brief Returns the maximum supported extended CPUID leaf.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u32 CpuidMaxExtLeaf() noexcept { return Cpuid(0x80000000u).eax; }


    /// @brief Enumeration of detectable CPU features.
    /// Each enumerator maps to a specific CPUID bit position.
    enum class CpuFeature : u32 {
        // Leaf 1 ECX
        Sse3 = 0,
        Pclmulqdq = 1,
        Ssse3 = 9,
        Fma = 12,
        Sse41 = 19,
        Sse42 = 20,
        X2Apic = 21,
        Popcnt = 23,
        Aes = 25,
        Xsave = 26,
        Osxsave = 27,
        Avx = 28,
        F16c = 29,
        Rdrand = 30,
        Hypervisor = 31, ///< Hypervisor present bit (leaf 1 ECX[31])

        // Leaf 1 EDX — offset by 32 to distinguish from ECX
        Fpu = 32 + 0,
        Tsc = 32 + 4,
        Msr = 32 + 5,
        Pae = 32 + 6,
        Apic = 32 + 9,
        Sep = 32 + 11,
        Mtrr = 32 + 12,
        Pge = 32 + 13,
        Pat = 32 + 16,
        Pse36 = 32 + 17,
        Clflush = 32 + 19,
        Mmx = 32 + 23,
        Fxsr = 32 + 24,
        Sse = 32 + 25,
        Sse2 = 32 + 26,
        Htt = 32 + 28,

        // Leaf 7 sub-leaf 0 EBX — offset by 64
        Fsgsbase = 64 + 0,
        Bmi1 = 64 + 3,
        Avx2 = 64 + 5,
        Smep = 64 + 7,
        Bmi2 = 64 + 8,
        Invpcid = 64 + 10,
        Avx512f = 64 + 16,
        Smap = 64 + 20,
        Clflushopt = 64 + 23,
        Clwb = 64 + 24,
        Sha = 64 + 29,

        // Leaf 7 sub-leaf 0 ECX — offset by 96
        Umip = 96 + 2,
        Pku = 96 + 3,
        Ospke = 96 + 4,
        Rdpid = 96 + 22,

        // Leaf 7 sub-leaf 0 EDX — offset by 128
        Md_clear = 128 + 10,
        Ibrs = 128 + 26,
        Stibp = 128 + 27,
        Ssbd = 128 + 31,

        // Extended leaf 0x80000001 ECX — offset by 160
        LahfLm = 160 + 0,
        Cmp_legacy = 160 + 1,
        Svm = 160 + 2,
        Cr8Legacy = 160 + 4,
        Abm = 160 + 5,
        Sse4a = 160 + 6,
        Prefetchw = 160 + 8,

        // Extended leaf 0x80000001 EDX — offset by 192
        Syscall = 192 + 11,
        Nx = 192 + 20,
        Page1gb = 192 + 26,
        Rdtscp = 192 + 27,
        Lm = 192 + 29, ///< Long mode (64-bit)

        // Leaf 7 sub-leaf 0 ECX (continued) — same offset band as Umip/Pku/Ospke/Rdpid
        La57 = 96 + 16, ///< 5-level paging support (CR4.LA57)
    };

    /// @brief Queries whether a specific CPU feature is supported.
    /// @param feature The feature to test.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool HasFeature(CpuFeature feature) noexcept {
        const u32 bit_index = static_cast<u32>(feature);

        if (bit_index < 32) {
            // Leaf 1 ECX
            return (Cpuid(0x1).ecx >> bit_index) & 1u;
        }
        if (bit_index < 64) {
            // Leaf 1 EDX
            return (Cpuid(0x1).edx >> (bit_index - 32)) & 1u;
        }
        if (bit_index < 96) {
            // Leaf 7 sub-leaf 0 EBX
            FK_WARN_ON(CpuidMaxLeaf() < 7, "Amd64: CPUID leaf 7 not supported on this CPU");
            return (Cpuid(0x7, 0).ebx >> (bit_index - 64)) & 1u;
        }
        if (bit_index < 128) {
            // Leaf 7 sub-leaf 0 ECX
            FK_WARN_ON(CpuidMaxLeaf() < 7, "Amd64: CPUID leaf 7 not supported on this CPU");
            return (Cpuid(0x7, 0).ecx >> (bit_index - 96)) & 1u;
        }
        if (bit_index < 160) {
            // Leaf 7 sub-leaf 0 EDX
            FK_WARN_ON(CpuidMaxLeaf() < 7, "Amd64: CPUID leaf 7 not supported on this CPU");
            return (Cpuid(0x7, 0).edx >> (bit_index - 128)) & 1u;
        }
        if (bit_index < 192) {
            // Extended leaf 0x80000001 ECX
            FK_WARN_ON(CpuidMaxExtLeaf() < 0x80000001u, "Amd64: Extended CPUID leaf 0x80000001 not supported");
            return (Cpuid(0x80000001u).ecx >> (bit_index - 160)) & 1u;
        }
        // Extended leaf 0x80000001 EDX
        FK_WARN_ON(CpuidMaxExtLeaf() < 0x80000001u, "Amd64: Extended CPUID leaf 0x80000001 not supported");
        return (Cpuid(0x80000001u).edx >> (bit_index - 192)) & 1u;
    }

    // =========================================================================
    // Vendor / Brand
    // =========================================================================

    /// @brief 12-byte vendor string (e.g. "GenuineIntel" / "AuthenticAMD").
    struct CpuVendor {
        char string[13]; ///< Null-terminated.
    };

    /// @brief 48-byte processor brand string (e.g. "Intel(R) Core(TM) i9...").
    /// Sourced from extended leaves 0x80000002–0x80000004, each returning 16 bytes.
    struct CpuBrandString {
        char string[49]; ///< Null-terminated.
    };

    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE CpuVendor GetVendor() noexcept {
        CpuVendor v{};
        const auto r = Cpuid(0x0);
        // EBX:EDX:ECX layout per Intel/AMD spec.
        FoundationKitMemory::MemoryCopy(v.string + 0, &r.ebx, 4);
        FoundationKitMemory::MemoryCopy(v.string + 4, &r.edx, 4);
        FoundationKitMemory::MemoryCopy(v.string + 8, &r.ecx, 4);
        v.string[12] = '\0';
        return v;
    }

    /// @brief Returns true if the CPU reports a hypervisor is present.
    /// This is the "hypervisor present" bit (CPUID.1:ECX[31]).
    /// Note: bare-metal CPUs always return 0 here; a VM may return 1.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool IsHypervisor() noexcept {
        return HasFeature(CpuFeature::Hypervisor);
    }

    /// @brief 4-byte hypervisor vendor leaf signature (leaf 0x40000000).
    struct HypervisorVendor {
        char string[13];
    };

    /// @brief Returns the 48-byte processor brand string.
    /// Returns an empty string if extended leaves 0x80000002–4 are not supported.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE CpuBrandString GetBrandString() noexcept {
        CpuBrandString b{};
        if (CpuidMaxExtLeaf() < 0x80000004u)
            return b;
        // Each of the three leaves returns 16 bytes in EAX:EBX:ECX:EDX.
        for (u32 i = 0; i < 3; ++i) {
            const auto r = Cpuid(0x80000002u + i);
            FoundationKitMemory::MemoryCopy(b.string + i * 16 + 0, &r.eax, 4);
            FoundationKitMemory::MemoryCopy(b.string + i * 16 + 4, &r.ebx, 4);
            FoundationKitMemory::MemoryCopy(b.string + i * 16 + 8, &r.ecx, 4);
            FoundationKitMemory::MemoryCopy(b.string + i * 16 + 12, &r.edx, 4);
        }
        b.string[48] = '\0';
        return b;
    }

    /// @brief Returns the hypervisor vendor string if a hypervisor is present.
    /// Returns an empty string on bare metal.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE HypervisorVendor GetHypervisorVendor() noexcept {
        HypervisorVendor v{};
        if (!IsHypervisor())
            return v;
        const auto r = Cpuid(0x40000000u);
        FoundationKitMemory::MemoryCopy(v.string + 0, &r.ebx, 4);
        FoundationKitMemory::MemoryCopy(v.string + 4, &r.ecx, 4);
        FoundationKitMemory::MemoryCopy(v.string + 8, &r.edx, 4);
        v.string[12] = '\0';
        return v;
    }

    // =========================================================================
    // Hybrid Architecture Detection (Intel P-core / E-core)
    // =========================================================================

    /// @brief Returns true if the CPU reports a hybrid topology (Intel Alder Lake+).
    /// CPUID.7.0:EDX[15] — "Hybrid" bit.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool IsHybrid() noexcept {
        if (CpuidMaxLeaf() < 7)
            return false;
        return (Cpuid(0x7, 0).edx >> 15) & 1u;
    }

    /// @brief Core type as reported by CPUID leaf 0x1A (Intel Hybrid Info).
    enum class HybridCoreType : u8 {
        Unknown = 0x00,
        Atom = 0x20, ///< Efficiency core
        Core = 0x40, ///< Performance core
    };

    /// @brief Returns the hybrid core type of the logical processor executing this call.
    /// Only meaningful when IsHybrid() == true.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE HybridCoreType GetHybridCoreType() noexcept {
        if (CpuidMaxLeaf() < 0x1Au)
            return HybridCoreType::Unknown;
        const u32 native_model_id = Cpuid(0x1Au).eax;
        // Bits [31:24] encode the core type.
        return static_cast<HybridCoreType>((native_model_id >> 24) & 0xFFu);
    }

    // =========================================================================
    // Topology
    // =========================================================================

    /// @brief Logical processor topology information.
    struct CpuTopology {
        u32 logical_processors; ///< Total logical processors (HTT siblings).
        u32 physical_cores; ///< Physical cores on this package.
        u32 apic_id; ///< Initial APIC ID of the executing logical processor.
        bool htt_enabled; ///< Hyper-Threading / SMT active.
    };

    /// @brief Reads basic topology from CPUID leaf 1 and leaf 4.
    /// For full NUMA/package topology use leaf 0xB (ExtendedTopology).
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE CpuTopology GetTopology() noexcept {
        CpuTopology t{};
        const auto leaf1 = Cpuid(0x1);

        t.apic_id = (leaf1.ebx >> 24) & 0xFFu;
        t.htt_enabled = (leaf1.edx >> 28) & 1u;
        t.logical_processors = t.htt_enabled ? ((leaf1.ebx >> 16) & 0xFFu) : 1u;

        // Leaf 4 sub-leaf 0: cores per package.
        if (CpuidMaxLeaf() >= 4) {
            const u32 cores_minus1 = (Cpuid(0x4, 0).eax >> 26) & 0x3Fu;
            t.physical_cores = cores_minus1 + 1u;
        } else {
            t.physical_cores = 1u;
        }

        return t;
    }

    /// @brief A single level of the extended topology (leaf 0xB / 0x1F).
    struct TopologyLevel {
        u32 level_type; ///< 0=invalid, 1=SMT, 2=Core, 3=Module, 4=Tile, 5=Die
        u32 logical_count; ///< Logical processors at this level.
        u32 x2apic_id; ///< x2APIC ID of the executing logical processor.
    };

    /// @brief Maximum topology levels returned by leaf 0xB.
    static constexpr usize kMaxTopologyLevels = 8;

    /// @brief Reads the extended topology via CPUID leaf 0xB.
    /// @param out_levels  Output buffer; must hold at least kMaxTopologyLevels entries.
    /// @param out_count   Number of valid entries written.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void GetExtendedTopology(TopologyLevel *out_levels, usize &out_count) noexcept {
        FK_BUG_ON(out_levels == nullptr, "GetExtendedTopology: out_levels must not be null");
        out_count = 0;

        if (CpuidMaxLeaf() < 0xBu)
            return;

        for (u32 sub = 0; sub < static_cast<u32>(kMaxTopologyLevels); ++sub) {
            const auto r = Cpuid(0xBu, sub);
            const u32 level_type = (r.ecx >> 8) & 0xFFu;
            if (level_type == 0)
                break;
            out_levels[out_count++] = {level_type, r.ebx & 0xFFFFu, r.edx};
        }
        // Paranoid: out_count must not exceed the buffer we were given.
        FK_BUG_ON(out_count > kMaxTopologyLevels,
            "GetExtendedTopology: wrote more entries ({}) than kMaxTopologyLevels ({})",
            out_count, kMaxTopologyLevels);
    }

    // =========================================================================
    // Interrupt Control
    // =========================================================================

    /// @brief Saves the current RFLAGS.IF state, disables interrupts, and returns the saved state.
    /// Pair with RestoreInterrupts() to implement interrupt-safe critical sections
    /// without unconditionally re-enabling interrupts on exit.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 SaveAndDisableInterrupts() noexcept {
        u64 flags;
        // PUSHFQ pushes RFLAGS onto the stack; we pop it into a register.
        // The "memory" clobber prevents the compiler from moving memory
        // accesses out of the critical section.
        __asm__ volatile("pushfq\n\t"
                         "pop %0\n\t"
                         "cli"
                         : "=r"(flags)::"memory");
        return flags;
    }

    /// @brief Restores the interrupt flag from a value previously returned by SaveAndDisableInterrupts().
    /// @param saved_flags  The RFLAGS value returned by SaveAndDisableInterrupts().
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void RestoreInterrupts(u64 saved_flags) noexcept {
        __asm__ volatile("push %0\n\t"
                         "popfq" ::"r"(saved_flags)
                         : "memory", "cc");
    }

    /// @brief Returns true if maskable interrupts are currently enabled (RFLAGS.IF).
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool InterruptsEnabled() noexcept {
        u64 flags;
        __asm__ volatile("pushfq\n\tpop %0" : "=r"(flags));
        return (flags >> 9) & 1u; // IF is bit 9 of RFLAGS
    }

    /// @brief Disables maskable interrupts (CLI).
    /// Must be paired with Sti() or used inside an interrupt-safe critical section.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void Cli() noexcept { __asm__ volatile("cli" ::: "memory"); }

    /// @brief Enables maskable interrupts (STI).
    /// The instruction following STI is still executed with interrupts disabled
    /// (one-instruction shadow), which prevents a race on the IRET path.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void Sti() noexcept { __asm__ volatile("sti" ::: "memory"); }

    /// @brief Halts the processor until the next interrupt (HLT).
    /// Must be called with interrupts enabled, otherwise the CPU halts forever.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void Hlt() noexcept {
        // Halting with interrupts disabled means the CPU will never wake up.
        // This is almost always a kernel bug (missed Sti() before Hlt()).
        FK_WARN_ON(!InterruptsEnabled(),
            "Hlt: called with interrupts disabled — CPU would halt forever");
        __asm__ volatile("hlt" ::: "memory");
    }

    // =========================================================================
    // Serialisation & Memory Fences
    // =========================================================================

    /// @brief Full memory fence — all prior loads and stores complete before any later ones.
    /// Use before/after MMIO sequences or when ordering DMA visibility.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void Mfence() noexcept { __asm__ volatile("mfence" ::: "memory"); }

    /// @brief Load fence — all prior loads complete before any later loads.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void Lfence() noexcept {
        // LFENCE is also a speculative-execution barrier on Intel (post-Spectre);
        // it prevents later instructions from executing speculatively.
        __asm__ volatile("lfence" ::: "memory");
    }

    /// @brief Store fence — all prior stores complete before any later stores.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void Sfence() noexcept { __asm__ volatile("sfence" ::: "memory"); }

    /// @brief Serialises the instruction stream via CPUID (leaf 0).
    ///
    /// CPUID is the only fully serialising instruction available in ring 0
    /// without privilege implications. Use it to fence RDTSC measurements or
    /// to drain the pipeline before a mode switch.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void Serialize() noexcept {
        // Clobber EAX/EBX/ECX/EDX to prevent the compiler from assuming
        // register values survive across this barrier.
        __asm__ volatile("cpuid" ::: "eax", "ebx", "ecx", "edx", "memory");
    }

    /// @brief Emits a PAUSE hint inside a spin-wait loop.
    ///
    /// On SMT processors PAUSE yields the physical core to the sibling thread
    /// and avoids memory-order machine clears on the spin variable.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void Pause() noexcept { __asm__ volatile("pause" ::: "memory"); }

    // =========================================================================
    // Processor ID
    // =========================================================================

    /// @brief Reads the current processor ID via RDPID.
    ///
    /// Returns the value of IA32_TSC_AUX (MSR 0xC0000103), which the OS programs
    /// to encode the logical processor number. Unlike RDTSCP, RDPID has no TSC
    /// side-effect and is significantly cheaper.
    /// Requires CpuFeature::Rdpid (CPUID.7.0:ECX[22]).
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u32 Rdpid() noexcept {
        u64 pid;
        // RDPID encodes the destination as a 64-bit register; the upper 32 bits
        // are always zero-extended on x86-64.
        __asm__ volatile("rdpid %0" : "=r"(pid));
        return static_cast<u32>(pid);
    }

    // =========================================================================
    // Timestamp Counter
    // =========================================================================

    /// @brief Reads the TSC without serialisation (may be reordered by the CPU).
    /// Suitable for coarse profiling where a few cycles of skew are acceptable.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 Rdtsc() noexcept {
        u32 lo, hi;
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        return (static_cast<u64>(hi) << 32) | lo;
    }

    /// @brief Reads the TSC with a preceding LFENCE to prevent out-of-order execution.
    ///
    /// The LFENCE ensures all prior instructions have retired before the counter
    /// is sampled — required for accurate start-of-interval measurements.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 RdtscFenced() noexcept {
        Lfence();
        return Rdtsc();
    }

    /// @brief Result of RDTSCP — TSC value plus the IA32_TSC_AUX identifier.
    struct RdtscpResult {
        u64 tsc; ///< Timestamp counter value.
        u32 tsc_aux; ///< IA32_TSC_AUX (typically encodes socket + core ID).
    };

    /// @brief Reads the TSC and IA32_TSC_AUX atomically via RDTSCP.
    ///
    /// RDTSCP waits for all prior instructions to complete before reading,
    /// making it suitable for end-of-interval measurements without an explicit
    /// LFENCE. Requires CPUID.80000001H:EDX[27] (Rdtscp feature).
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE RdtscpResult Rdtscp() noexcept {
        u32 lo, hi, aux;
        __asm__ volatile("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux));
        return {(static_cast<u64>(hi) << 32) | lo, aux};
    }

    // =========================================================================
    // Hardware RNG
    // =========================================================================

    /// @brief Reads a hardware random value via RDRAND.
    ///
    /// RDRAND draws from the CPU's on-chip CSPRNG. The carry flag (CF) indicates
    /// whether a value was available; the hardware may return CF=0 under entropy
    /// exhaustion. This wrapper retries up to `retries` times before returning
    /// false, which is the correct usage per Intel's recommendation.
    ///
    /// @param out      Receives the random value on success.
    /// @param retries  Maximum attempts before giving up (Intel recommends 10).
    /// @returns        True if a valid random value was produced.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool Rdrand64(u64 &out, u32 retries = 10) noexcept {
        // The "=r" constraint lets the compiler pick any GPR for the output.
        // The "=@ccc" constraint captures CF directly into a bool without a
        // separate SETC instruction — a GCC/Clang C23 asm flag output.
        for (u32 i = 0; i < retries; ++i) {
            u8 ok;
            __asm__ volatile("rdrand %1" : "=@ccc"(ok), "=r"(out)::"cc");
            if (ok)
                return true;
        }
        return false;
    }

    /// @brief 32-bit variant of Rdrand64.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool Rdrand32(u32 &out, u32 retries = 10) noexcept {
        for (u32 i = 0; i < retries; ++i) {
            u8 ok;
            __asm__ volatile("rdrand %1" : "=@ccc"(ok), "=r"(out)::"cc");
            if (ok)
                return true;
        }
        return false;
    }

    /// @brief Reads a hardware random seed via RDSEED.
    ///
    /// RDSEED draws directly from the raw entropy source (before the CSPRNG
    /// conditioning stage), making it suitable for seeding external PRNGs.
    /// It is slower and more likely to return CF=0 than RDRAND.
    ///
    /// @param out      Receives the seed value on success.
    /// @param retries  Maximum attempts before giving up.
    /// @returns        True if a valid seed was produced.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool Rdseed64(u64 &out, u32 retries = 10) noexcept {
        for (u32 i = 0; i < retries; ++i) {
            u8 ok;
            __asm__ volatile("rdseed %1" : "=@ccc"(ok), "=r"(out)::"cc");
            if (ok)
                return true;
        }
        return false;
    }

    // =========================================================================
    // Atomic Compare-Exchange (memory operand forms)
    // =========================================================================
    //
    // These are the *memory-operand* forms of CMPXCHG, distinct from the
    // compiler's __atomic builtins which operate on register-sized values.
    // They are needed when:
    //   - The target is a volatile MMIO location that must not be torn
    //   - You need the exact LOCK prefix semantics on a specific address
    //   - CMPXCHG16B is required for 128-bit lock-free updates (e.g. tagged
    //     pointers, double-word descriptors in GDT/IDT manipulation)

    /// @brief 64-bit LOCK CMPXCHG on a memory operand.
    ///
    /// Atomically: if *ptr == expected, writes desired and returns true.
    /// On failure, expected is updated with the current value at *ptr.
    ///
    /// @param ptr       Target memory location (must be 8-byte aligned).
    /// @param expected  In: value to compare against. Out: actual value if mismatch.
    /// @param desired   Value to write on success.
    /// @returns         True if the exchange succeeded.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool Cmpxchg64(volatile u64 *ptr, u64 &expected,
                                                                   u64 desired) noexcept {
        FK_BUG_ON(reinterpret_cast<uptr>(ptr) % 8 != 0, "Cmpxchg64: pointer must be 8-byte aligned");
        u8 succeeded;
        __asm__ volatile("lock cmpxchgq %3, %1"
                         : "=@ccz"(succeeded), "+m"(*ptr), "+a"(expected)
                         : "r"(desired)
                         : "memory");
        return succeeded;
    }

    /// @brief 32-bit LOCK CMPXCHG on a memory operand.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool Cmpxchg32(volatile u32 *ptr, u32 &expected,
                                                                   u32 desired) noexcept {
        FK_BUG_ON(reinterpret_cast<uptr>(ptr) % 4 != 0, "Cmpxchg32: pointer must be 4-byte aligned");
        u8 succeeded;
        __asm__ volatile("lock cmpxchgl %3, %1"
                         : "=@ccz"(succeeded), "+m"(*ptr), "+a"(expected)
                         : "r"(desired)
                         : "memory");
        return succeeded;
    }

    /// @brief 128-bit compare-exchange via LOCK CMPXCHG16B.
    ///
    /// Atomically compares the 128-bit value at *ptr with {expected_hi:expected_lo}.
    /// If equal, writes {desired_hi:desired_lo}. On failure, the expected pair is
    /// updated with the current contents of *ptr.
    ///
    /// Primary use cases:
    ///   - Lock-free tagged pointer updates (pointer + ABA counter in one word)
    ///   - Atomic update of 128-bit descriptors (e.g. GDT system segment entries)
    ///
    /// @param ptr          Target memory (MUST be 16-byte aligned — #GP otherwise).
    /// @param expected_lo  In/Out: low 64 bits of the expected value (maps to RAX).
    /// @param expected_hi  In/Out: high 64 bits of the expected value (maps to RDX).
    /// @param desired_lo   Low 64 bits to write on success (maps to RBX).
    /// @param desired_hi   High 64 bits to write on success (maps to RCX).
    /// @returns            True if the exchange succeeded (ZF=1).
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool
    Cmpxchg16b(volatile u64 *ptr, u64 &expected_lo, u64 &expected_hi, u64 desired_lo, u64 desired_hi) noexcept {
        // 16-byte alignment is a hard architectural requirement: a misaligned
        // CMPXCHG16B raises #GP(0) unconditionally, even with the LOCK prefix.
        FK_BUG_ON(reinterpret_cast<uptr>(ptr) % 16 != 0,
                  "Cmpxchg16b: pointer must be 16-byte aligned — misalignment causes #GP");
        u8 succeeded;
        // RBX and RCX hold the desired value. We use explicit register constraints
        // because the instruction encoding hardwires these registers.
        // "+A" is not valid for 128-bit in 64-bit mode; we must split into
        // "=a"/"=d" for the expected pair explicitly.
        __asm__ volatile("lock cmpxchg16b %1"
                         : "=@ccz"(succeeded), "+m"(*ptr), "+a"(expected_lo), "+d"(expected_hi)
                         : "b"(desired_lo), "c"(desired_hi)
                         : "memory");
        return succeeded;
    }

    /// @brief 64-bit compare-exchange via LOCK CMPXCHG8B (memory operand form).
    ///
    /// Operates on a 64-bit memory location using the EDX:EAX / ECX:EBX register
    /// pairs. On x86-64 this is largely superseded by the 64-bit register form of
    /// CMPXCHG, but remains useful for:
    ///   - Accessing 64-bit fields in legacy 32-bit data structures
    ///   - Explicit EDX:EAX split semantics required by some firmware interfaces
    ///
    /// @param ptr          Target memory (must be 8-byte aligned).
    /// @param expected_lo  In/Out: low 32 bits (EAX).
    /// @param expected_hi  In/Out: high 32 bits (EDX).
    /// @param desired_lo   Low 32 bits to write on success (EBX).
    /// @param desired_hi   High 32 bits to write on success (ECX).
    /// @returns            True if the exchange succeeded.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE bool
    Cmpxchg8b(volatile u32 *ptr, u32 &expected_lo, u32 &expected_hi, u32 desired_lo, u32 desired_hi) noexcept {
        FK_BUG_ON(reinterpret_cast<uptr>(ptr) % 8 != 0, "Cmpxchg8b: pointer must be 8-byte aligned");
        u8 succeeded;
        __asm__ volatile("lock cmpxchg8b %1"
                         : "=@ccz"(succeeded), "+m"(*ptr), "+a"(expected_lo), "+d"(expected_hi)
                         : "b"(desired_lo), "c"(desired_hi)
                         : "memory");
        return succeeded;
    }

    // =========================================================================
    // Cache Management
    // =========================================================================

    /// @brief Flushes the cache line containing `addr` to memory (CLFLUSH).
    ///
    /// Invalidates the line in all cache levels on all processors. Ordered with
    /// respect to MFENCE but not to other memory operations — surround with
    /// Mfence() when coherency with DMA or persistent memory is required.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void Clflush(const void *addr) noexcept {
        FK_BUG_ON(addr == nullptr, "Clflush: null address");
        __asm__ volatile("clflush (%0)" ::"r"(addr) : "memory");
    }

    /// @brief Optimised cache line flush (CLFLUSHOPT).
    ///
    /// Like CLFLUSH but weakly ordered — multiple CLFLUSHOPT to different lines
    /// may execute out of order relative to each other. Faster than CLFLUSH for
    /// bulk flushing. Requires CpuFeature::Clflushopt. Use Sfence() after a
    /// batch to establish ordering.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void Clflushopt(const void *addr) noexcept {
        FK_BUG_ON(addr == nullptr, "Clflushopt: null address");
        __asm__ volatile("clflushopt (%0)" ::"r"(addr) : "memory");
    }

    /// @brief Writes back a dirty cache line without invalidating it (CLWB).
    ///
    /// The line remains in cache in a non-dirty state after the writeback,
    /// making subsequent reads cheaper than after CLFLUSH/CLFLUSHOPT.
    /// Ideal for persistent memory (pmem) commit paths.
    /// Requires CpuFeature::Clwb. Use Sfence() after a batch.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void Clwb(const void *addr) noexcept {
        FK_BUG_ON(addr == nullptr, "Clwb: null address");
        __asm__ volatile("clwb (%0)" ::"r"(addr) : "memory");
    }

    /// @brief Writes back and invalidates all cache levels on this processor (WBINVD).
    ///
    /// Ring-0 only. Extremely expensive — stalls the pipeline until all dirty
    /// lines are written to memory. Only use when switching cache modes globally
    /// (e.g. toggling CR0.CD or reprogramming MTRRs).
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void Wbinvd() noexcept { __asm__ volatile("wbinvd" ::: "memory"); }

} // namespace FoundationKitPlatform::Amd64

#endif // FOUNDATIONKITPLATFORM_ARCH_X86_64
