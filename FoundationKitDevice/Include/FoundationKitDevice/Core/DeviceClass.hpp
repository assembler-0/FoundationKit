#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Format.hpp>

namespace FoundationKitDevice {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // DeviceClass — Hierarchical device taxonomy
    //
    // Encoding: high byte = category, low byte = sub-class.
    // Use (static_cast<u16>(cls) & 0xFF00) to test category membership.
    //
    // The enum is intentionally exhaustive. New sub-classes are added within
    // their category range. A driver's MatchEntry may specify DeviceClass as
    // a first-pass filter before checking vendor/device IDs or compatible
    // strings.
    // =========================================================================

    /// @brief Hierarchical classification of every device in the system.
    ///
    /// @desc  Models after PCI class codes and Linux device classes.
    ///        High byte = category, low byte = sub-class within that category.
    ///        A driver that handles "any storage controller" can mask with
    ///        `(cls & 0xFF00) == 0x0200`.
    enum class DeviceClass : u16 {
        Unknown           = 0x0000,

        // --- 0x00xx: Platform / System ---
        Platform          = 0x0001,   ///< SoC-integrated (DT/ACPI discovered)
        System            = 0x0002,   ///< HPET, RTC, firmware tables
        Cpu               = 0x0003,   ///< Logical CPU / core
        Firmware          = 0x0004,   ///< UEFI runtime, ACPI, SMBIOS interface

        // --- 0x01xx: Interconnect / Bus ---
        PciBus            = 0x0100,
        UsbBus            = 0x0101,
        I2cBus            = 0x0102,
        SpiBus            = 0x0103,
        VirtIOBus         = 0x0104,
        PlatformBus       = 0x0105,   ///< Pseudo-bus for platform devices

        // --- 0x02xx: Storage ---
        BlockDevice       = 0x0200,
        NvmeController    = 0x0201,
        AhciController    = 0x0202,
        VirtIOBlock       = 0x0203,
        RamDisk           = 0x0204,

        // --- 0x03xx: Network ---
        NetworkInterface  = 0x0300,
        EthernetAdapter   = 0x0301,
        WifiAdapter       = 0x0302,
        VirtIONet         = 0x0303,
        LoopbackDevice    = 0x0304,

        // --- 0x04xx: Graphics ---
        DisplayController = 0x0400,
        GpuDevice         = 0x0401,
        Framebuffer       = 0x0402,
        VirtIOGpu         = 0x0403,

        // --- 0x05xx: Input ---
        InputDevice       = 0x0500,
        Keyboard          = 0x0501,
        Mouse             = 0x0502,
        Touchpad          = 0x0503,
        Tablet            = 0x0504,
        GameController    = 0x0505,

        // --- 0x06xx: Serial / Console ---
        SerialPort        = 0x0600,
        Console           = 0x0601,
        VirtIOConsole     = 0x0602,

        // --- 0x07xx: Audio ---
        AudioController   = 0x0700,
        AudioCodec        = 0x0701,

        // --- 0x08xx: Timer / Clock ---
        TimerDevice       = 0x0800,
        ClockDevice       = 0x0801,

        // --- 0x09xx: Interrupt Controller ---
        IrqController     = 0x0900,
        MsiController     = 0x0901,

        // --- 0x0Axx: Power / Thermal ---
        PowerSupply       = 0x0A00,
        ThermalZone       = 0x0A01,
        VoltageRegulator  = 0x0A02,

        // --- 0x0Bxx: Crypto ---
        CryptoEngine      = 0x0B00,
        TrngDevice        = 0x0B01,

        // --- 0x0Cxx: Misc ---
        WatchdogTimer     = 0x0C00,
        DmaEngine         = 0x0D00,
        IommuDevice       = 0x0E00,
        FirmwareDevice    = 0x0F00,
    };

    // =========================================================================
    // Category helpers
    // =========================================================================

    /// @brief Extract the category byte from a DeviceClass.
    [[nodiscard]] constexpr u16 DeviceClassCategory(DeviceClass cls) noexcept {
        return static_cast<u16>(cls) & 0xFF00;
    }

    /// @brief Test whether a DeviceClass belongs to a specific category.
    [[nodiscard]] constexpr bool DeviceClassInCategory(DeviceClass cls, DeviceClass category) noexcept {
        return DeviceClassCategory(cls) == DeviceClassCategory(category);
    }

