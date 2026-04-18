#pragma once

#if !defined(FOUNDATIONKITPLATFORM_MACHINE_WITDH_32) && !defined(FOUNDATIONKITPLATFORM_MACHINE_WITDH_64)

#  if defined(__LP64__) || defined(_WIN64) || defined(__x86_64__) || \
defined(__aarch64__) || defined(__riscv) && (__riscv_xlen == 64)
#    define FOUNDATIONKITPLATFORM_MACHINE_WITDH_64 1
#    define FOUNDATIONKITPLATFORM_MACHINE_WITDH 64
#  else
#    define FOUNDATIONKITPLATFORM_MACHINE_WITDH_32 1
#    define FOUNDATIONKITPLATFORM_MACHINE_WITDH 32
#  endif

#endif