#include <TestFramework.hpp>
#include <FoundationKitDevice/Core/DeviceClass.hpp>
#include <FoundationKitDevice/Core/DeviceState.hpp>
#include <FoundationKitDevice/Core/DeviceProperty.hpp>
#include <FoundationKitDevice/Core/DeviceNode.hpp>
#include <FoundationKitDevice/Driver/MatchEntry.hpp>
#include <FoundationKitDevice/Driver/DriverDescriptor.hpp>
#include <FoundationKitDevice/Driver/DriverRegistry.hpp>
#include <FoundationKitDevice/Bus/BusDescriptor.hpp>
#include <FoundationKitDevice/Bus/BusNode.hpp>
#include <FoundationKitDevice/Power/PowerManager.hpp>
#include <FoundationKitDevice/Resource/ResourceDescriptor.hpp>
#include <FoundationKitDevice/Dma/DmaDescriptor.hpp>
#include <FoundationKitDevice/DeviceManager.hpp>

// Integration test: IrqChip and Clocksource living as DeviceNodes
#include <FoundationKitPlatform/IrqChip/IrqChip.hpp>
#include <FoundationKitPlatform/Clocksource/ClockSource.hpp>

using namespace FoundationKitCxxStl;
using namespace FoundationKitDevice;

// ============================================================================
// Mock driver state — global for lambda capture-less callbacks
// ============================================================================

static bool g_mock_probe_called  = false;
static bool g_mock_attach_called = false;
static bool g_mock_detach_called = false;
static bool g_mock_shutdown_called = false;
static bool g_mock_suspend_called = false;
static bool g_mock_resume_called = false;
static bool g_mock_probe_should_fail = false;
static bool g_mock_attach_should_fail = false;
static u32  g_mock_enumerate_count = 0;
static bool g_mock_configure_called = false;

static void ResetMockState() {
    g_mock_probe_called    = false;
    g_mock_attach_called   = false;
    g_mock_detach_called   = false;
    g_mock_shutdown_called = false;
    g_mock_suspend_called  = false;
    g_mock_resume_called   = false;
    g_mock_probe_should_fail  = false;
    g_mock_attach_should_fail = false;
    g_mock_enumerate_count = 0;
    g_mock_configure_called = false;
}

// ============================================================================
// Mock driver ops
// ============================================================================

static KernelResult<void> MockProbe(DeviceNode& dev) noexcept {
    g_mock_probe_called = true;
    if (g_mock_probe_should_fail) return Unexpected(KernelError::NotSupported);
    return {};
}

static KernelResult<void> MockAttach(DeviceNode& dev) noexcept {
    g_mock_attach_called = true;
    if (g_mock_attach_should_fail) return Unexpected(KernelError::NotSupported);
    return {};
}

static void MockDetach(DeviceNode& dev) noexcept {
    g_mock_detach_called = true;
    dev.driver_private = nullptr;
}

static void MockShutdown(DeviceNode& dev) noexcept {
    g_mock_shutdown_called = true;
}

static KernelResult<void> MockSuspend(DeviceNode& dev, DevicePowerState target) noexcept {
    g_mock_suspend_called = true;
    return {};
}

static KernelResult<void> MockResume(DeviceNode& dev, DevicePowerState target) noexcept {
    g_mock_resume_called = true;
    return {};
}

// ============================================================================
// Mock match table
// ============================================================================

static const MatchEntry g_mock_match_table[] = {
    { .compatible = "mock,test-device-v1", .vendor_id = 0, .device_id = 0 },
    { .compatible = "mock,test-device-v2", .vendor_id = 0, .device_id = 0 },
    { .compatible = nullptr, .vendor_id = 0x1234, .device_id = 0x5678 },
    { /* sentinel */ }
};

static DriverDescriptor g_mock_driver = {
    .name         = "mock-driver",
    .target_class = DeviceClass::BlockDevice,
    .match_table  = g_mock_match_table,
    .match_count  = 3,
    .ops = {
        .Probe    = MockProbe,
        .Attach   = MockAttach,
        .Detach   = MockDetach,
        .Shutdown = MockShutdown,
        .Suspend  = MockSuspend,
        .Resume   = MockResume,
    },
    .priority = 100,
};

// A second driver with higher priority (for priority test)
static const MatchEntry g_mock_match_table_hi[] = {
    { .compatible = "mock,test-device-v1" },
    { /* sentinel */ }
};

static DriverDescriptor g_mock_driver_hi = {
    .name         = "mock-driver-hi-prio",
    .target_class = DeviceClass::BlockDevice,
    .match_table  = g_mock_match_table_hi,
    .match_count  = 1,
    .ops = {
        .Probe    = MockProbe,
        .Attach   = MockAttach,
        .Detach   = MockDetach,
    },
    .priority = 200,
};

// A class-only driver (no match table)
static DriverDescriptor g_mock_class_driver = {
    .name         = "mock-fb-driver",
    .target_class = DeviceClass::Framebuffer,
    .match_table  = nullptr,
    .match_count  = 0,
    .ops = {
        .Probe  = MockProbe,
        .Attach = MockAttach,
        .Detach = MockDetach,
    },
    .priority = 50,
};

// ============================================================================
// Mock bus ops
// ============================================================================

static KernelResult<void> MockBusEnumerate(DeviceNode& bus_dev) noexcept {
    g_mock_enumerate_count++;
    return {};
}

static bool MockBusMatch(const DeviceNode& dev, const DriverDescriptor& drv) noexcept {
    // Simple: just delegate to compatible check
    if (dev.compatible && drv.match_table) {
        for (u32 i = 0; i < drv.match_count; ++i) {
            if (drv.match_table[i].IsSentinel()) break;
            if (drv.match_table[i].compatible &&
                CompatibleMatch(drv.match_table[i].compatible, dev.compatible)) {
                return true;
            }
        }
    }
    return false;
}

static KernelResult<void> MockBusConfigure(DeviceNode& dev) noexcept {
    g_mock_configure_called = true;
    return {};
}

static BusDescriptor g_mock_bus_desc = {
    .name      = "mock-bus",
    .bus_class = DeviceClass::PlatformBus,
    .ops = {
        .Enumerate       = MockBusEnumerate,
        .TranslateAddress = nullptr,
        .MatchDriver     = MockBusMatch,
        .ConfigureDevice = MockBusConfigure,
        .RemoveDevice    = nullptr,
    },
};

// ============================================================================
// Helper: reset DeviceManager + DriverRegistry between tests
// ============================================================================

static void ResetAll() {
    ResetMockState();
    DeviceManager::ResetSubsystem();
    DriverRegistry::ResetSubsystem();
}

// ============================================================================
//  1. DeviceClass Tests
// ============================================================================