    // =========================================================================
    // DeviceClass name — debug / logging only
    // =========================================================================

    /// @brief Return a human-readable name for a DeviceClass enumerator.
    [[nodiscard]] inline const char* DeviceClassName(DeviceClass cls) noexcept {
        switch (cls) {
            case DeviceClass::Unknown:           return "Unknown";
            case DeviceClass::Platform:          return "Platform";
            case DeviceClass::System:            return "System";
            case DeviceClass::Cpu:               return "Cpu";
            case DeviceClass::Firmware:          return "Firmware";
            case DeviceClass::PciBus:            return "PciBus";
            case DeviceClass::UsbBus:            return "UsbBus";
            case DeviceClass::I2cBus:            return "I2cBus";
            case DeviceClass::SpiBus:            return "SpiBus";
            case DeviceClass::VirtIOBus:         return "VirtIOBus";
            case DeviceClass::PlatformBus:       return "PlatformBus";
            case DeviceClass::BlockDevice:       return "BlockDevice";
            case DeviceClass::NvmeController:    return "NvmeController";
            case DeviceClass::AhciController:    return "AhciController";
            case DeviceClass::VirtIOBlock:       return "VirtIOBlock";
            case DeviceClass::RamDisk:           return "RamDisk";
            case DeviceClass::NetworkInterface:  return "NetworkInterface";
            case DeviceClass::EthernetAdapter:   return "EthernetAdapter";
            case DeviceClass::WifiAdapter:       return "WifiAdapter";
            case DeviceClass::VirtIONet:         return "VirtIONet";
            case DeviceClass::LoopbackDevice:    return "LoopbackDevice";
            case DeviceClass::DisplayController: return "DisplayController";
            case DeviceClass::GpuDevice:         return "GpuDevice";
            case DeviceClass::Framebuffer:       return "Framebuffer";
            case DeviceClass::VirtIOGpu:         return "VirtIOGpu";
            case DeviceClass::InputDevice:       return "InputDevice";
            case DeviceClass::Keyboard:          return "Keyboard";
            case DeviceClass::Mouse:             return "Mouse";
            case DeviceClass::Touchpad:          return "Touchpad";
            case DeviceClass::Tablet:            return "Tablet";
            case DeviceClass::GameController:    return "GameController";
            case DeviceClass::SerialPort:        return "SerialPort";
            case DeviceClass::Console:           return "Console";
            case DeviceClass::VirtIOConsole:      return "VirtIOConsole";
            case DeviceClass::AudioController:   return "AudioController";
            case DeviceClass::AudioCodec:        return "AudioCodec";
            case DeviceClass::TimerDevice:       return "TimerDevice";
            case DeviceClass::ClockDevice:       return "ClockDevice";
            case DeviceClass::IrqController:     return "IrqController";
            case DeviceClass::MsiController:     return "MsiController";
            case DeviceClass::PowerSupply:       return "PowerSupply";
            case DeviceClass::ThermalZone:       return "ThermalZone";
            case DeviceClass::VoltageRegulator:  return "VoltageRegulator";
            case DeviceClass::CryptoEngine:      return "CryptoEngine";
            case DeviceClass::TrngDevice:        return "TrngDevice";
            case DeviceClass::WatchdogTimer:     return "WatchdogTimer";
            case DeviceClass::DmaEngine:         return "DmaEngine";
            case DeviceClass::IommuDevice:       return "IommuDevice";
            case DeviceClass::FirmwareDevice:    return "FirmwareDevice";
        }
        return "<invalid>";
    }

} // namespace FoundationKitDevice

// Formatter specialization must live in FoundationKitCxxStl (where the primary template is).
namespace FoundationKitCxxStl {
    template <>
    struct Formatter<FoundationKitDevice::DeviceClass> {
        template <typename Sink>
        void Format(Sink& sb, const FoundationKitDevice::DeviceClass& cls, const FormatSpec& = {}) {
            const char* name = FoundationKitDevice::DeviceClassName(cls);
            usize len = 0;
            while (name[len]) ++len;
            sb.Append(name, len);
        }
    };
} // namespace FoundationKitCxxStl
