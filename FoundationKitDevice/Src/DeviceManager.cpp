#include <FoundationKitDevice/DeviceManager.hpp>
#include <FoundationKitDevice/Driver/DriverRegistry.hpp>
#include <FoundationKitDevice/Power/PowerManager.hpp>

namespace FoundationKitDevice {

    // =========================================================================
    // DeviceManager static storage
    // =========================================================================

    DeviceNode*       DeviceManager::s_root           = nullptr;
    Structure::IntrusiveDoublyLinkedList DeviceManager::s_all_devices;
    usize             DeviceManager::s_device_count   = 0;
    u32               DeviceManager::s_next_device_id = 1;
    BusDescriptor*    DeviceManager::s_buses[kMaxBusTypes] = {};
    usize             DeviceManager::s_bus_count      = 0;

    // =========================================================================
    // DriverRegistry static storage
    // =========================================================================

    DriverDescriptor* DriverRegistry::s_drivers[kMaxDrivers] = {};
    u32               DriverRegistry::s_count = 0;

    // =========================================================================
    // DriverRegistry::FindMatch — driver matching engine
    // =========================================================================

    DriverDescriptor* DriverRegistry::FindMatch(
            const DeviceNode& dev,
            const MatchEntry** out_match_entry) noexcept {

        DriverDescriptor* best = nullptr;
        const MatchEntry* best_entry = nullptr;
        u16 best_prio = 0;

        for (u32 d = 0; d < s_count; ++d) {
            DriverDescriptor* drv = s_drivers[d];
            FK_BUG_ON(drv == nullptr,
                "DriverRegistry::FindMatch: null driver at index {}", d);

            // Step 1: class filter. DeviceClass::Unknown means "match any".
            if (drv->target_class != DeviceClass::Unknown &&
                drv->target_class != dev.device_class) {
                continue;
            }

            // Step 2: match table.
            bool matched = false;
            const MatchEntry* matched_entry = nullptr;

            if (drv->match_table && drv->match_count > 0) {
                for (u32 m = 0; m < drv->match_count; ++m) {
                    const MatchEntry& entry = drv->match_table[m];
                    if (entry.IsSentinel()) break;

                    bool entry_matches = true;

                    // Compatible string match.
                    if (entry.compatible != nullptr) {
                        if (dev.compatible == nullptr ||
                            !CompatibleMatch(entry.compatible, dev.compatible)) {
                            entry_matches = false;
                        }
                    }

                    // Vendor ID match.
                    if (entry_matches && entry.vendor_id != 0) {
                        const u16 dev_vendor = dev.TryVendorId();
                        if (dev_vendor != entry.vendor_id) {
                            entry_matches = false;
                        }
                    }

                    // Device ID match.
                    if (entry_matches && entry.device_id != 0) {
                        const u16 dev_devid = dev.TryDeviceIdProp();
                        if (dev_devid != entry.device_id) {
                            entry_matches = false;
                        }
                    }

                    // Subsystem vendor.
                    if (entry_matches && entry.subsystem_vendor != 0) {
                        u64 sub_v = 0;
                        if (!dev.properties.TryGetU64("subsystem-vendor-id", sub_v) ||
                            static_cast<u16>(sub_v) != entry.subsystem_vendor) {
                            entry_matches = false;
                        }
                    }

                    // Subsystem device.
                    if (entry_matches && entry.subsystem_device != 0) {
                        u64 sub_d = 0;
                        if (!dev.properties.TryGetU64("subsystem-device-id", sub_d) ||
                            static_cast<u16>(sub_d) != entry.subsystem_device) {
                            entry_matches = false;
                        }
                    }

                    // Class code.
                    if (entry_matches && entry.class_code != 0) {
                        u64 cc = 0;
                        if (!dev.properties.TryGetU64("class-code", cc) ||
                            static_cast<u8>(cc) != entry.class_code) {
                            entry_matches = false;
                        }
                    }

                    if (entry_matches) {
                        matched = true;
                        matched_entry = &entry;
                        break; // First match wins within a driver's table.
                    }
                }
            } else {
                // No match table — the class filter alone is sufficient.
                // This covers "class-only" drivers (e.g., a generic
                // framebuffer driver that handles all Framebuffer devices).
                if (drv->target_class != DeviceClass::Unknown) {
                    matched = true;
                }
            }

            if (matched && drv->priority > best_prio) {
                best = drv;
                best_prio = drv->priority;
                best_entry = matched_entry;
            }
        }

        if (out_match_entry) {
            *out_match_entry = best_entry;
        }

        return best;
    }