TEST_CASE(Device_DeviceClass_CategoryExtraction) {
    EXPECT_EQ(DeviceClassCategory(DeviceClass::PciBus),         static_cast<u16>(0x0100));
    EXPECT_EQ(DeviceClassCategory(DeviceClass::UsbBus),         static_cast<u16>(0x0100));
    EXPECT_EQ(DeviceClassCategory(DeviceClass::BlockDevice),    static_cast<u16>(0x0200));
    EXPECT_EQ(DeviceClassCategory(DeviceClass::NvmeController), static_cast<u16>(0x0200));
    EXPECT_EQ(DeviceClassCategory(DeviceClass::Framebuffer),    static_cast<u16>(0x0400));
    EXPECT_EQ(DeviceClassCategory(DeviceClass::Unknown),        static_cast<u16>(0x0000));
}

TEST_CASE(Device_DeviceClass_InCategory) {
    // NvmeController is in the same category as BlockDevice
    EXPECT_TRUE(DeviceClassInCategory(DeviceClass::NvmeController, DeviceClass::BlockDevice));
    EXPECT_TRUE(DeviceClassInCategory(DeviceClass::AhciController, DeviceClass::BlockDevice));
    // Framebuffer is NOT in the Storage category
    EXPECT_FALSE(DeviceClassInCategory(DeviceClass::Framebuffer, DeviceClass::BlockDevice));
    // IrqController is in the 0x09xx category
    EXPECT_TRUE(DeviceClassInCategory(DeviceClass::MsiController, DeviceClass::IrqController));
}

TEST_CASE(Device_DeviceClass_NameNotNull) {
    // Every known class should have a non-null, non-empty name
    const char* n = DeviceClassName(DeviceClass::AhciController);
    EXPECT_TRUE(n != nullptr);
    EXPECT_TRUE(n[0] != '\0');
}

// ============================================================================
//  2. DeviceState FSM Tests
// ============================================================================

TEST_CASE(Device_DeviceState_LegalTransitions) {
    EXPECT_TRUE(IsLegalTransition(DeviceState::Uninitialized, DeviceState::Discovered));
    EXPECT_TRUE(IsLegalTransition(DeviceState::Discovered, DeviceState::Probing));
    EXPECT_TRUE(IsLegalTransition(DeviceState::Probing, DeviceState::Bound));
    EXPECT_TRUE(IsLegalTransition(DeviceState::Bound, DeviceState::Active));
    EXPECT_TRUE(IsLegalTransition(DeviceState::Active, DeviceState::Suspended));
    EXPECT_TRUE(IsLegalTransition(DeviceState::Suspended, DeviceState::Active));
    EXPECT_TRUE(IsLegalTransition(DeviceState::Active, DeviceState::Stopping));
    EXPECT_TRUE(IsLegalTransition(DeviceState::Stopping, DeviceState::Detached));
    EXPECT_TRUE(IsLegalTransition(DeviceState::Detached, DeviceState::Discovered));
}

TEST_CASE(Device_DeviceState_IllegalTransitions) {
    EXPECT_FALSE(IsLegalTransition(DeviceState::Uninitialized, DeviceState::Active));
    EXPECT_FALSE(IsLegalTransition(DeviceState::Active, DeviceState::Uninitialized));
    EXPECT_FALSE(IsLegalTransition(DeviceState::Discovered, DeviceState::Active));
    EXPECT_FALSE(IsLegalTransition(DeviceState::Probing, DeviceState::Active));
    EXPECT_FALSE(IsLegalTransition(DeviceState::Bound, DeviceState::Discovered));
    EXPECT_FALSE(IsLegalTransition(DeviceState::Suspended, DeviceState::Discovered));
    EXPECT_FALSE(IsLegalTransition(DeviceState::Detached, DeviceState::Active));
    // Self-transitions must all be illegal
    EXPECT_FALSE(IsLegalTransition(DeviceState::Active, DeviceState::Active));
    EXPECT_FALSE(IsLegalTransition(DeviceState::Error, DeviceState::Error));
}

TEST_CASE(Device_DeviceState_ErrorReachable) {
    // Error should be reachable from most operational states
    EXPECT_TRUE(IsLegalTransition(DeviceState::Discovered, DeviceState::Error));
    EXPECT_TRUE(IsLegalTransition(DeviceState::Probing, DeviceState::Error));
    EXPECT_TRUE(IsLegalTransition(DeviceState::Bound, DeviceState::Error));
    EXPECT_TRUE(IsLegalTransition(DeviceState::Active, DeviceState::Error));
    EXPECT_TRUE(IsLegalTransition(DeviceState::Suspended, DeviceState::Error));
    EXPECT_TRUE(IsLegalTransition(DeviceState::Stopping, DeviceState::Error));
    // But NOT from Uninitialized or Detached
    EXPECT_FALSE(IsLegalTransition(DeviceState::Uninitialized, DeviceState::Error));
}

TEST_CASE(Device_DeviceState_ErrorRecovery) {
    // From Error, only Stopping or Detached are reachable
    EXPECT_TRUE(IsLegalTransition(DeviceState::Error, DeviceState::Stopping));
    EXPECT_TRUE(IsLegalTransition(DeviceState::Error, DeviceState::Detached));
    EXPECT_FALSE(IsLegalTransition(DeviceState::Error, DeviceState::Active));
    EXPECT_FALSE(IsLegalTransition(DeviceState::Error, DeviceState::Discovered));
}

TEST_CASE(Device_DeviceState_NameRoundTrip) {
    const char* n = DeviceStateName(DeviceState::Active);
    EXPECT_TRUE(n != nullptr);
    EXPECT_TRUE(n[0] == 'A'); // "Active"
}

// ============================================================================
//  3. DeviceProperty Tests
// ============================================================================

TEST_CASE(Device_DeviceProperty_SetGetU64) {
    DevicePropertyStore store;
    store.SetU64("test-val", 42);
    EXPECT_EQ(store.GetU64("test-val"), static_cast<u64>(42));
    EXPECT_EQ(store.Count(), static_cast<usize>(1));
}

TEST_CASE(Device_DeviceProperty_SetGetString) {
    DevicePropertyStore store;
    store.SetString("compatible", "mock,test-v1");
    const char* val = store.GetString("compatible");
    EXPECT_TRUE(CompatibleMatch(val, "mock,test-v1"));
}

TEST_CASE(Device_DeviceProperty_SetGetBool) {
    DevicePropertyStore store;
    store.SetBool("is-removable", true);
    EXPECT_TRUE(store.GetBool("is-removable"));
    store.SetBool("is-removable", false);
    EXPECT_FALSE(store.GetBool("is-removable"));
    // Overwrite should not increase count
    EXPECT_EQ(store.Count(), static_cast<usize>(1));
}

TEST_CASE(Device_DeviceProperty_SetGetI64) {
    DevicePropertyStore store;
    store.SetI64("temperature", -40);
    EXPECT_EQ(store.GetI64("temperature"), static_cast<i64>(-40));
}

