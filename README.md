# FoundationKit

FoundationKit is a modular C++23 systems framework designed specifically for operating system development. It provides the essential building blocks for kernel-space environments, ranging from a modern STL implementation to low-level memory primitives and ABI support.

> Use this in your kernel/OS!

## Repository Structure

The project is organized as a monorepo. Each component resides in its own directory and follows a standardized internal structure:

* **`Include/[Component]/`**: Public headers (primarily header-only templates).
* **`Src/`**: Implementation files.
* **`CMakeLists.txt`**: Defines the specific library target and source declarations.

### Components

| Component                 | Description                                                 | Documentation                       |
|:--------------------------|:------------------------------------------------------------|:------------------------------------|
| **FoundationKitCxxStl**   | A kernel-ready, C++23 compliant Standard Template Library.  | `Documentation/FoundationKitCxxStl` |
| **FoundationKitMemory**   | Core memory management primitives and allocators.           | `Documentation/FoundationKitMemory` |
| **FoundationKitOsl**      | Operating System Layer for fundamental system abstractions. | `Documentation/FoundationKitOsl`    |
| **FoundationKitCxxAbi**   | Low-level C++ ABI support.                                  | `Documentation/FoundationKitCxxAbi` |
| **FoundationKitPlatform** | Low-level architecture specific helpers                     | `WIP!`                              |

## Build System

The project uses CMake. The architecture is designed for seamless integration into existing (cmake) OSDev toolchains:

* **Root CMakeLists.txt**: Handles the global project configuration and builds the test suite executable.
* **Component CMakeLists.txt**: Each directory contains its own logic, often declaring `INTERFACE` targets to facilitate header-only usage while managing necessary source files for non-template logic.

## Testing

Unit tests for all components are located in the `/Test` directory. These ensure the reliability of the STL and memory primitives before deployment into a kernel environment.

---

## License

This project is licensed under the MIT license. For more information see the `LICENSE` file.