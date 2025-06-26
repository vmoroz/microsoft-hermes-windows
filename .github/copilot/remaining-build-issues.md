# Remaining Build Issues for Hermes Windows Port

## Summary
This document tracks the remaining compilation issues that need to be resolved for the Hermes Windows port.

## Current Status
Last updated: December 25, 2024

🎉 **Major Progress**: Fixed all `PlatformIntlWindows.cpp` compilation errors! ICU/Intl implementation now compiles successfully.

### Critical Issues (2 Remaining)

#### 1. hermes_napi.cpp API Compatibility Issues
**File:** `API/hermes_shared/hermes_napi.cpp`
**Status:** Critical - Multiple API compatibility errors
**Multiple API compatibility errors:**

**Function Definition Issues:**
- Line 2067: `func` is not a member of `NodeApiHostFunctionContext`
- Lines 2073, 2080: Cannot access private members `env_` and `hostCallback_`

**Hermes VM API Changes:**
- Line 4680: `makeHandle` overload resolution failure with `PseudoHandle<PropertyAccessor>`
- Line 4947: `createThisForConstruct_RJS` expects 3 arguments, called with 2
- Line 4946: Cannot convert `CallResult<PseudoHandle<HermesValue>>` to `CallResult<PseudoHandle<JSObject>>`
- Line 5284: `NativeConstructor::create` called with wrong number of arguments
- Line 5308: `defineNameLengthAndPrototype` called with wrong number of arguments
- Lines 5759, 5784: `isNativeValue` is not a member of `PinnedHermesValue`
- Lines 6283, 6340, 6358: `createWithoutPrototype` is not a member of `NativeFunction`
- Line 6663: `BytecodeSerializer` is not a member of `hermes::hbc`
- Line 6739: Cannot access protected member `Handle<T>::Handle`

**Priority:** High - Multiple API breaking changes need systematic resolution

#### 2. GTest Template Compilation Issues
**Files:** Multiple test files
**Status:** Critical - Template instantiation failures

**Issues:**
- GoogleTest template errors with `char16_t*` stream operators
- `std::operator<<` ambiguity and deleted function references
- Template instantiation failures in comparison operations

**Files Affected:**
- `unittests/API/SynthTraceParserTest.cpp`
- `unittests/API/APITest.cpp`
- `unittests/VMRuntime/StringViewTest.cpp`
- `unittests/VMRuntime/GCBasicsTest.cpp`

**Root Cause:** MSVC C++20 standard library changes affecting GoogleTest's char16_t handling

**Priority:** Medium - Test compilation (non-blocking for main library)

### Progress Summary

#### ✅ Resolved Issues
- ✅ **PlatformIntlWindows.cpp** - All compilation errors fixed!
  - Fixed incorrect include of `PlatformIntlShared.cpp` (changed to `.h`)
  - Removed duplicate `getCanonicalLocales` function definition
  - Implemented missing `lookupSupportedLocales` function
  - Fixed `getOptionBool` to use `impl_icu::getBoolOption`
  - Added proper ICU headers and utilities
- Cross-compiler macros (static_h.h)
- Boost Context assembly files
- SH code generator array forward declarations
- Missing inspector headers
- JSI protected member access
- Internal compiler error (was fixed)
- ICU basic compilation issues

#### 🔄 Current Focus Areas
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
