# FoundationKit

FoundationKit is a modular C++23 systems framework designed specifically for operating system development. It provides the essential building blocks for kernel-space environments, ranging from a modern STL implementation to low-level memory primitives and ABI support.

> Use this in your kernel/OS!

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/assembler-0/FoundationKit)

## Components

| Component                 | Description                                                 |
|:--------------------------|:------------------------------------------------------------|
| **FoundationKitCxxStl**   | A kernel-ready, C++23 compliant Standard Template Library.  |
| **FoundationKitMemory**   | Core memory management primitives and allocators.           |
| **FoundationKitOsl**      | Operating System Layer for fundamental system abstractions. |
| **FoundationKitCxxAbi**   | Low-level C++ ABI support.                                  |
| **FoundationKitPlatform** | Low-level architecture specific helpers                     |
| **FoundationKitDevice**   | Device model and driver framework                           |

## Testing

Unit tests for all components are located in the `/Test` directory. These ensure the reliability of the STL and memory primitives before deployment into a kernel environment.

---

## License

This project is licensed under the MIT license. For more information see the `LICENSE` file.