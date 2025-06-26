# Remaining Build Issues for Hermes Windows Port

## Summary
This document tracks the remaining compilation issues that need to be resolved for the Hermes Windows port.

## Current Status
Last updated: December 2024

### Critical Issues

#### 1. PlatformIntlWindows.cpp Compilation Errors
**File:** `lib/Platform/Intl/PlatformIntlWindows.cpp`
**Errors:**
- Line 209: `lookupSupportedLocales` identifier not found
- Line 214: Duplicate function definition of `getCanonicalLocales` 
- Line 342: Error in function definition/declaration for `getCanonicalLocales`
- Line 345: `requestedLocales` cannot be used before initialization
- Line 394: `getOptionBool` identifier not found

**Details:** The PlatformIntlWindows.cpp appears to have missing function implementations and conflicts with PlatformIntlShared.cpp

**Priority:** High - Blocks compilation

#### 2. hermes_napi.cpp API Compatibility Issues
**File:** `API/hermes_shared/hermes_napi.cpp`
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

### Progress Summary

#### ✅ Resolved Issues
- Cross-compiler macros (static_h.h)
- Boost Context assembly files
- SH code generator array forward declarations
- Missing inspector headers
- JSI protected member access
- Internal compiler error (was fixed)
- ICU basic compilation issues

#### 🔄 Current Focus Areas
1. **PlatformIntlWindows.cpp** - Need to resolve function conflicts and missing implementations
2. **hermes_napi.cpp** - Systematic API migration to match current Hermes VM APIs

#### 📝 Remaining Medium Priority Issues
- GTest template issues with `char16_t*` printing (multiple test files)
- Platform Unicode implementation missing for Windows
- CMake warning cleanup

## Next Steps
1. Fix PlatformIntlWindows.cpp implementation conflicts
2. Update hermes_napi.cpp to use current Hermes VM API signatures
3. Address remaining test compilation issues
4. Clean up build warnings
