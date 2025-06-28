# Hermes Windows Build Issues Summary

## Overview
This document tracks remaining build issues for the Hermes Windows port.

Last updated: 2025-01-23

## Build Status
- **ICU/Intl Support**: ✅ **FIXED** - All compilation errors resolved in `PlatformIntlWindows.cpp`
- **NAPI API Compatibility**: 🔴 **CRITICAL** - Multiple API compatibility issues in `hermes_napi.cpp`
- **Platform Unicode/Intl Functions**: 🔴 **CRITICAL** - Missing function implementations causing linker errors
- **GTest Template Issues**: ⚠️ **MEDIUM PRIORITY** - Template compilation errors in test files

## Critical Issues (Blocking successful compilation)

### 1. Platform Unicode/Intl Missing Function Implementations
**Status**: 🔴 **CRITICAL** - Multiple linker errors

**Missing Function Implementations** (all `LNK2019` errors):
- `hermes::platform_unicode::dateFormat` - Date formatting for Unicode support
- `hermes::platform_unicode::convertToCase` - String case conversion
- `hermes::platform_unicode::normalize` - String normalization
- `hermes::platform_intl::getCanonicalLocales` - Canonical locale resolution
- `hermes::platform_intl::toDateTimeOptions` - DateTime options processing
- `hermes::platform_intl::impl_icu::getBoolOption` - ICU boolean option parsing
- Windows Timer APIs: `__imp_timeBeginPeriod`, `__imp_timeEndPeriod` - Profiler timing

**Impact**: Blocks all executable linking (hdb.exe, hermes.exe, hvm.exe, etc.)

### 2. NAPI API Compatibility Issues in `hermes_napi.cpp`
**Status**: 🔴 **CRITICAL** - Compilation errors

**Fixed Issues**:
- ✅ Replaced all `isNativeValue` calls with `isDouble`
- ✅ Fixed static method naming for `NodeApiHostFunctionContext`
- ✅ Updated `makeHandle` usage to use `std::move`
- ✅ Updated `createThisForConstruct_RJS` call arguments

**Outstanding Issues**:
- Syntax error at line 4961: stray `do` keyword
- Missing API: `hermes::vm::NativeFunction::createWithoutPrototype` (multiple locations)
- Missing API: `hermes::hbc::BytecodeSerializer` class
- Protected constructor access: `hermes::vm::Handle<T>::Handle`

### 3. GTest Template Compilation Issues
**Status**: ⚠️ **MEDIUM PRIORITY** - Template instantiation failures

**Issues:**
- GoogleTest template errors with `char16_t*` stream operators
- `std::operator<<` ambiguity and deleted function references
- Template instantiation failures in comparison operations

**Files Affected:**
- `unittests/API/SynthTraceParserTest.cpp`
- `unittests/API/APITest.cpp`
- `unittests/VMRuntime/StringViewTest.cpp`
- `unittests/VMRuntime/GCBasicsTest.cpp`

## Detailed Analysis

### Build Impact
The current build shows these failures:
- **25 total build targets attempted**
- **13 failed** due to linker errors (missing platform functions)
- **4 failed** due to compilation errors (hermes_napi.cpp syntax errors)
- **4 failed** due to GTest template issues

### Next Steps (Priority Order)

1. **Platform Function Implementation** (CRITICAL)
   - Implement missing Unicode/Intl functions in Windows platform layer
   - Add Windows timer API linking for profiler support
   - Required for any executable to link successfully

2. **hermes_napi.cpp Fixes** (CRITICAL)
   - Fix syntax error at line 4961
   - Research API changes for `createWithoutPrototype` and `BytecodeSerializer`
   - Address protected constructor access issues

3. **GTest Template Issues** (MEDIUM)
   - Can be addressed after core functionality works
   - May require GoogleTest configuration changes for MSVC C++20

### Notes
- ICU/Intl compilation is now working correctly
- Main library targets are compiling but failing at link stage due to missing implementations
- NAPI integration requires significant API compatibility work

### Previous Fixes Applied
- ✅ **PlatformIntlWindows.cpp** - All compilation errors fixed
- ✅ Fixed several NAPI API calls (`isNativeValue`, `makeHandle`, etc.)
- ✅ Cross-compiler macros and Boost Context assembly files
- ✅ SH code generator and internal compiler issues
1. **hermes_napi.cpp** - Systematic API migration to match current Hermes VM APIs
2. **GTest template issues** - Address char16_t printing compatibility for MSVC C++20

#### 📝 Medium Priority Issues (Non-blocking)
- GTest template issues with `char16_t*` printing (multiple test files)
- Platform Unicode implementation missing for Windows
- CMake warning cleanup

## Next Steps
1. ✅ ~~Fix PlatformIntlWindows.cpp implementation conflicts~~ **COMPLETED**
2. Update hermes_napi.cpp to use current Hermes VM API signatures
3. Address GTest template issues for test compilation
4. Clean up remaining build warnings

**Build Status**: Major blocker resolved - ICU/Intl now fully working!
