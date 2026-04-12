#include <FoundationKitPlatform/HostArchitecture.hpp>
#include <FoundationKitPlatform/IrqChip/IrqChip.hpp>
#include <TestFramework.hpp>

#ifdef FOUNDATIONKITPLATFORM_ARCH_X86_64
#include <FoundationKitPlatform/Amd64/IrqChip.hpp>
#endif

using namespace FoundationKitCxxStl;
using namespace FoundationKitPlatform::IrqChip;

// Mock states
static bool g_unmasked = false;
static bool g_masked = false;
static bool g_startup_called = false;
static bool g_shutdown_called = false;
static u32  g_last_hw_irq = 0xFFFF;

static void ResetMockState() {
    g_unmasked = false;
    g_masked = false;
    g_startup_called = false;
    g_shutdown_called = false;
    g_last_hw_irq = 0xFFFF;
}

// ============================================================================
// Abstracted Control Tests (Simulating an IrqManager fallback layer)
// ============================================================================

TEST_CASE(IrqChip_Fallback_StartupToUnmask) {
    ResetMockState();
    
    IrqChipDescriptor chip;
    chip.name = "TestChip";
    chip.Startup = nullptr; // Intentionally null
    chip.Unmask = [](const IrqData& data) noexcept {
        g_unmasked = true;
        g_last_hw_irq = data.hw_irq;
    };
    
    IrqData data{1, 5, nullptr};
    
    // Simulated IrqManager Startup routine
    if (chip.Startup) {
        chip.Startup(data);
    } else if (chip.Unmask) {
        chip.Unmask(data);
    }
    
    EXPECT_TRUE(g_unmasked);
    EXPECT_FALSE(g_startup_called);
    EXPECT_EQ(g_last_hw_irq, 5);
}

TEST_CASE(IrqChip_Startup_PreferredOverUnmask) {
    ResetMockState();
    
    IrqChipDescriptor chip;
    chip.name = "TestChip";
    chip.Startup = [](const IrqData& data) noexcept {
        g_startup_called = true;
    };
    chip.Unmask = [](const IrqData& data) noexcept {
        g_unmasked = true;
    };
    
    IrqData data{1, 5, nullptr};
    
    // Simulated IrqManager Startup routine
    if (chip.Startup) {
        chip.Startup(data);
    } else if (chip.Unmask) {
        chip.Unmask(data);
    }
    
    EXPECT_TRUE(g_startup_called);
    EXPECT_FALSE(g_unmasked);
}

TEST_CASE(IrqChip_SkipNullAdvancedFeatures) {
    ResetMockState();
    
    IrqChipDescriptor chip;
    chip.name = "BasicChip";
    // Everything is null by default
    
    IrqData data{1, 1, nullptr};
    
    bool affinity_success = false;
    // Simulated routing attempt
    if (chip.SetAffinity) {
        chip.SetAffinity(data, 0x1);
        affinity_success = true;
    }
    
    // Must gracefully skip since SetAffinity is null
    EXPECT_FALSE(affinity_success);
}

// ============================================================================
// AMD64 Specific Hardware Stubs Tests
// ============================================================================

#ifdef FOUNDATIONKITPLATFORM_ARCH_X86_64
using namespace FoundationKitPlatform::Amd64;

TEST_CASE(IrqChip_Amd64_Pic_FeaturesNull) {
    auto pic = MakePicIrqChip();
    
    // PIC doesn't support advanced features
    EXPECT_TRUE(pic.SetAffinity == nullptr);
    EXPECT_TRUE(pic.SendIpi == nullptr);
    EXPECT_TRUE(pic.SetType == nullptr);
    EXPECT_TRUE(pic.Startup == nullptr);
    
    // PIC must support core masking and EOI
    EXPECT_TRUE(pic.Mask != nullptr);
    EXPECT_TRUE(pic.Unmask != nullptr);
    EXPECT_TRUE(pic.EndOfInterrupt != nullptr);
    EXPECT_TRUE(pic.Acknowledge != nullptr);
}

// Ensure the PIC mock doesn't crash on standard valid vectors
TEST_CASE(IrqChip_Amd64_Pic_ValidVectors) {
    auto pic = MakePicIrqChip();
    IrqData data{0, 0, nullptr}; // Valid dual-PIC hw_irq
    
    // Check that execution occurs safely without hitting FK_BUG_ON
    pic.Mask(data);
    
    data.hw_irq = 15;
    pic.Unmask(data);
    
    data.hw_irq = 7;
    pic.EndOfInterrupt(data);
    
    EXPECT_TRUE(true);
}
#endif

// ============================================================================
// Concept Constraints Checking
// ============================================================================

struct ValidChipBuilder {
    static IrqChipDescriptor Describe() {
        IrqChipDescriptor d;
        d.name = "ValidChip";
        return d;
    }
};

struct InvalidChipBuilder {
    static int Describe() { return 0; }
};

static_assert(IIrqChip<ValidChipBuilder>);
static_assert(!IIrqChip<InvalidChipBuilder>);

TEST_CASE(IrqChip_Concept_Resolution) {
    // If the test compiles, the static_asserts above correctly evaluate IIrqChip.
    EXPECT_TRUE(true);
}