TEST_CASE(Device_DeviceProperty_SetGetBlob) {
    DevicePropertyStore store;
    u8 data[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    store.SetBlob("firmware-blob", data, 4);
    EXPECT_TRUE(store.Has("firmware-blob"));
}

TEST_CASE(Device_DeviceProperty_Overwrite) {
    DevicePropertyStore store;
    store.SetU64("version", 1);
    EXPECT_EQ(store.GetU64("version"), static_cast<u64>(1));
    store.SetU64("version", 2);
    EXPECT_EQ(store.GetU64("version"), static_cast<u64>(2));
    EXPECT_EQ(store.Count(), static_cast<usize>(1)); // Same key, no growth
}

TEST_CASE(Device_DeviceProperty_TryGet_Missing) {
    DevicePropertyStore store;
    u64 val = 999;
    EXPECT_FALSE(store.TryGetU64("nonexistent", val));
    EXPECT_EQ(val, static_cast<u64>(999)); // Unchanged
}

TEST_CASE(Device_DeviceProperty_TryGetString_Missing) {
    DevicePropertyStore store;
    const char* val = nullptr;
    EXPECT_FALSE(store.TryGetString("nonexistent", val));
    EXPECT_TRUE(val == nullptr);
}

TEST_CASE(Device_DeviceProperty_Has) {
    DevicePropertyStore store;
    EXPECT_FALSE(store.Has("foo"));
    store.SetU64("foo", 1);
    EXPECT_TRUE(store.Has("foo"));
}

TEST_CASE(Device_DeviceProperty_Remove) {
    DevicePropertyStore store;
    store.SetU64("a", 1);
    store.SetU64("b", 2);
    EXPECT_EQ(store.Count(), static_cast<usize>(2));
    EXPECT_TRUE(store.Remove("a"));
    EXPECT_EQ(store.Count(), static_cast<usize>(1));
    EXPECT_FALSE(store.Has("a"));
    EXPECT_TRUE(store.Has("b"));
    // Remove nonexistent returns false
    EXPECT_FALSE(store.Remove("a"));
}

TEST_CASE(Device_DeviceProperty_MultipleKeys) {
    DevicePropertyStore store;
    const char* const keys[] = {"key0", "key1", "key2", "key3", "key4", "key5", "key6", "key7", "key8", "key9"};
    for (usize i = 0; i < 10; ++i) {
        store.SetU64(keys[i], i * 10);
    }
    EXPECT_EQ(store.Count(), static_cast<usize>(10));
    EXPECT_EQ(store.GetU64("key5"), static_cast<u64>(50));
}

TEST_CASE(Device_DeviceProperty_HashCollisionHandled) {
    // FNV-1a should give different hashes for different short keys,
    // but even if two keys hash the same, KeysEqual does byte compare.
    DevicePropertyStore store;
    store.SetU64("ab", 1);
    store.SetU64("ba", 2);
    EXPECT_EQ(store.GetU64("ab"), static_cast<u64>(1));
    EXPECT_EQ(store.GetU64("ba"), static_cast<u64>(2));
    EXPECT_EQ(store.Count(), static_cast<usize>(2));
}

// ============================================================================
//  4. ResourceDescriptor Tests
// ============================================================================

TEST_CASE(Device_ResourceTable_AddAndFind) {
    ResourceTable table;
    EXPECT_EQ(table.count, static_cast<u8>(0));

    ResourceDescriptor mmio;
    mmio.type  = ResourceType::Mmio;
    mmio.base  = 0xFED00000;
    mmio.size  = 0x1000;
    mmio.index = 0;
    table.Add(mmio);

    ResourceDescriptor irq;
    irq.type  = ResourceType::Irq;
    irq.base  = 16;
    irq.size  = 1;
    irq.index = 0;
    table.Add(irq);

    EXPECT_EQ(table.count, static_cast<u8>(2));

    const auto* found_mmio = table.FindByType(ResourceType::Mmio);
    EXPECT_TRUE(found_mmio != nullptr);
    EXPECT_EQ(found_mmio->base, static_cast<u64>(0xFED00000));

    const auto* found_irq = table.FindByType(ResourceType::Irq);
    EXPECT_TRUE(found_irq != nullptr);
    EXPECT_EQ(found_irq->base, static_cast<u64>(16));

    const auto* found_dma = table.FindByType(ResourceType::DmaChannel);
    EXPECT_TRUE(found_dma == nullptr);
}

TEST_CASE(Device_ResourceTable_FindByTypeAndIndex) {
    ResourceTable table;
    for (u8 i = 0; i < 6; ++i) {
        ResourceDescriptor r;
        r.type  = ResourceType::Mmio;
        r.base  = 0x80000000 + i * 0x10000;
        r.size  = 0x10000;
        r.index = i;
        table.Add(r);
    }
    const auto* bar2 = table.FindByTypeAndIndex(ResourceType::Mmio, 2);
    EXPECT_TRUE(bar2 != nullptr);
    EXPECT_EQ(bar2->base, static_cast<u64>(0x80020000));
    EXPECT_TRUE(table.FindByTypeAndIndex(ResourceType::Mmio, 10) == nullptr);
}

TEST_CASE(Device_ResourceTable_CountByType) {
    ResourceTable table;
    for (u8 i = 0; i < 4; ++i) {
        ResourceDescriptor r;
        r.type = ResourceType::Irq;
        r.base = 32 + i;
        r.index = i;
        table.Add(r);
    }
    ResourceDescriptor m;
    m.type = ResourceType::Mmio;
    m.base = 0;
    table.Add(m);

    EXPECT_EQ(table.CountByType(ResourceType::Irq), static_cast<u8>(4));
    EXPECT_EQ(table.CountByType(ResourceType::Mmio), static_cast<u8>(1));
    EXPECT_EQ(table.CountByType(ResourceType::DmaChannel), static_cast<u8>(0));
}

TEST_CASE(Device_ResourceDescriptor_Contains) {
    ResourceDescriptor r;
    r.type = ResourceType::Mmio;
    r.base = 0x1000;
    r.size = 0x100;

    EXPECT_TRUE(r.Contains(0x1000));
    EXPECT_TRUE(r.Contains(0x10FF));
    EXPECT_FALSE(r.Contains(0x0FFF));
    EXPECT_FALSE(r.Contains(0x1100));
}

TEST_CASE(Device_ResourceDescriptor_Flags) {
    ResourceDescriptor r;
    r.flags = 0;
    EXPECT_FALSE(r.HasFlag(ResourceFlags::Cacheable));

    r.SetFlag(ResourceFlags::Cacheable);
    EXPECT_TRUE(r.HasFlag(ResourceFlags::Cacheable));

    r.SetFlag(ResourceFlags::Shareable);
    EXPECT_TRUE(r.HasFlag(ResourceFlags::Cacheable));
    EXPECT_TRUE(r.HasFlag(ResourceFlags::Shareable));

    r.ClearFlag(ResourceFlags::Cacheable);
    EXPECT_FALSE(r.HasFlag(ResourceFlags::Cacheable));
    EXPECT_TRUE(r.HasFlag(ResourceFlags::Shareable));
}

// ============================================================================
//  5. MatchEntry Tests
// ============================================================================

TEST_CASE(Device_MatchEntry_CompatibleMatch) {
    EXPECT_TRUE(CompatibleMatch("arm,pl011", "arm,pl011"));
    EXPECT_FALSE(CompatibleMatch("arm,pl011", "arm,pl012"));
    EXPECT_FALSE(CompatibleMatch("arm,pl011", "arm,pl01"));
    EXPECT_FALSE(CompatibleMatch(nullptr, "arm,pl011"));
    EXPECT_FALSE(CompatibleMatch("arm,pl011", nullptr));
}

TEST_CASE(Device_MatchEntry_Sentinel) {
    MatchEntry sentinel{};
    EXPECT_TRUE(sentinel.IsSentinel());

    MatchEntry non_sentinel;
    non_sentinel.compatible = "test";
    EXPECT_FALSE(non_sentinel.IsSentinel());

    MatchEntry vendor_only;
    vendor_only.vendor_id = 0x1234;
    EXPECT_FALSE(vendor_only.IsSentinel());
}

// ============================================================================
//  6. DriverDescriptor & DriverRegistry Tests
// ============================================================================

TEST_CASE(Device_DriverRegistry_RegisterAndQuery) {
    ResetAll();

    DriverRegistry::Register(g_mock_driver);
    EXPECT_EQ(DriverRegistry::Count(), static_cast<u32>(1));

    auto all = DriverRegistry::All();
    EXPECT_EQ(all.Size(), static_cast<usize>(1));
    EXPECT_TRUE(all[0] == &g_mock_driver);
}

TEST_CASE(Device_DriverRegistry_FindMatch_Compatible) {
    ResetAll();
    DriverRegistry::Register(g_mock_driver);

    DeviceNode dev;
    dev.device_class = DeviceClass::BlockDevice;
    dev.compatible = "mock,test-device-v1";

    const MatchEntry* match_entry = nullptr;
    auto* found = DriverRegistry::FindMatch(dev, &match_entry);
    EXPECT_TRUE(found != nullptr);
    EXPECT_TRUE(found == &g_mock_driver);
    EXPECT_TRUE(match_entry != nullptr);
    EXPECT_TRUE(CompatibleMatch(match_entry->compatible, "mock,test-device-v1"));
}

TEST_CASE(Device_DriverRegistry_FindMatch_VendorDevice) {
    ResetAll();
    DriverRegistry::Register(g_mock_driver);

    DeviceNode dev;
    dev.device_class = DeviceClass::BlockDevice;
    dev.compatible = nullptr;
    dev.properties.SetU64("vendor-id", 0x1234);
    dev.properties.SetU64("device-id", 0x5678);

    auto* found = DriverRegistry::FindMatch(dev);
    EXPECT_TRUE(found != nullptr);
    EXPECT_TRUE(found == &g_mock_driver);
}

TEST_CASE(Device_DriverRegistry_FindMatch_NoMatch) {
    ResetAll();
    DriverRegistry::Register(g_mock_driver);

    DeviceNode dev;
    dev.device_class = DeviceClass::NetworkInterface; // Wrong class
    dev.compatible = "unmatched,device";

    auto* found = DriverRegistry::FindMatch(dev);
    EXPECT_TRUE(found == nullptr);
}

TEST_CASE(Device_DriverRegistry_FindMatch_Priority) {
    ResetAll();
    DriverRegistry::Register(g_mock_driver);
    DriverRegistry::Register(g_mock_driver_hi);

    DeviceNode dev;
    dev.device_class = DeviceClass::BlockDevice;
    dev.compatible = "mock,test-device-v1";

    auto* found = DriverRegistry::FindMatch(dev);
    EXPECT_TRUE(found != nullptr);
    // Higher priority driver should win
    EXPECT_TRUE(found == &g_mock_driver_hi);
    EXPECT_EQ(found->priority, static_cast<u16>(200));
}

TEST_CASE(Device_DriverRegistry_FindMatch_ClassOnly) {
    ResetAll();
    DriverRegistry::Register(g_mock_class_driver);

    DeviceNode dev;
    dev.device_class = DeviceClass::Framebuffer;
    dev.compatible = nullptr;

    auto* found = DriverRegistry::FindMatch(dev);
    EXPECT_TRUE(found != nullptr);
    EXPECT_TRUE(found == &g_mock_class_driver);
}

// ============================================================================
//  7. BusNode Tests
// ============================================================================

TEST_CASE(Device_BusNode_PciCreation) {
    BusNode node = MakePciBusNode(&g_mock_bus_desc, 0, 0);
    EXPECT_TRUE(node.descriptor == &g_mock_bus_desc);
    EXPECT_EQ(node.topology.pci.segment, static_cast<u16>(0));
    EXPECT_EQ(node.topology.pci.bus, static_cast<u8>(0));
    EXPECT_FALSE(node.enumerated);
    node.Validate(); // Must not crash
}

TEST_CASE(Device_BusNode_PlatformCreation) {
    BusNode node = MakePlatformBusNode(&g_mock_bus_desc, 0xFE000000);
    EXPECT_TRUE(node.descriptor == &g_mock_bus_desc);
    EXPECT_EQ(node.topology.platform.base_address, static_cast<u64>(0xFE000000));
    node.Validate();
}

// ============================================================================
//  8. DMA Tests
// ============================================================================

TEST_CASE(Device_Dma_IdentityMapping) {
    using namespace FoundationKitMemory;
    auto dma = IdentityDma::MakeIdentityDma();

    EXPECT_TRUE(dma.ops.MapSingle != nullptr);
    EXPECT_TRUE(dma.ops.UnmapSingle != nullptr);
    EXPECT_TRUE(dma.ops.SyncForCpu != nullptr);
    EXPECT_TRUE(dma.ops.SyncForDevice != nullptr);
    EXPECT_TRUE(dma.ops.AllocCoherent == nullptr); // Not provided by identity
    EXPECT_EQ(dma.dma_mask, ~u64{0});
    EXPECT_FALSE(dma.requires_bounce);
}

TEST_CASE(Device_Dma_MaskCheck) {
    using namespace FoundationKitMemory;
    DmaDescriptor dma;
    dma.dma_mask = 0xFFFFFFFF; // 32-bit only

    EXPECT_TRUE(dma.CanAddress(PhysicalAddress{0x00000000}));
    EXPECT_TRUE(dma.CanAddress(PhysicalAddress{0xFFFFFFFF}));
    EXPECT_FALSE(dma.CanAddress(PhysicalAddress{0x100000000}));
}

TEST_CASE(Device_Dma_CoherentMask) {
    using namespace FoundationKitMemory;
    DmaDescriptor dma;
    dma.coherent_mask = 0x0FFFFFFFFF; // 36-bit

    EXPECT_TRUE(dma.CanAddressCoherent(PhysicalAddress{0x0FFFFFFFFF}));
    EXPECT_FALSE(dma.CanAddressCoherent(PhysicalAddress{0x1000000000}));
}

// ============================================================================
//  9. PowerState Tests
// ============================================================================

TEST_CASE(Device_Power_LegalTransitions) {
    EXPECT_TRUE(IsLegalPowerTransition(DevicePowerState::D0_Active, DevicePowerState::D1_Light));
    EXPECT_TRUE(IsLegalPowerTransition(DevicePowerState::D1_Light, DevicePowerState::D2_Deep));
    EXPECT_TRUE(IsLegalPowerTransition(DevicePowerState::D2_Deep, DevicePowerState::D3_Off));
    // Resume direction
    EXPECT_TRUE(IsLegalPowerTransition(DevicePowerState::D3_Off, DevicePowerState::D2_Deep));
    EXPECT_TRUE(IsLegalPowerTransition(DevicePowerState::D1_Light, DevicePowerState::D0_Active));
    // Initial power-on
    EXPECT_TRUE(IsLegalPowerTransition(DevicePowerState::D_Unknown, DevicePowerState::D0_Active));
    EXPECT_TRUE(IsLegalPowerTransition(DevicePowerState::D_Unknown, DevicePowerState::D3_Off));
}

TEST_CASE(Device_Power_IllegalTransitions) {
    // Skipping D-states (D0 → D3 directly) is illegal
    EXPECT_FALSE(IsLegalPowerTransition(DevicePowerState::D0_Active, DevicePowerState::D3_Off));
    EXPECT_FALSE(IsLegalPowerTransition(DevicePowerState::D3_Off, DevicePowerState::D0_Active));
    // Self-transition is illegal
    EXPECT_FALSE(IsLegalPowerTransition(DevicePowerState::D0_Active, DevicePowerState::D0_Active));
    // D_Unknown can only go to D0 or D3
    EXPECT_FALSE(IsLegalPowerTransition(DevicePowerState::D_Unknown, DevicePowerState::D1_Light));
    EXPECT_FALSE(IsLegalPowerTransition(DevicePowerState::D_Unknown, DevicePowerState::D2_Deep));
}

// ============================================================================
// 10. DeviceNode Unit Tests
// ============================================================================

TEST_CASE(Device_DeviceNode_InitialState) {
    DeviceNode node;
    EXPECT_EQ(node.State(), DeviceState::Uninitialized);
    EXPECT_EQ(node.PowerState(), DevicePowerState::D_Unknown);
    EXPECT_FALSE(node.IsBound());
    EXPECT_FALSE(node.IsActive());
    EXPECT_FALSE(node.IsBus());
    EXPECT_TRUE(node.IsRoot()); // No parent
    EXPECT_FALSE(node.HasChildren());
    EXPECT_EQ(node.ChildCount(), static_cast<usize>(0));
    EXPECT_EQ(node.resources.count, static_cast<u8>(0));
    EXPECT_EQ(node.properties.Count(), static_cast<usize>(0));
}

TEST_CASE(Device_DeviceNode_TransitionTo) {
    DeviceNode node;
    node.name = "test-node";
    node.device_id = 99;
    node.TransitionTo(DeviceState::Discovered);
    EXPECT_EQ(node.State(), DeviceState::Discovered);
    node.TransitionTo(DeviceState::Probing);
    EXPECT_EQ(node.State(), DeviceState::Probing);
    node.TransitionTo(DeviceState::Bound);
    EXPECT_EQ(node.State(), DeviceState::Bound);
    node.TransitionTo(DeviceState::Active);
    EXPECT_EQ(node.State(), DeviceState::Active);
}

TEST_CASE(Device_DeviceNode_PropertyConvenience) {
    DeviceNode node;
    node.properties.SetU64("vendor-id", 0x8086);
    node.properties.SetU64("device-id", 0x2922);

    EXPECT_EQ(node.VendorId(), static_cast<u16>(0x8086));
    EXPECT_EQ(node.DeviceIdProp(), static_cast<u16>(0x2922));
}

TEST_CASE(Device_DeviceNode_TryVendorId_Missing) {
    DeviceNode node;
    EXPECT_EQ(node.TryVendorId(), static_cast<u16>(0));
    EXPECT_EQ(node.TryDeviceIdProp(), static_cast<u16>(0));
}

// ============================================================================
// 11. DeviceManager Integration Tests
// ============================================================================

TEST_CASE(Device_DeviceManager_Init) {
    ResetAll();

    DeviceNode root;
    DeviceManager::Init(&root);

    EXPECT_TRUE(DeviceManager::Root() == &root);
    EXPECT_EQ(DeviceManager::DeviceCount(), static_cast<usize>(1));
    EXPECT_EQ(root.State(), DeviceState::Discovered);
    EXPECT_TRUE(CompatibleMatch(root.name, "system"));
    EXPECT_EQ(root.device_class, DeviceClass::System);
}

TEST_CASE(Device_DeviceManager_AttachDevice) {
    ResetAll();

    DeviceNode root;
    DeviceManager::Init(&root);

    DeviceNode child;
    child.name = "test-child";
    child.device_class = DeviceClass::BlockDevice;

    DeviceManager::AttachDevice(&child, &root);

    EXPECT_EQ(DeviceManager::DeviceCount(), static_cast<usize>(2));
    EXPECT_EQ(child.State(), DeviceState::Discovered);
    EXPECT_TRUE(child.parent == &root);
    EXPECT_TRUE(root.HasChildren());
    EXPECT_EQ(root.ChildCount(), static_cast<usize>(1));
}

TEST_CASE(Device_DeviceManager_ProbeAndBind_Success) {
    ResetAll();

    DeviceNode root;
    DeviceManager::Init(&root);

    DriverRegistry::Register(g_mock_driver);

    DeviceNode dev;
    dev.name = "ahci0";
    dev.device_class = DeviceClass::BlockDevice;
    dev.compatible = "mock,test-device-v1";

    DeviceManager::AttachDevice(&dev, &root);

    bool bound = DeviceManager::ProbeAndBind(dev);
    EXPECT_TRUE(bound);
    EXPECT_TRUE(g_mock_probe_called);
    EXPECT_TRUE(g_mock_attach_called);
    EXPECT_EQ(dev.State(), DeviceState::Active);
    EXPECT_TRUE(dev.IsBound());
    EXPECT_TRUE(dev.bound_driver == &g_mock_driver);
}

TEST_CASE(Device_DeviceManager_ProbeAndBind_ProbeFail) {
    ResetAll();

    DeviceNode root;
    DeviceManager::Init(&root);

    DriverRegistry::Register(g_mock_driver);
    g_mock_probe_should_fail = true;

    DeviceNode dev;
    dev.name = "fail-dev";
    dev.device_class = DeviceClass::BlockDevice;
    dev.compatible = "mock,test-device-v1";

    DeviceManager::AttachDevice(&dev, &root);

    bool bound = DeviceManager::ProbeAndBind(dev);
    EXPECT_FALSE(bound);
    EXPECT_TRUE(g_mock_probe_called);
    EXPECT_FALSE(g_mock_attach_called);
    EXPECT_EQ(dev.State(), DeviceState::Error);
    EXPECT_FALSE(dev.IsBound());
}

TEST_CASE(Device_DeviceManager_ProbeAndBind_NoDriver) {
    ResetAll();

    DeviceNode root;
    DeviceManager::Init(&root);
    // Register no drivers

    DeviceNode dev;
    dev.name = "orphan-dev";
    dev.device_class = DeviceClass::NetworkInterface;
    dev.compatible = "nobody,matches-me";

    DeviceManager::AttachDevice(&dev, &root);

    bool bound = DeviceManager::ProbeAndBind(dev);
    EXPECT_FALSE(bound);
    EXPECT_EQ(dev.State(), DeviceState::Discovered); // Stays discovered
}

TEST_CASE(Device_DeviceManager_DetachDevice) {
    ResetAll();

    DeviceNode root;
    DeviceManager::Init(&root);

    DriverRegistry::Register(g_mock_driver);

    DeviceNode dev;
    dev.name = "detach-test";
    dev.device_class = DeviceClass::BlockDevice;
    dev.compatible = "mock,test-device-v1";

    DeviceManager::AttachDevice(&dev, &root);
    DeviceManager::ProbeAndBind(dev);
    EXPECT_TRUE(dev.IsActive());

    DeviceManager::DetachDevice(&dev);
    EXPECT_TRUE(g_mock_detach_called);
    EXPECT_EQ(dev.State(), DeviceState::Detached);
    EXPECT_FALSE(dev.IsBound());
    EXPECT_TRUE(dev.parent == nullptr);
    EXPECT_EQ(DeviceManager::DeviceCount(), static_cast<usize>(1)); // only root
}

TEST_CASE(Device_DeviceManager_FindByClass) {
    ResetAll();

    DeviceNode root;
    DeviceManager::Init(&root);

    DeviceNode net_dev;
    net_dev.name = "eth0";
    net_dev.device_class = DeviceClass::EthernetAdapter;
    DeviceManager::AttachDevice(&net_dev, &root);

    DeviceNode blk_dev;
    blk_dev.name = "nvme0";
    blk_dev.device_class = DeviceClass::NvmeController;
    DeviceManager::AttachDevice(&blk_dev, &root);

    auto* found = DeviceManager::FindByClass(DeviceClass::EthernetAdapter);
    EXPECT_TRUE(found != nullptr);
    EXPECT_TRUE(found == &net_dev);

    auto* found2 = DeviceManager::FindByClass(DeviceClass::NvmeController);
    EXPECT_TRUE(found2 == &blk_dev);

    auto* not_found = DeviceManager::FindByClass(DeviceClass::AudioController);
    EXPECT_TRUE(not_found == nullptr);
}

TEST_CASE(Device_DeviceManager_FindByName) {
    ResetAll();

    DeviceNode root;
    DeviceManager::Init(&root);

    DeviceNode dev;
    dev.name = "serial0";
    dev.device_class = DeviceClass::SerialPort;
    DeviceManager::AttachDevice(&dev, &root);

    auto* found = DeviceManager::FindByName("serial0");
    EXPECT_TRUE(found == &dev);

    auto* not_found = DeviceManager::FindByName("serial1");
    EXPECT_TRUE(not_found == nullptr);
}

TEST_CASE(Device_DeviceManager_FindById) {
    ResetAll();

    DeviceNode root;
    DeviceManager::Init(&root);

    DeviceNode dev1, dev2;
    dev1.name = "dev1";
    dev1.device_class = DeviceClass::Platform;
    dev2.name = "dev2";
    dev2.device_class = DeviceClass::Platform;

    DeviceManager::AttachDevice(&dev1, &root);
    DeviceManager::AttachDevice(&dev2, &root);

    auto* found = DeviceManager::FindById(dev2.device_id);
    EXPECT_TRUE(found == &dev2);
}

TEST_CASE(Device_DeviceManager_FindAllInCategory) {
    ResetAll();

    DeviceNode root;
    DeviceManager::Init(&root);

    DeviceNode nvme, ahci, eth;
    nvme.name = "nvme0"; nvme.device_class = DeviceClass::NvmeController;
    ahci.name = "ahci0"; ahci.device_class = DeviceClass::AhciController;
    eth.name  = "eth0";  eth.device_class  = DeviceClass::EthernetAdapter;

    DeviceManager::AttachDevice(&nvme, &root);
    DeviceManager::AttachDevice(&ahci, &root);
    DeviceManager::AttachDevice(&eth, &root);

    DeviceNode* results[8];
    usize count = DeviceManager::FindAllInCategory(DeviceClass::BlockDevice, results, 8);
    EXPECT_EQ(count, static_cast<usize>(2)); // nvme + ahci both in 0x02xx
}

TEST_CASE(Device_DeviceManager_MultiLevelTree) {
    ResetAll();

    DeviceNode root;
    DeviceManager::Init(&root);

    // Level 1: PCI bus
    DeviceNode pci_bus;
    pci_bus.name = "pci0";
    pci_bus.device_class = DeviceClass::PciBus;
    DeviceManager::AttachDevice(&pci_bus, &root);

    // Level 2: devices on PCI bus
    DeviceNode ahci, gpu;
    ahci.name = "ahci0"; ahci.device_class = DeviceClass::AhciController;
    gpu.name  = "gpu0";  gpu.device_class  = DeviceClass::GpuDevice;
    DeviceManager::AttachDevice(&ahci, &pci_bus);
    DeviceManager::AttachDevice(&gpu, &pci_bus);

    EXPECT_EQ(DeviceManager::DeviceCount(), static_cast<usize>(4)); // root + pci + ahci + gpu
    EXPECT_EQ(pci_bus.ChildCount(), static_cast<usize>(2));
    EXPECT_TRUE(ahci.parent == &pci_bus);
    EXPECT_TRUE(gpu.parent == &pci_bus);
    EXPECT_TRUE(pci_bus.parent == &root);

    // DumpTree should not crash
    DeviceManager::DumpTree();
}

TEST_CASE(Device_DeviceManager_ProbeAndBindAll) {
    ResetAll();

    DeviceNode root;
    DeviceManager::Init(&root);

    DriverRegistry::Register(g_mock_driver);
    DriverRegistry::Register(g_mock_class_driver);

    DeviceNode blk, fb;
    blk.name = "blk0"; blk.device_class = DeviceClass::BlockDevice;
    blk.compatible = "mock,test-device-v2";
    fb.name = "fb0";   fb.device_class = DeviceClass::Framebuffer;

    DeviceManager::AttachDevice(&blk, &root);
    DeviceManager::AttachDevice(&fb, &root);

    usize bound = DeviceManager::ProbeAndBindAll();
    EXPECT_EQ(bound, static_cast<usize>(2));
    EXPECT_TRUE(blk.IsActive());
    EXPECT_TRUE(fb.IsActive());
}

TEST_CASE(Device_DeviceManager_ShutdownAll) {
    ResetAll();

    DeviceNode root;
    DeviceManager::Init(&root);

    DriverRegistry::Register(g_mock_driver);

    DeviceNode dev;
    dev.name = "shutdown-test";
    dev.device_class = DeviceClass::BlockDevice;
    dev.compatible = "mock,test-device-v1";

    DeviceManager::AttachDevice(&dev, &root);
    DeviceManager::ProbeAndBind(dev);

    g_mock_shutdown_called = false;
    DeviceManager::ShutdownAll();
    EXPECT_TRUE(g_mock_shutdown_called);
}

TEST_CASE(Device_DeviceManager_RecursiveDetach) {
    ResetAll();

    DeviceNode root;
    DeviceManager::Init(&root);

    DeviceNode parent_dev, child1, child2;
    parent_dev.name = "parent"; parent_dev.device_class = DeviceClass::PciBus;
    child1.name = "child1"; child1.device_class = DeviceClass::AhciController;
    child2.name = "child2"; child2.device_class = DeviceClass::GpuDevice;

    DeviceManager::AttachDevice(&parent_dev, &root);
    DeviceManager::AttachDevice(&child1, &parent_dev);
    DeviceManager::AttachDevice(&child2, &parent_dev);

    EXPECT_EQ(DeviceManager::DeviceCount(), static_cast<usize>(4));

    // Detaching parent should recursively detach children
    DeviceManager::DetachDevice(&parent_dev);

    EXPECT_EQ(DeviceManager::DeviceCount(), static_cast<usize>(1)); // Only root
    EXPECT_EQ(child1.State(), DeviceState::Detached);
    EXPECT_EQ(child2.State(), DeviceState::Detached);
    EXPECT_EQ(parent_dev.State(), DeviceState::Detached);
}

// ============================================================================
// 12. Bus Integration Tests
// ============================================================================

TEST_CASE(Device_DeviceManager_BusEnumeration) {
    ResetAll();

    DeviceNode root;
    DeviceManager::Init(&root);

    DriverRegistry::Register(g_mock_driver);

    // Create a mock bus driver to match the bus node
    static DriverDescriptor bus_driver = {
        .name = "mock-bus-driver",
        .target_class = DeviceClass::PlatformBus,
        .match_table = nullptr,
        .match_count = 0,
        .ops = {
            .Probe  = [](DeviceNode& d) noexcept -> KernelResult<void> { return {}; },
            .Attach = [](DeviceNode& d) noexcept -> KernelResult<void> { return {}; },
        },
        .priority = 100,
    };
    DriverRegistry::Register(bus_driver);

    BusNode bus_data = MakePlatformBusNode(&g_mock_bus_desc);

    DeviceNode bus_dev;
    bus_dev.name = "platform-bus";
    bus_dev.device_class = DeviceClass::PlatformBus;
    bus_dev.bus_data = &bus_data;

    DeviceManager::AttachDevice(&bus_dev, &root);

    // ProbeAndBind the bus should trigger enumeration
    bool bound = DeviceManager::ProbeAndBind(bus_dev);
    EXPECT_TRUE(bound);
    EXPECT_TRUE(bus_dev.IsActive());
    EXPECT_EQ(g_mock_enumerate_count, static_cast<u32>(1));
    EXPECT_TRUE(bus_data.enumerated);
}

// ============================================================================
// 13. IDriver / IBus Concept Tests
// ============================================================================

struct ValidDriverBuilder {
    static DriverDescriptor Describe() {
        DriverDescriptor d;
        d.name = "valid";
        return d;
    }
};

struct InvalidDriverBuilder {
    static int Describe() { return 0; }
};

struct ValidBusBuilder {
    static BusDescriptor Describe() {
        BusDescriptor b;
        b.name = "valid-bus";
        return b;
    }
};

struct InvalidBusBuilder {
    static int Describe() { return 0; }
};

static_assert(IDriver<ValidDriverBuilder>);
static_assert(!IDriver<InvalidDriverBuilder>);
static_assert(IBus<ValidBusBuilder>);
static_assert(!IBus<InvalidBusBuilder>);

TEST_CASE(Device_Concepts_Resolved) {
    // If the test compiles, the static_asserts above are correct.
    EXPECT_TRUE(true);
}

// ============================================================================
// 14. Non-polymorphic static_assert
// ============================================================================

static_assert(!__is_polymorphic(DeviceNode),
    "DeviceNode must not be polymorphic");
static_assert(!__is_polymorphic(DriverDescriptor),
    "DriverDescriptor must not be polymorphic");
static_assert(!__is_polymorphic(BusDescriptor),
    "BusDescriptor must not be polymorphic");
static_assert(!__is_polymorphic(DmaDescriptor),
    "DmaDescriptor must not be polymorphic");
static_assert(!__is_polymorphic(ResourceDescriptor),
    "ResourceDescriptor must not be polymorphic");

TEST_CASE(Device_NoPolymorphism_Verified) {
    EXPECT_TRUE(true);
}

// ============================================================================
// 15. IrqChip / Clocksource Compatibility — Bridge Pattern Test
//
// This test demonstrates that existing IrqChipDescriptor and
// ClockSourceDescriptor can coexist with DeviceNode by storing
// the descriptor pointer in driver_private and the descriptor
// in a property.
//
// This proves NO source changes are needed to IrqChip or Clocksource.
// ============================================================================

TEST_CASE(Device_IrqChipBridge_StoreAsDriverPrivate) {
    using namespace FoundationKitPlatform::IrqChip;

    // 1. Create an IrqChipDescriptor (existing API, unchanged)
    IrqChipDescriptor pic;
    pic.name = "mock-pic";
    pic.Mask = [](const IrqData& data) noexcept {};
    pic.Unmask = [](const IrqData& data) noexcept {};
    pic.EndOfInterrupt = [](const IrqData& data) noexcept {};

    // 2. Create a DeviceNode representing this interrupt controller
    ResetAll();
    DeviceNode root;
    DeviceManager::Init(&root);

    DeviceNode irq_dev;
    irq_dev.name = "irq-controller0";
    irq_dev.device_class = DeviceClass::IrqController;
    irq_dev.compatible = "mock,pic-irqchip";

    // 3. Store the IrqChipDescriptor as driver_private — zero-cost bridge
    irq_dev.driver_private = &pic;

    // 4. Store metadata as properties
    irq_dev.properties.SetString("irqchip-name", pic.name);
    irq_dev.properties.SetBool("supports-affinity", pic.SetAffinity != nullptr);
    irq_dev.properties.SetBool("supports-ipi", pic.SendIpi != nullptr);

    DeviceManager::AttachDevice(&irq_dev, &root);

    // 5. Verify: we can retrieve the IrqChipDescriptor from any code
    //    that has a DeviceNode* for the interrupt controller.
    auto* retrieved = static_cast<IrqChipDescriptor*>(irq_dev.driver_private);
    EXPECT_TRUE(retrieved != nullptr);
    EXPECT_TRUE(CompatibleMatch(retrieved->name, "mock-pic"));
    EXPECT_TRUE(retrieved->Mask != nullptr);
    EXPECT_TRUE(retrieved->Unmask != nullptr);
    EXPECT_TRUE(retrieved->EndOfInterrupt != nullptr);
    EXPECT_TRUE(retrieved->SetAffinity == nullptr);

    // Property queries work as expected
    EXPECT_FALSE(irq_dev.properties.GetBool("supports-affinity"));
    EXPECT_FALSE(irq_dev.properties.GetBool("supports-ipi"));
}

TEST_CASE(Device_ClockSourceBridge_StoreAsDriverPrivate) {
    using namespace FoundationKitPlatform::Clocksource;

    // 1. Create a ClockSourceDescriptor (existing API, unchanged)
    ClockSourceDescriptor tsc;
    tsc.name = "mock-tsc";
    tsc.Read = []() noexcept -> u64 { return 123456789; };
    tsc.rating = ClockSourceRating::Excellent;
    tsc.is_smp_safe = true;
    tsc.mask = ~u64{0};
    tsc.mult = CalibrateMult(3000000000ULL); // 3 GHz
    tsc.shift = 32;

    // 2. Create a DeviceNode for this clock device
    ResetAll();
    DeviceNode root;
    DeviceManager::Init(&root);

    DeviceNode clock_dev;
    clock_dev.name = "tsc0";
    clock_dev.device_class = DeviceClass::TimerDevice;
    clock_dev.compatible = "x86,tsc";
    clock_dev.driver_private = &tsc;

    clock_dev.properties.SetString("clocksource-name", tsc.name);
    clock_dev.properties.SetU64("rating", static_cast<u64>(tsc.rating));
    clock_dev.properties.SetBool("smp-safe", tsc.is_smp_safe);

    DeviceManager::AttachDevice(&clock_dev, &root);

    // 3. Verify retrieval
    auto* retrieved = static_cast<ClockSourceDescriptor*>(clock_dev.driver_private);
    EXPECT_TRUE(retrieved != nullptr);
    EXPECT_TRUE(CompatibleMatch(retrieved->name, "mock-tsc"));
    EXPECT_EQ(retrieved->Read(), static_cast<u64>(123456789));
    EXPECT_TRUE(retrieved->is_smp_safe);
    EXPECT_EQ(clock_dev.properties.GetU64("rating"),
        static_cast<u64>(ClockSourceRating::Excellent));
}

// ============================================================================
// 16. ForEach and DeviceNode iteration
// ============================================================================

TEST_CASE(Device_DeviceManager_ForEach) {
    ResetAll();

    DeviceNode root;
    DeviceManager::Init(&root);

    DeviceNode d1, d2, d3;
    d1.name = "d1"; d1.device_class = DeviceClass::Platform;
    d2.name = "d2"; d2.device_class = DeviceClass::Platform;
    d3.name = "d3"; d3.device_class = DeviceClass::Platform;

    DeviceManager::AttachDevice(&d1, &root);
    DeviceManager::AttachDevice(&d2, &root);
    DeviceManager::AttachDevice(&d3, &root);

    u32 count = 0;
    DeviceManager::ForEach([&](DeviceNode& dev) noexcept {
        ++count;
    });
    EXPECT_EQ(count, static_cast<u32>(4)); // root + 3
}

TEST_CASE(Device_DeviceNode_ForEachChild) {
    ResetAll();

    DeviceNode root;
    DeviceManager::Init(&root);

    DeviceNode c1, c2;
    c1.name = "c1"; c1.device_class = DeviceClass::Platform;
    c2.name = "c2"; c2.device_class = DeviceClass::Platform;

    DeviceManager::AttachDevice(&c1, &root);
    DeviceManager::AttachDevice(&c2, &root);

    u32 child_count = 0;
    root.ForEachChild([&](DeviceNode& child) noexcept {
        ++child_count;
    });
    EXPECT_EQ(child_count, static_cast<u32>(2));
}

// ============================================================================
// 17. Bus registration
// ============================================================================

TEST_CASE(Device_DeviceManager_RegisterBus) {
    ResetAll();
    DeviceNode root;
    DeviceManager::Init(&root);

    DeviceManager::RegisterBus(g_mock_bus_desc);

    auto* found = DeviceManager::FindBus(DeviceClass::PlatformBus);
    EXPECT_TRUE(found != nullptr);
    EXPECT_TRUE(found == &g_mock_bus_desc);

    auto* not_found = DeviceManager::FindBus(DeviceClass::PciBus);
    EXPECT_TRUE(not_found == nullptr);
}

// ============================================================================
// 18. Edge: DeviceNode with resources, DMA, and properties all together
// ============================================================================

TEST_CASE(Device_FullDevice_Integration) {
    using namespace FoundationKitMemory;
    ResetAll();

    DeviceNode root;
    DeviceManager::Init(&root);

    DriverRegistry::Register(g_mock_driver);

    DeviceNode ahci;
    ahci.name = "ahci0";
    ahci.device_class = DeviceClass::BlockDevice;
    ahci.compatible = "mock,test-device-v1";

    // Set properties
    ahci.properties.SetU64("vendor-id", 0x8086);
    ahci.properties.SetU64("device-id", 0x2922);
    ahci.properties.SetString("compatible", "mock,test-device-v1");
    ahci.properties.SetBool("hotplug-capable", false);

    // Add resources
    ResourceDescriptor bar0;
    bar0.type = ResourceType::Mmio; bar0.base = 0xFEBFF000;
    bar0.size = 0x1000; bar0.index = 0;
    ahci.resources.Add(bar0);

    ResourceDescriptor irq;
    irq.type = ResourceType::Irq; irq.base = 23; irq.size = 1; irq.index = 0;
    ahci.resources.Add(irq);

    // Set DMA
    ahci.dma = IdentityDma::MakeIdentityDma(0xFFFFFFFF); // 32-bit DMA

    DeviceManager::AttachDevice(&ahci, &root);

    // Bind
    bool bound = DeviceManager::ProbeAndBind(ahci);
    EXPECT_TRUE(bound);

    // Verify everything is wired up
    EXPECT_TRUE(ahci.IsActive());
    EXPECT_EQ(ahci.VendorId(), static_cast<u16>(0x8086));
    EXPECT_EQ(ahci.DeviceIdProp(), static_cast<u16>(0x2922));
    EXPECT_EQ(ahci.resources.count, static_cast<u8>(2));
    EXPECT_TRUE(ahci.resources.FindByType(ResourceType::Mmio) != nullptr);
    EXPECT_TRUE(ahci.resources.FindByType(ResourceType::Irq) != nullptr);
    EXPECT_EQ(ahci.dma.dma_mask, static_cast<u64>(0xFFFFFFFF));
    EXPECT_TRUE(ahci.dma.CanAddress(PhysicalAddress{0xFEBFF000}));
    EXPECT_FALSE(ahci.dma.CanAddress(PhysicalAddress{0x100000000}));

    // Cleanup
    DeviceManager::DetachDevice(&ahci);
    EXPECT_EQ(ahci.State(), DeviceState::Detached);
}
