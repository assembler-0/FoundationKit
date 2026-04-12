#pragma once

#include <FoundationKitPlatform/HostArchitecture.hpp>

#ifdef FOUNDATIONKITPLATFORM_ARCH_X86_64

#include <FoundationKitPlatform/Amd64/Cpu.hpp>
#include <FoundationKitPlatform/IrqChip/IrqChip.hpp>

namespace FoundationKitPlatform::Amd64 {

    using namespace FoundationKitCxxStl;
    using namespace FoundationKitPlatform::IrqChip;

    // ============================================================================
    // 8259A PIC Dummy Implementation Example
    // ============================================================================
    
    /// @brief Creates an IrqChipDescriptor for the standard 8259A PIC.
    ///
    /// This is an example mapping of an archaic controller onto the generic API.
    /// The PIC doesn't support SMP, IPIs, or dynamic trigger modification, 
    /// so all advanced features are deliberately left as nullptr.
    [[nodiscard]] inline IrqChipDescriptor MakePicIrqChip() noexcept {
        IrqChipDescriptor d;
        d.name = "i8259A";

        // The PIC is controlled by writing bitmasks to IO ports 0x21 (Master)
        // and 0xA1 (Slave). We mask by setting bits, unmask by clearing them.
        
        d.Mask = [](const IrqData& data) noexcept {
            // Simulated PIC mask behavior snippet
            FK_BUG_ON(data.hw_irq >= 16, "MakePicIrqChip/Mask: hw_irq >= 16 is impossible for dual-PIC setup");
            // e.g. Outb(0x21, Inb(0x21) | (1 << data.hw_irq));
        };

        d.Unmask = [](const IrqData& data) noexcept {
            FK_BUG_ON(data.hw_irq >= 16, "MakePicIrqChip/Unmask: hw_irq >= 16 is impossible");
            // e.g. Outb(0x21, Inb(0x21) & ~(1 << data.hw_irq));
        };

        d.EndOfInterrupt = [](const IrqData& data) noexcept {
            FK_BUG_ON(data.hw_irq >= 16, "MakePicIrqChip/EOI: hw_irq >= 16 is impossible");
            // EOI is port 0x20 writing 0x20. For slave (hw_irq >= 8), also write to 0xA0.
        };

        // Let the generic layer fall back to Mask/Unmask for Startup/Shutdown.
        d.Startup = nullptr;
        d.Shutdown = nullptr;
        d.Enable = nullptr;
        d.Disable = nullptr;

        // For edge-triggered, acknowledging usually involves masking first.
        d.Acknowledge = d.Mask; 

        // Unsupported Features: PIC does not support SMP routing or IPIs
        d.SetAffinity = nullptr;
        d.SetType = nullptr;
        d.SendIpi = nullptr;

        return d;
    }

} // namespace FoundationKitPlatform::Amd64

#endif // FOUNDATIONKITPLATFORM_ARCH_X86_64
