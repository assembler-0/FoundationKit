#pragma once

#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Meta/Concepts.hpp>

namespace FoundationKitPlatform::IrqChip {

    using namespace FoundationKitCxxStl;

    // ============================================================================
    // Trigger and Delivery Configurations
    // ============================================================================

    enum class IrqTriggerType : u8 {
        EdgeRising,
        EdgeFalling,
        LevelHigh,
        LevelLow,
    };

    enum class IrqDeliveryMode : u8 {
        Fixed,
        LowestPriority,
        SMI,
        NMI,
        Init,
        ExtINT,
    };

    // ============================================================================
    // IrqData Context
    // ============================================================================

    /// @brief Context passed to every chip operation. Mirrors Linux's `irq_data`.
    /// 
    /// Distinguishes the logical OS IRQ number from the hardware pin, while
    /// carrying chip-specific data (e.g. MMIO base, cascaded state). By passing
    /// this, a single stateless IrqChipDescriptor instance can manage multiple
    /// identical physical controllers.
    struct IrqData {
        /// @brief The high-level/logical global IRQ number managed by the OS.
        u32 logical_irq = 0;
        
        /// @brief The hardware pin or vector on the respective controller.
        u32 hw_irq = 0;
        
        /// @brief Controller-specific opaque context pointer.
        void* chip_ctx = nullptr;
    };

    // ============================================================================
    // IrqChipDescriptor
    // ============================================================================

    /// @brief FoundationKit abstraction of a hardware interrupt controller.
    ///
    /// Every function pointer is strictly optional. If the hardware does not
    /// support a capability, the pointer should be explicitly set to `nullptr`. 
    /// The owning subsystem (e.g., IrqManager) must check for null before invoking.
    struct IrqChipDescriptor {
        /// @brief Human-readable identifier for debugging (e.g., "i8259A", "IO-APIC-edge").
        const char* name = nullptr;
        
        // --- Core Flow Operations ---
        
        /// @brief Initialise and unmask the IRQ line. Fallback is Unmask().
        void (*Startup)(const IrqData& data) noexcept = nullptr;
        
        /// @brief Shut down and mask the IRQ line. Fallback is Mask().
        void (*Shutdown)(const IrqData& data) noexcept = nullptr;
        
        /// @brief Enable the IRQ line.
        void (*Enable)(const IrqData& data) noexcept = nullptr;
        
        /// @brief Disable the IRQ line.
        void (*Disable)(const IrqData& data) noexcept = nullptr;
        
        /// @brief Unmask the interrupt in the hardware register (crucial).
        void (*Unmask)(const IrqData& data) noexcept = nullptr;
        
        /// @brief Mask the interrupt in the hardware register (crucial).
        void (*Mask)(const IrqData& data) noexcept = nullptr;
        
        /// @brief Pre-ack the interrupt. Required by some edge-triggered controllers before processing.
        void (*Acknowledge)(const IrqData& data) noexcept = nullptr;

        /// @brief End-of-Interrupt (EOI). Tells the controller the CPU has finished handling.
        void (*EndOfInterrupt)(const IrqData& data) noexcept = nullptr;
        
        // --- Advanced Control (Optional) ---
        
        /// @brief Direct SMP routing to a specific CPU or set of CPUs.
        void (*SetAffinity)(const IrqData& data, u64 cpu_mask) noexcept = nullptr;
        
        /// @brief Change polarity or trigger mode dynamically (e.g. PCI vs ISA configuration).
        void (*SetType)(const IrqData& data, IrqTriggerType type) noexcept = nullptr;
        
        /// @brief Trigger an Inter-Processor Interrupt explicitly via this controller.
        void (*SendIpi)(const IrqData& data, u32 cpu_target, u32 vector, IrqDeliveryMode mode) noexcept = nullptr;
    };

    // ============================================================================
    // IIrqChip concept
    // ============================================================================

    /// @brief A type that can produce an IrqChipDescriptor.
    /// Architectural builders typically satisfy this if wrapped into a struct, 
    /// but practically, subsystems just consume the descriptor directly.
    template<typename T>
    concept IIrqChip = requires(T t) {
        { t.Describe() } -> SameAs<IrqChipDescriptor>;
    };

} // namespace FoundationKitPlatform::IrqChip
