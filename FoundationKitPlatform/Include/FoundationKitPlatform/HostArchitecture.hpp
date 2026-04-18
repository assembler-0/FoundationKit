#pragma once

#if defined(__x86_64__) || defined(_M_X64)
#  define FOUNDATIONKITPLATFORM_ARCH_X86_64 1
#  define FOUNDATIONKITPLATFORM_ARCH_NAME "Amd64"
#elif defined(__aarch64__) || defined(_M_ARM64)
#  define FOUNDATIONKITPLATFORM_ARCH_ARM64 1
#  define FOUNDATIONKITPLATFORM_ARCH_NAME "Arm64"
#elif defined(__riscv) && (__riscv_xlen == 64)
#  define FOUNDATIONKITPLATFORM_ARCH_RISCV64 1
#  define FOUNDATIONKITPLATFORM_ARCH_NAME "Riscv64"
#else
#  error "FoundationKitPlatform: unsupported target architecture."
#endif