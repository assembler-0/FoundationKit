#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>

namespace FoundationKitDevice {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // MatchEntry — driver-to-device matching record
    //
    // Inspired by Linux's of_device_id and pci_device_id.
    //
    // Matching semantics:
    //   1. If `compatible` is non-null, it is compared against the device's
    //      "compatible" property string. Exact match required.
    //   2. If `vendor_id` is non-zero, it must match the device's vendor ID.
    //      If `device_id` is also non-zero, both must match.
    //   3. If `class_code` is non-zero, it is compared against the PCI class
    //      code.
    //   4. All zero/null fields are wildcards — they match anything.
    //   5. A match entry where every field is zero/null matches everything
    //      (catch-all). This is intentional but should be the LAST entry.
    //
    // A driver's match_table is a pointer to a contiguous array of MatchEntry,
    // terminated by a sentinel entry (all zeros). match_count is provided for
    // bounds checking but the sentinel is the canonical end marker.
    // =========================================================================

    struct MatchEntry {
        /// @brief Device-tree / ACPI compatible string.
        ///        e.g., "arm,pl011", "pci8086,2922", "virtio,1"
        ///        Source-lifetime — not copied.
        const char* compatible = nullptr;

        /// @brief PCI / USB vendor ID. 0 = wildcard.
        u16 vendor_id = 0;

        /// @brief PCI / USB device ID. 0 = wildcard.
        u16 device_id = 0;

        /// @brief PCI / USB subsystem vendor. 0 = wildcard.
        u16 subsystem_vendor = 0;

        /// @brief PCI / USB subsystem device. 0 = wildcard.
        u16 subsystem_device = 0;

        /// @brief PCI class code. 0 = wildcard.
        u8 class_code = 0;

        /// @brief Opaque per-match driver context.
        ///
        /// @desc  Allows a single driver to handle multiple variants by passing
        ///        variant-specific data through the match entry. The driver's
        ///        Probe() reads this from the matching entry to decide on
        ///        variant-specific initialisation paths.
        const void* driver_data = nullptr;

        /// @brief Check if this entry is the sentinel (all-zero terminator).
        [[nodiscard]] constexpr bool IsSentinel() const noexcept {
            return compatible == nullptr
                && vendor_id == 0 && device_id == 0
                && subsystem_vendor == 0 && subsystem_device == 0
                && class_code == 0
                && driver_data == nullptr;
        }
    };

    // =========================================================================
    // MatchEntry utility — string comparison for compatible matching
    // =========================================================================

    /// @brief Byte-wise string equality check (no libc dependency).
    [[nodiscard]] inline constexpr bool CompatibleMatch(
            const char* entry_compat, const char* device_compat) noexcept {
        if (entry_compat == nullptr || device_compat == nullptr) return false;
        while (*entry_compat && *device_compat) {
            if (*entry_compat != *device_compat) return false;
            ++entry_compat;
            ++device_compat;
        }
        return *entry_compat == *device_compat;
    }

} // namespace FoundationKitDevice