    // =========================================================================
    // PowerManager implementation
    // =========================================================================

    KernelResult<void> PowerManager::SuspendDevice(
            DeviceNode& dev, DevicePowerState target) noexcept {
        FK_BUG_ON(!dev.IsBound(),
            "PowerManager::SuspendDevice: device[{}] '{}' has no bound driver",
            dev.device_id, dev.name ? dev.name : "<unnamed>");

        if (!dev.bound_driver->ops.Suspend) {
            // No Suspend op — device does not support power management.
            // Transition directly.
            dev.TransitionPowerTo(target);
            return {};
        }

        auto result = dev.bound_driver->ops.Suspend(dev, target);
        if (!result) {
            FK_LOG_ERR("PowerManager: Suspend failed for device[{}] '{}': {}",
                dev.device_id, dev.name ? dev.name : "<unnamed>",
                result.Error());
            return result;
        }

        dev.TransitionPowerTo(target);
        dev.TransitionTo(DeviceState::Suspended);
        return {};
    }

    KernelResult<void> PowerManager::ResumeDevice(
            DeviceNode& dev, DevicePowerState target) noexcept {
        FK_BUG_ON(!dev.IsBound(),
            "PowerManager::ResumeDevice: device[{}] '{}' has no bound driver",
            dev.device_id, dev.name ? dev.name : "<unnamed>");

        if (!dev.bound_driver->ops.Resume) {
            // No Resume op — device does not support power management.
            dev.TransitionPowerTo(target);
            return {};
        }

        auto result = dev.bound_driver->ops.Resume(dev, target);
        if (!result) {
            FK_LOG_ERR("PowerManager: Resume failed for device[{}] '{}': {}",
                dev.device_id, dev.name ? dev.name : "<unnamed>",
                result.Error());
            return result;
        }

        dev.TransitionPowerTo(target);
        if (target == DevicePowerState::D0_Active) {
            dev.TransitionTo(DeviceState::Active);
        }
        return {};
    }

    KernelResult<void> PowerManager::SuspendSubtree(
            DeviceNode& root, DevicePowerState target) noexcept {
        // Suspend children first (depth-first, leaves → root).
        auto* child_node = root.children.Begin();
        while (child_node != root.children.End()) {
            auto* next = child_node->next;
            DeviceNode* child = Structure::ContainerOf<
                DeviceNode, &DeviceNode::sibling_node>(child_node);

            auto result = SuspendSubtree(*child, target);
            if (!result) {
                FK_LOG_ERR("PowerManager: aborting subtree suspend at device[{}] '{}' — "
                    "child device[{}] '{}' failed",
                    root.device_id, root.name ? root.name : "<unnamed>",
                    child->device_id, child->name ? child->name : "<unnamed>");
                // Rollback: resume already-suspended children.
                auto* rollback_node = root.children.Begin();
                while (rollback_node != child_node) {
                    auto* rb_next = rollback_node->next;
                    DeviceNode* rb_child = Structure::ContainerOf<
                        DeviceNode, &DeviceNode::sibling_node>(rollback_node);
                    if (rb_child->State() == DeviceState::Suspended) {
                        ResumeSubtree(*rb_child, DevicePowerState::D0_Active);
                    }
                    rollback_node = rb_next;
                }
                return result;
            }

            child_node = next;
        }

        // Suspend the root itself.
        if (root.IsBound() && root.IsActive()) {
            return SuspendDevice(root, target);
        }

        return {};
    }

    KernelResult<void> PowerManager::ResumeSubtree(
            DeviceNode& root, DevicePowerState target) noexcept {
        // Resume the root first (parent before children).
        if (root.IsBound() && root.State() == DeviceState::Suspended) {
            auto result = ResumeDevice(root, target);
            if (!result) return result;
        }

        // Then resume children.
        auto* child_node = root.children.Begin();
        while (child_node != root.children.End()) {
            auto* next = child_node->next;
            DeviceNode* child = Structure::ContainerOf<
                DeviceNode, &DeviceNode::sibling_node>(child_node);

            auto result = ResumeSubtree(*child, target);
            if (!result) {
                FK_LOG_ERR("PowerManager: resume failed for child device[{}] '{}' "
                    "under parent device[{}] '{}'",
                    child->device_id, child->name ? child->name : "<unnamed>",
                    root.device_id, root.name ? root.name : "<unnamed>");
                return result;
            }

            child_node = next;
        }

        return {};
    }

} // namespace FoundationKitDevice
