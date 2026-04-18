#pragma once

#define FOUNDATIONKIT_VERSION "2026h1"

#include <FoundationKitCxxStl/Base/Compiler.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>
#include <FoundationKitPlatform/HostArchitecture.hpp>
#include <FoundationKitPlatform/MachineWidth.hpp>
#if defined(FOUNDATIONKITPLATFORM_ARCH_X86_64)
#include <FoundationKitPlatform/Amd64/Cpu.hpp>
#elif defined(FOUNDATIONKITPLATFORM_ARCH_ARM64)
#include <FoundationKitPlatform/Arm64/Cpu.hpp>
#endif

namespace FoundationKitCxxStl {
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void PrintFoundationKitInfo() {
        FK_LOG_INFO("FoundationKit (R) {} ({} {}) - copyright (C) 2026 assembler-0 All rights reserved.", FOUNDATIONKIT_VERSION,
            __DATE__, __TIME__);
        FK_LOG_INFO("FoundationKit (R) configuration:");
        FK_LOG_INFO("   |---> Host architecture: {}", FOUNDATIONKITPLATFORM_ARCH_NAME);
        FK_LOG_INFO("   |---> Host machine width: {}", FOUNDATIONKITPLATFORM_MACHINE_WITDH);
        FK_LOG_INFO("   |---> Compiler: {}", FOUNDATIONKIT_COMPILER);
        FK_LOG_INFO("   |---> Compiler 128-bit integer support: {}", FOUNDATIONKITCXXSTL_BOOL_TO_STR(FOUNDATIONKITCXXSTL_HAS_INT128));
#if defined(FOUNDATIONKITPLATFORM_ARCH_X86_64)
        FK_LOG_INFO("   |---> [Amd64] CPU Vendor: {}", FoundationKitPlatform::Amd64::GetVendor().string);
        FK_LOG_INFO("   |---> [Amd64] CPU Brand: {}", FoundationKitPlatform::Amd64::GetBrandString().string);
        FK_LOG_INFO("   |---> [Amd64] Hypervisor: {}", FOUNDATIONKITCXXSTL_BOOL_TO_STR(FoundationKitPlatform::Amd64::IsHypervisor()));
        FK_LOG_INFO("   |---> [Amd64] Hypervisor Vendor: {}", FoundationKitPlatform::Amd64::GetHypervisorVendor().string);
        FK_LOG_INFO("   |---> [Amd64] Hybrid Architecture: {}", FOUNDATIONKITCXXSTL_BOOL_TO_STR(FoundationKitPlatform::Amd64::IsHybrid()));
        FK_LOG_INFO("   |---> [Amd64] Logical Processor: {}", FoundationKitPlatform::Amd64::GetTopology().logical_processors);
        FK_LOG_INFO("   |---> [Amd64] Physical Cores: {}", FoundationKitPlatform::Amd64::GetTopology().physical_cores);
        FK_LOG_INFO("   |---> [Amd64] BSP APIC ID: {}", FoundationKitPlatform::Amd64::GetTopology().apic_id);
        FK_LOG_INFO("   |---> [Amd64] HTT Enabled: {}", FOUNDATIONKITCXXSTL_BOOL_TO_STR(FoundationKitPlatform::Amd64::GetTopology().htt_enabled));
#elif defined(FOUNDATIONKITPLATFORM_ARCH_ARM64)
        FK_LOG_INFO("   |---> [Arm64] MPIDR: {:#x}", FoundationKitPlatform::Arm64::GetTopology().mpidr);
        FK_LOG_INFO("   |---> [Arm64] Aff0: {}", FoundationKitPlatform::Arm64::GetTopology().affinity0);
        FK_LOG_INFO("   |---> [Arm64] Aff1: {}", FoundationKitPlatform::Arm64::GetTopology().affinity1);
        FK_LOG_INFO("   |---> [Arm64] Aff2: {}", FoundationKitPlatform::Arm64::GetTopology().affinity2);
        FK_LOG_INFO("   |---> [Arm64] Aff3: {}", FoundationKitPlatform::Arm64::GetTopology().affinity3);
        FK_LOG_INFO("   |---> [Arm64] Implementer: {:#x}", FoundationKitPlatform::Arm64::GetImplementation().implementer);
        FK_LOG_INFO("   |---> [Arm64] Part Number: {:#x}", FoundationKitPlatform::Arm64::GetImplementation().part_number);
        FK_LOG_INFO("   |---> [Arm64] Variant: {:#x}", FoundationKitPlatform::Arm64::GetImplementation().variant);
        FK_LOG_INFO("   |---> [Arm64] Revision: {:#x}", FoundationKitPlatform::Arm64::GetImplementation().revision);
#endif
        FK_LOG_INFO("FoundationKit (R) - A safe, high-performance kernel C++23 runtime environment and libraries");
        FK_LOG_INFO("This software is licensed under the MIT license.");
        FK_LOG_INFO("Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), "
            "to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,"
            "and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:"
        );
        FK_LOG_INFO("The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.");
        FK_LOG_INFO("THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, "
            "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, "
            "WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE."
        );
    }
}