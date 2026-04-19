#pragma once

#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/KernelError.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitMemory/Management/AddressTypes.hpp>

namespace FoundationKitDevice {

    using namespace FoundationKitCxxStl;

    // Forward declaration — avoid circular include with DeviceNode.
    struct DeviceNode;

    // =========================================================================
    // DMA direction
    // =========================================================================

    /// @brief DMA transfer direction.
    ///
    /// @desc  The direction matters for cache coherency operations:
    ///        - ToDevice:      CPU writes, device reads.  Flush before DMA.
    ///        - FromDevice:    Device writes, CPU reads.  Invalidate after DMA.
    ///        - Bidirectional: Both.  Flush before, invalidate after.
    enum class DmaDirection : u32 {
        ToDevice      = 0,
        FromDevice    = 1,
        Bidirectional = 2,
    };

    /// @brief DMA bus address — may differ from PhysicalAddress if an IOMMU remaps.
    using DmaAddress = u64;

    // =========================================================================
    // DmaOps — function-pointer table for DMA mapping
    //
    // Follows the IrqChipDescriptor / ClockSourceDescriptor pattern:
    // a POD struct of nullable function pointers. The bus or platform IOMMU
    // driver populates this at boot. DeviceNode stores a pointer to the
    // applicable DmaOps for its bus.
    // =========================================================================

    /// @brief DMA mapping operations for a bus or IOMMU.
    ///
    /// @desc  Every function pointer is optional. If the hardware does not
    ///        support a capability (e.g., no IOMMU → no remap), the pointer
    ///        is nullptr and the DeviceManager must check before invoking.
    ///
    ///        In the simplest case (no IOMMU, identity-mapped physical
    ///        memory), MapSingle returns the physical address unchanged.
    struct DmaOps {
        /// @brief Map a physical range for streaming DMA.
        ///
        /// @param dev        The device performing the DMA.
        /// @param pa         Physical address to map.
        /// @param size       Number of bytes.
        /// @param direction  Transfer direction (for cache ops).
        /// @return DMA bus address visible to the device.
        KernelResult<DmaAddress> (*MapSingle)(
            DeviceNode& dev, FoundationKitMemory::PhysicalAddress pa,
            usize size, DmaDirection direction) noexcept = nullptr;

        /// @brief Unmap a previously mapped streaming DMA range.
        void (*UnmapSingle)(
            DeviceNode& dev, DmaAddress da,
            usize size, DmaDirection direction) noexcept = nullptr;

        /// @brief Allocate a coherent (uncacheable) DMA buffer.
        ///
        /// @param dev           The device.
        /// @param size          Requested buffer size in bytes.
        /// @param out_dma_addr  [out] DMA address visible to device.
        /// @return CPU-visible virtual address of the buffer.
        KernelResult<void*> (*AllocCoherent)(
            DeviceNode& dev, usize size,
            DmaAddress* out_dma_addr) noexcept = nullptr;

        /// @brief Free a coherent DMA buffer.
        void (*FreeCoherent)(
            DeviceNode& dev, void* cpu_addr,
            DmaAddress dma_addr, usize size) noexcept = nullptr;

        /// @brief Synchronise a streaming buffer for CPU access.
        ///
        /// @desc  Call before the CPU reads from a FromDevice or
        ///        Bidirectional buffer. Invalidates cache lines.
        void (*SyncForCpu)(
            DeviceNode& dev, DmaAddress da,
            usize size, DmaDirection direction) noexcept = nullptr;

        /// @brief Synchronise a streaming buffer for device access.
        ///
        /// @desc  Call before the device reads from a ToDevice or
        ///        Bidirectional buffer. Flushes cache lines.
        void (*SyncForDevice)(
            DeviceNode& dev, DmaAddress da,
            usize size, DmaDirection direction) noexcept = nullptr;
    };

    // =========================================================================
    // DmaDescriptor — per-device DMA configuration
    // =========================================================================

    /// @brief DMA capabilities and constraints for a single device.
    ///
    /// @desc  Populated by the bus driver during device enumeration.
    ///        The masks define the hardware's addressable DMA range:
    ///
    ///        - 32-bit DMA only:  dma_mask = 0x0000'0000'FFFF'FFFF
    ///        - Full 64-bit:      dma_mask = ~u64{0}
    ///        - 36-bit (PAE-era): dma_mask = 0x0000'000F'FFFF'FFFF
    ///
    ///        If the device cannot address all of physical memory directly,
    ///        requires_bounce is set and the DMA subsystem must bounce-buffer
    ///        through a low-memory copy.
    struct DmaDescriptor {
        DmaOps ops{};

        /// @brief Addressable DMA range for streaming mappings.
        u64 dma_mask = ~u64{0};

        /// @brief Addressable DMA range for coherent allocations.
        u64 coherent_mask = ~u64{0};

        /// @brief True if some allocations will exceed dma_mask and require
        ///        bounce buffering.
        bool requires_bounce = false;

        // --- Validation helpers ---

        /// @brief Check if a physical address is within the device's DMA mask.
        [[nodiscard]] constexpr bool CanAddress(FoundationKitMemory::PhysicalAddress pa) const noexcept {
            return pa.value <= dma_mask;
        }

        /// @brief Check if a physical address is within the coherent mask.
        [[nodiscard]] constexpr bool CanAddressCoherent(FoundationKitMemory::PhysicalAddress pa) const noexcept {
            return pa.value <= coherent_mask;
        }
    };

    // =========================================================================
    // Identity DMA Ops — trivial implementation for platforms without IOMMU
    //
    // When no IOMMU is present, the DMA address IS the physical address.
    // This is the most common case for bare-metal and simple embedded targets.
    // =========================================================================

    namespace IdentityDma {

        inline KernelResult<DmaAddress> MapSingle(
                DeviceNode& dev, FoundationKitMemory::PhysicalAddress pa,
                usize size, DmaDirection /*direction*/) noexcept {
            (void)dev; (void)size;
            return static_cast<DmaAddress>(pa.value);
        }

        inline void UnmapSingle(
                DeviceNode& /*dev*/, DmaAddress /*da*/,
                usize /*size*/, DmaDirection /*direction*/) noexcept {
            // Identity mapping — nothing to undo.
        }

        inline void SyncForCpu(
                DeviceNode& /*dev*/, DmaAddress /*da*/,
                usize /*size*/, DmaDirection /*direction*/) noexcept {
            // On cache-coherent architectures this is a no-op.
            // On non-coherent archs, the platform overrides DmaOps.
        }

        inline void SyncForDevice(
                DeviceNode& /*dev*/, DmaAddress /*da*/,
                usize /*size*/, DmaDirection /*direction*/) noexcept {
            // Same rationale as SyncForCpu.
        }

        /// @brief Build a DmaDescriptor with identity mapping ops.
        [[nodiscard]] inline DmaDescriptor MakeIdentityDma(u64 mask = ~u64{0}) noexcept {
            DmaDescriptor d;
            d.dma_mask       = mask;
            d.coherent_mask  = mask;
            d.requires_bounce = false;
            d.ops.MapSingle    = &MapSingle;
            d.ops.UnmapSingle  = &UnmapSingle;
            d.ops.SyncForCpu   = &SyncForCpu;
            d.ops.SyncForDevice = &SyncForDevice;
            // AllocCoherent / FreeCoherent require kernel heap integration —
            // left null here; the kernel sets them via DeviceManager.
            d.ops.AllocCoherent = nullptr;
            d.ops.FreeCoherent  = nullptr;
            return d;
        }

    } // namespace IdentityDma

} // namespace FoundationKitDevice
