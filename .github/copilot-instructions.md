# Copilot Instructions for Hermes Windows Port

## Build Commands

### Primary Build Command
```bash
node .\.ado\scripts\build.js
```
This is the main build command to use for building and verifying the Hermes Windows port.

### Alternative CMake Build Commands
```bash
# Build all targets
cmake --build build --config Debug --target ALL_BUILD -j 34

# Build specific target
cmake --build build --config Debug --target <target_name> -j 4
```

## Project Context

This is a port of the Hermes JavaScript engine to Windows using MSVC. The project involves:
- Cross-compiler compatibility fixes (GCC/Clang → MSVC)
- CMake build system updates
- C++ standard compliance (C++17)
- Assembly code compatibility (MASM)
- Library linking and naming issues

## Key Files and Areas

### Critical Headers
- `include/hermes/VM/static_h.h` - Cross-compiler macros and built-ins
- `include/hermes/VM/sh_legacy_value.h` - Value encoding (C/C++ compatibility)
- `API/jsi/jsi/jsi.h` - JavaScript Interface (JSI) definitions

### Build Configuration
- `CMakeLists.txt` - Main CMake configuration
- `lib/CMakeLists.txt` - Library configuration
- `external/boost/boost_1_86_0/libs/context/CMakeLists.txt` - Boost Context library

### Assembly Files
- `external/boost/boost_1_86_0/libs/context/src/asm/*_x86_64_ms_pe_masm.asm` - MASM assembly files

## Common Issues and Solutions

### MSVC Compatibility
- Use `_MSC_VER` macros for MSVC-specific code
- Replace GCC built-ins with MSVC equivalents
- Handle zero-sized arrays (use flexible array member or conditional compilation)
- Use literal constants instead of expressions in C context

### C vs C++ Compilation
- Some files compile as C, requiring constant expressions
- Use `#ifdef __cplusplus` when needed
- Avoid C++-specific features in headers included by C files

### Library Naming
- MSVC auto-generates library names in format `lib<name>-<compiler>-<config>.lib`
- Use `OUTPUT_NAME` property to control library naming
- Ensure consistent library references across targets

## Recent Fixes Applied

1. **Cross-compiler macros** in `static_h.h` for `__builtin_*` functions
2. **JSI protected member access** - Added friend declaration for `castInterface` template
3. **Boost Context assembly** - Fixed MASM syntax and macro definitions
4. **Constant expressions** in `sh_legacy_value.h` - Replaced calculations with literals for C compatibility
5. **Library linking** - Fixed Boost Context library naming and linking issues
6. **Zero-sized arrays** in `static_h.h` - Replaced `locals[0]` with conditional compilation for MSVC compatibility
7. **__builtin_frame_address** - Replaced with `_AddressOfReturnAddress()` for MSVC
8. **Boost is_pod warnings** - Added `_SILENCE_CXX20_IS_POD_DEPRECATION_WARNING` to suppress deprecation warnings
9. **Missing Boost version header** - Created `hoost/version.hpp` with correct version macros
10. **dlfcn.h platform issues** - Added Windows-specific dynamic loading code
11. **C++20 string::reserve** - Fixed by passing explicit argument to `reserve()`
12. **Generated C code arrays** - Fixed zero-sized and unknown-size array issues in SH bytecode generator

## Development Workflow

1. Make changes to source files
2. Run `node .\.ado\scripts\build.js` to build and test
3. Address compilation errors systematically
4. Focus on cross-compiler compatibility
5. Test both C and C++ compilation contexts
6. Verify library linking and dependencies

## Notes

- The project uses a mix of C and C++ code
- MSVC is more strict about some language features than GCC/Clang
- Pay attention to protected/private member access in template functions
- Assembly code requires MASM-specific syntax adjustments
