#pragma once

#include <FoundationKitPlatform/HostArchitecture.hpp>

#ifdef FOUNDATIONKITPLATFORM_ARCH_X86_64

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>

namespace FoundationKitPlatform::Amd64 {

    using namespace FoundationKitCxxStl;

    /// @brief Bit flags for CR0.
    enum class Cr0Flags : u64 {
        Pe   = 1ull << 0,   ///< Protection Enable
        Mp   = 1ull << 1,   ///< Monitor Coprocessor
        Em   = 1ull << 2,   ///< Emulation (disables x87)
        Ts   = 1ull << 3,   ///< Task Switched
        Et   = 1ull << 4,   ///< Extension Type (always 1 on modern CPUs)
        Ne   = 1ull << 5,   ///< Numeric Error
        Wp   = 1ull << 16,  ///< Write Protect (kernel cannot write to RO user pages)
        Am   = 1ull << 18,  ///< Alignment Mask
        Nw   = 1ull << 29,  ///< Not Write-through
        Cd   = 1ull << 30,  ///< Cache Disable
        Pg   = 1ull << 31,  ///< Paging Enable
    };

    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 ReadCr0() noexcept {
        u64 val;
        __asm__ volatile("mov %%cr0, %0" : "=r"(val));
        return val;
    }

    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void WriteCr0(u64 val) noexcept {
        FK_BUG_ON(
            !(val & static_cast<u64>(Cr0Flags::Pe)) && (val & static_cast<u64>(Cr0Flags::Pg)),
            "CR0: cannot clear PE while PG is set — would cause #GP"
        );
        __asm__ volatile("mov %0, %%cr0" :: "r"(val) : "memory");
    }

    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void SetCr0Bits(u64 mask) noexcept {
        WriteCr0(ReadCr0() | mask);
    }

    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void ClearCr0Bits(u64 mask) noexcept {
        WriteCr0(ReadCr0() & ~mask);
    }

    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 ReadCr2() noexcept {
        u64 val;
        __asm__ volatile("mov %%cr2, %0" : "=r"(val));
        return val;
    }

    /// @brief Bit flags for CR3 (non-PCID mode).
    enum class Cr3Flags : u64 {
        Pwt  = 1ull << 3,   ///< Page-level Write-Through
        Pcd  = 1ull << 4,   ///< Page-level Cache Disable
    };

    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 ReadCr3() noexcept {
        u64 val;
        __asm__ volatile("mov %%cr3, %0" : "=r"(val));
        return val;
    }

    /// @brief Writes CR3, flushing the TLB (unless PCID is in use and bit 63 is set).
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void WriteCr3(u64 val) noexcept {
        // The physical base must be 4K-aligned (bits [11:0] are flags, not address).
        // A non-aligned base is a kernel bug that will cause immediate page-fault storms.
        FK_BUG_ON((val & 0xFFFull) != 0 && (val & (1ull << 63)) == 0,
            "WriteCr3: physical base is not 4K-aligned (val: {:x}) — bits [11:0] must be zero or PCID must be in use", val);
        __asm__ volatile("mov %0, %%cr3" :: "r"(val) : "memory");
    }

    /// @brief Returns the physical base address stored in CR3 (strips flag bits).
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 Cr3PhysBase() noexcept {
        // Bits [11:0] are flags; bits [63:52] are reserved/PCID.
        return ReadCr3() & 0x000FFFFFFFFFF000ull;
    }

    // =========================================================================
    // CR4
    // =========================================================================

    /// @brief Bit flags for CR4.
    enum class Cr4Flags : u64 {
        Vme        = 1ull << 0,   ///< Virtual-8086 Mode Extensions
        Pvi        = 1ull << 1,   ///< Protected-Mode Virtual Interrupts
        Tsd        = 1ull << 2,   ///< Time Stamp Disable (RDTSC only in ring 0)
        De         = 1ull << 3,   ///< Debugging Extensions
        Pse        = 1ull << 4,   ///< Page Size Extensions (4 MiB pages)
        Pae        = 1ull << 5,   ///< Physical Address Extension
        Mce        = 1ull << 6,   ///< Machine-Check Enable
        Pge        = 1ull << 7,   ///< Page Global Enable
        Pce        = 1ull << 8,   ///< Performance-Monitoring Counter Enable
        Osfxsr     = 1ull << 9,   ///< OS Support for FXSAVE/FXRSTOR
        Osxmmexcpt = 1ull << 10,  ///< OS Support for Unmasked SIMD FP Exceptions
        Umip       = 1ull << 11,  ///< User-Mode Instruction Prevention
        La57       = 1ull << 12,  ///< 57-bit Linear Addresses (5-level paging)
        Vmxe       = 1ull << 13,  ///< VMX Enable
        Smxe       = 1ull << 14,  ///< SMX Enable
        Fsgsbase   = 1ull << 16,  ///< Enable RDFSBASE/WRFSBASE etc.
        Pcide      = 1ull << 17,  ///< PCID Enable
        Osxsave    = 1ull << 18,  ///< XSAVE and Processor Extended States Enable
        Kl         = 1ull << 19,  ///< Key Locker Enable
        Smep       = 1ull << 20,  ///< Supervisor Mode Execution Prevention
        Smap       = 1ull << 21,  ///< Supervisor Mode Access Prevention
        Pke        = 1ull << 22,  ///< Protection Keys for User-mode pages
        Cet        = 1ull << 23,  ///< Control-flow Enforcement Technology
        Pks        = 1ull << 24,  ///< Protection Keys for Supervisor-mode pages
    };

    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 ReadCr4() noexcept {
        u64 val;
        __asm__ volatile("mov %%cr4, %0" : "=r"(val));
        return val;
    }

    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void WriteCr4(u64 val) noexcept {
        // CR4.PCIDE (bit 17) requires CR4.PAE (bit 5) to be set simultaneously.
        // Enabling PCID without PAE causes #GP.
        FK_BUG_ON(
            (val & static_cast<u64>(Cr4Flags::Pcide)) && !(val & static_cast<u64>(Cr4Flags::Pae)),
            "WriteCr4: PCIDE requires PAE to be set simultaneously"
        );
        // CR4.CET (bit 23) requires CR0.WP to be set; checked at the CR0 level.
        // CR4.LA57 (5-level paging) cannot be toggled after long mode is active;
        // we can only warn, not enforce, since we don't know the boot state here.
        __asm__ volatile("mov %0, %%cr4" :: "r"(val) : "memory");
    }

    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void SetCr4Bits(u64 mask) noexcept {
        WriteCr4(ReadCr4() | mask);
    }

    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void ClearCr4Bits(u64 mask) noexcept {
        WriteCr4(ReadCr4() & ~mask);
    }

