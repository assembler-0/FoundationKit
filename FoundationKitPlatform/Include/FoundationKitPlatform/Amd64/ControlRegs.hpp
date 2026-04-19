#pragma once

#include <FoundationKitPlatform/HostArchitecture.hpp>

#ifdef FOUNDATIONKITPLATFORM_ARCH_X86_64

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitPlatform/Bitfield.hpp>

namespace FoundationKitPlatform::Amd64::ControlRegs {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // CR0
    // =========================================================================

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
        // Intel SDM Vol 3A, 4.1.1: CR0.PG = 1 requires CR0.PE = 1.
        FK_BUG_ON(
            (val & static_cast<u64>(Cr0Flags::Pg)) && !(val & static_cast<u64>(Cr0Flags::Pe)),
            "CR0: Paging (PG) cannot be enabled without Protection (PE)"
        );

        // Intel SDM: CR0.NW=1 and CR0.CD=0 is an invalid configuration (results in #GP).
        FK_BUG_ON(
            (val & static_cast<u64>(Cr0Flags::Nw)) && !(val & static_cast<u64>(Cr0Flags::Cd)),
            "CR0: NW cannot be set if CD is clear — invalid cache configuration"
        );

        __asm__ volatile("mov %0, %%cr0" :: "r"(val) : "memory");
    }

    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void SetCr0Bits(u64 mask) noexcept {
        WriteCr0(ReadCr0() | mask);
    }

    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void ClearCr0Bits(u64 mask) noexcept {
        WriteCr0(ReadCr0() & ~mask);
    }

    // -------------------------------------------------------------------------
    // Typed Bitfield accessors for CR0 fields
    // -------------------------------------------------------------------------
    using Cr0Pe   = Bitfield<u64,  0, 1>;  ///< Protection Enable
    using Cr0Mp   = Bitfield<u64,  1, 1>;  ///< Monitor Coprocessor
    using Cr0Em   = Bitfield<u64,  2, 1>;  ///< Emulation
    using Cr0Ts   = Bitfield<u64,  3, 1>;  ///< Task Switched
    using Cr0Ne   = Bitfield<u64,  5, 1>;  ///< Numeric Error
    using Cr0Wp   = Bitfield<u64, 16, 1>;  ///< Write Protect
    using Cr0Am   = Bitfield<u64, 18, 1>;  ///< Alignment Mask
    using Cr0Pg   = Bitfield<u64, 31, 1>;  ///< Paging Enable

    // =========================================================================
    // CR2
    // =========================================================================

    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 ReadCr2() noexcept {
        u64 val;
        __asm__ volatile("mov %%cr2, %0" : "=r"(val));
        return val;
    }

    // =========================================================================
    // CR3
    // =========================================================================

    /// @brief Bit flags for CR3 (non-PCID mode).
    enum class Cr3Flags : u64 {
        Pwt  = 1ull << 3,   ///< Page-level Write-Through
        Pcd  = 1ull << 4,   ///< Page-level Cache Disable
    };

    // Forward declaration of CR4 read for CR3 checks.
    [[nodiscard]] u64 ReadCr4() noexcept;

    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 ReadCr3() noexcept {
        u64 val;
        __asm__ volatile("mov %%cr3, %0" : "=r"(val));
        return val;
    }

    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void WriteCr3(u64 val) noexcept {
        // The physical base must be 4K-aligned. 
        FK_BUG_ON((val & 0xFFFull) != 0 && (val & (1ull << 63)) == 0,
            "WriteCr3: physical base is not 4K-aligned (val: {:x})", val);

        // Reserved bits check: if PCID is not enabled, bits 11:0 (except 3,4) must be 0.
        // Also bits 62:52 are reserved if PCIDE=0.
        const u64 cr4 = ReadCr4();
        if (!(cr4 & (1ull << 17))) { // Cr4Flags::Pcide = bit 17
            FK_BUG_ON(val & 0xFF00000000000FF6ull, 
                "WriteCr3: Reserved bits set while PCID is disabled");
        }

        __asm__ volatile("mov %0, %%cr3" :: "r"(val) : "memory");
    }

    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 Cr3PhysBase() noexcept {
        return ReadCr3() & 0x000FFFFFFFFFF000ull;
    }

    // =========================================================================
    // MSR & EFER
    // =========================================================================

    static constexpr u32 kMsrEfer   = 0xC0000080u;
    static constexpr u32 kMsrStar   = 0xC0000081u;
    static constexpr u32 kMsrLstar  = 0xC0000082u;
    static constexpr u32 kMsrSfmask = 0xC0000084u;

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

    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 ReadMsr(u32 msr) noexcept {
        u32 lo, hi;
        __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
        return (static_cast<u64>(hi) << 32) | lo;
    }

    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void WriteMsr(u32 msr, u64 val) noexcept {
        const u32 lo = static_cast<u32>(val);
        const u32 hi = static_cast<u32>(val >> 32);
        __asm__ volatile("wrmsr" :: "a"(lo), "d"(hi), "c"(msr) : "memory");
    }

    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 ReadEfer() noexcept {
        return ReadMsr(kMsrEfer);
    }

    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void WriteEfer(u64 val) noexcept {
        const u64 current = ReadEfer();
        FK_BUG_ON((val ^ current) & static_cast<u64>(EferFlags::Lma),
            "WriteEfer: LMA (bit 10) is read-only");
        FK_BUG_ON(!(val & static_cast<u64>(EferFlags::Lme)) && (ReadCr0() & static_cast<u64>(Cr0Flags::Pg)),
            "WriteEfer: cannot clear LME while paging is enabled");
        WriteMsr(kMsrEfer, val);
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
        const u64 current = ReadCr4();

        // LA57 toggle check (requires CR0.PG=0 and EFER.LMA=0).
        if ((val ^ current) & static_cast<u64>(Cr4Flags::La57)) {
            FK_BUG_ON(ReadCr0() & static_cast<u64>(Cr0Flags::Pg),
                "WriteCr4: cannot toggle LA57 while paging is enabled");
            FK_BUG_ON(ReadEfer() & static_cast<u64>(EferFlags::Lma),
                "WriteCr4: cannot toggle LA57 while in 64-bit mode");
        }

        // CR4.PCIDE requires CR4.PAE.
        FK_BUG_ON((val & static_cast<u64>(Cr4Flags::Pcide)) && !(val & static_cast<u64>(Cr4Flags::Pae)),
            "WriteCr4: PCIDE requires PAE");

        // CR4.CET requires CR0.WP.
        if (val & static_cast<u64>(Cr4Flags::Cet)) {
            FK_BUG_ON(!(ReadCr0() & static_cast<u64>(Cr0Flags::Wp)),
                "WriteCr4: CET requires CR0.WP to be set");
        }

        __asm__ volatile("mov %0, %%cr4" :: "r"(val) : "memory");
    }

    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void SetCr4Bits(u64 mask) noexcept {
        WriteCr4(ReadCr4() | mask);
    }

    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void ClearCr4Bits(u64 mask) noexcept {
        WriteCr4(ReadCr4() & ~mask);
    }

    // -------------------------------------------------------------------------
    // Typed Bitfield accessors for CR4 fields
    // -------------------------------------------------------------------------
    using Cr4Pae    = Bitfield<u64,  5, 1>;  ///< Physical Address Extension
    using Cr4Pge    = Bitfield<u64,  7, 1>;  ///< Page Global Enable
    using Cr4Osfxsr = Bitfield<u64,  9, 1>;  ///< OS FXSAVE/FXRSTOR support
    using Cr4Umip   = Bitfield<u64, 11, 1>;  ///< User-Mode Instruction Prevention
    using Cr4Vmxe   = Bitfield<u64, 13, 1>;  ///< VMX Enable
    using Cr4Pcide  = Bitfield<u64, 17, 1>;  ///< PCID Enable
    using Cr4Osxsave= Bitfield<u64, 18, 1>;  ///< XSAVE Enable
    using Cr4Smep   = Bitfield<u64, 20, 1>;  ///< Supervisor Mode Execution Prevention
    using Cr4Smap   = Bitfield<u64, 21, 1>;  ///< Supervisor Mode Access Prevention

    // =========================================================================
    // XCR0
    // =========================================================================

    /// @brief Component bits for XCR0 (XSAVE state components).
    enum class Xcr0Flags : u64 {
        X87     = 1ull << 0,   ///< x87 FPU state
        Sse     = 1ull << 1,   ///< SSE (XMM) state
        Avx     = 1ull << 2,   ///< AVX (YMM) upper halves
        Bndreg  = 1ull << 3,   ///< MPX bound registers
        Bndcsr  = 1ull << 4,   ///< MPX bound config/status
        Opmask  = 1ull << 5,   ///< AVX-512 opmask
        ZmmHi256= 1ull << 6,   ///< AVX-512 ZMM upper 256 bits
        Hi16Zmm = 1ull << 7,   ///< AVX-512 ZMM16–ZMM31
        Pkru    = 1ull << 9,   ///< Protection Key Rights
    };

    [[nodiscard]] FOUNDATIONKITCXXSTL_ALWAYS_INLINE u64 ReadXcr0() noexcept {
        u32 lo, hi;
        __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0u));
        return (static_cast<u64>(hi) << 32) | lo;
    }

    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void WriteXcr0(u64 val) noexcept {
        // Intel SDM hierarchy checks:
        FK_BUG_ON(!(val & static_cast<u64>(Xcr0Flags::X87)),
            "XCR0: X87 bit must always be set");
        
        // AVX requires SSE.
        if (val & static_cast<u64>(Xcr0Flags::Avx)) {
            FK_BUG_ON(!(val & static_cast<u64>(Xcr0Flags::Sse)),
                "XCR0: AVX requires SSE to be enabled");
        }

        // AVX-512 requires AVX.
        constexpr u64 avx512_mask = static_cast<u64>(Xcr0Flags::Opmask) | 
                                    static_cast<u64>(Xcr0Flags::ZmmHi256) | 
                                    static_cast<u64>(Xcr0Flags::Hi16Zmm);
        if (val & avx512_mask) {
            FK_BUG_ON(!(val & static_cast<u64>(Xcr0Flags::Avx)),
                "XCR0: AVX-512 components require AVX to be enabled");
        }

        const u32 lo = static_cast<u32>(val);
        const u32 hi = static_cast<u32>(val >> 32);
        __asm__ volatile("xsetbv" :: "a"(lo), "d"(hi), "c"(0u) : "memory");
    }

    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void SetEferBits(u64 mask) noexcept {
        WriteEfer(ReadEfer() | mask);
    }

    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void ClearEferBits(u64 mask) noexcept {
        WriteEfer(ReadEfer() & ~mask);
    }

    // -------------------------------------------------------------------------
    // Typed Bitfield accessors for EFER fields
    // -------------------------------------------------------------------------
    using EferSce  = Bitfield<u64,  0, 1>;  ///< SYSCALL Enable
    using EferLme  = Bitfield<u64,  8, 1>;  ///< Long Mode Enable
    using EferLma  = Bitfield<u64, 10, 1>;  ///< Long Mode Active (read-only)
    using EferNxe  = Bitfield<u64, 11, 1>;  ///< No-Execute Enable

} // namespace FoundationKitPlatform::Amd64

#endif // FOUNDATIONKITPLATFORM_ARCH_X86_64
