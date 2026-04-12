#pragma once

#define FOUNDATIONKIT_VERSION "2026h1"

#include <FoundationKitCxxStl/Base/Compiler.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>

namespace FoundationKitCxxStl {
    FOUNDATIONKITCXXSTL_ALWAYS_INLINE void PrintFoundationKitInfo() {
        FK_LOG_INFO("FoundationKit® {} ({} {} {}) - copyright© 2026 assembler-0", FOUNDATIONKIT_VERSION,
            __DATE__, __TIME__, FOUNDATIONKIT_COMPILER);
    }
}