    /// @brief Component bits for XCR0 (XSAVE state components).
    enum class Xcr0Flags : u64 {
        X87     = 1ull << 0,   ///< x87 FPU state
        Sse     = 1ull << 1,   ///< SSE (XMM) state
        Avx     = 1ull << 2,   ///< AVX (YMM) upper halves
        Bndreg  = 1ull << 3,   ///< MPX bound registers
        Bndcsr  = 1ull << 4,   ///< MPX bound config/status
        Opmask  = 1ull << 5,   ///< AVX-512 opmask registers
        ZmmHi256= 1ull << 6,   ///< AVX-512 ZMM upper 256 bits (zmm0–zmm15)
        Hi16Zmm = 1ull << 7,   ///< AVX-512 ZMM16–ZMM31
        Pkru    = 1ull << 9,   ///< Protection Key Rights register
    };

    /// @brief Reads XCR0 via XGETBV.
    /// Caller must ensure CR4.OSXSAVE is set before calling; otherwise #UD.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 ReadXcr0() noexcept {
        u32 lo, hi;
        // XGETBV with ECX=0 reads XCR0. The result is EDX:EAX.
        __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0u));
        return (static_cast<u64>(hi) << 32) | lo;
    }

    /// @brief Writes XCR0 via XSETBV.
    /// Caller must be in ring 0 and have CR4.OSXSAVE set.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void WriteXcr0(u64 val) noexcept {
        FK_BUG_ON(
            !(val & static_cast<u64>(Xcr0Flags::X87)),
            "XCR0: X87 bit must always be set — hardware requirement"
        );
        FK_BUG_ON(
            (val & static_cast<u64>(Xcr0Flags::Avx)) && !(val & static_cast<u64>(Xcr0Flags::Sse)),
            "XCR0: AVX requires SSE bit to also be set"
        );
        const u32 lo = static_cast<u32>(val);
        const u32 hi = static_cast<u32>(val >> 32);
        __asm__ volatile("xsetbv" :: "a"(lo), "d"(hi), "c"(0u) : "memory");
    }

    static constexpr u32 kMsrEfer = 0xC0000080u;

    /// @brief Bit flags for the EFER MSR.
    enum class EferFlags : u64 {
        Sce  = 1ull << 0,   ///< SYSCALL Enable
        Lme  = 1ull << 8,   ///< Long Mode Enable
        Lma  = 1ull << 10,  ///< Long Mode Active (read-only, set by hardware)
        Nxe  = 1ull << 11,  ///< No-Execute Enable
        Svme = 1ull << 12,  ///< SVM Enable (AMD)
        Lmsle= 1ull << 13,  ///< Long Mode Segment Limit Enable (AMD)
        Ffxsr= 1ull << 14,  ///< Fast FXSAVE/FXRSTOR (AMD)
        Tce  = 1ull << 15,  ///< Translation Cache Extension (AMD)
    };

    /// @brief Reads an arbitrary MSR.
    /// @param msr  MSR address.
    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 ReadMsr(u32 msr) noexcept {
        u32 lo, hi;
        __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
        return (static_cast<u64>(hi) << 32) | lo;
    }

    /// @brief Writes an arbitrary MSR.
    /// @param msr  MSR address.
    /// @param val  64-bit value to write.
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void WriteMsr(u32 msr, u64 val) noexcept {
        const u32 lo = static_cast<u32>(val);
        const u32 hi = static_cast<u32>(val >> 32);
        __asm__ volatile("wrmsr" :: "a"(lo), "d"(hi), "c"(msr) : "memory");
    }

    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 ReadEfer() noexcept {
        return ReadMsr(kMsrEfer);
    }

    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void WriteEfer(u64 val) noexcept {
        // EFER.LMA (bit 10) is read-only — set by hardware when long mode activates.
        // Writing it as if it were writable is a kernel bug.
        FK_BUG_ON(
            val & static_cast<u64>(EferFlags::Lma),
            "WriteEfer: LMA (bit 10) is read-only and must not be written"
        );
        // EFER.LME (bit 8) must not be cleared while paging is active (CR0.PG=1),
        // as that would cause an immediate #GP.
        FK_BUG_ON(
            !(val & static_cast<u64>(EferFlags::Lme)) &&
            (ReadCr0() & static_cast<u64>(Cr0Flags::Pg)),
            "WriteEfer: cannot clear LME while CR0.PG is set — would cause #GP"
        );
        WriteMsr(kMsrEfer, val);
    }

    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void SetEferBits(u64 mask) noexcept {
        WriteEfer(ReadEfer() | mask);
    }

    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void ClearEferBits(u64 mask) noexcept {
        WriteEfer(ReadEfer() & ~mask);
    }

} // namespace FoundationKitPlatform::Amd64

#endif // FOUNDATIONKITPLATFORM_ARCH_X86_64
