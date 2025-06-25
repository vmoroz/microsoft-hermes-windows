# Remaining Build Issues for Hermes Windows Port

## High Priority Issues

### 1. Internal Compiler Error (CRITICAL)
- **File**: `internal_unit.c` 
- **Error**: `fatal error C1001: Internal compiler error`
- **Location**: `e:\GitHub\microsoft\hermes-windows\out\build\win32-x64-release\lib\InternalJavaScript\internal_unit.c`
- **Status**: BLOCKING - prevents compilation
- **Cause**: Generated C code from SH bytecode generator may have syntax issues or MSVC-incompatible constructs
- **Next Steps**: 
  - Examine generated `internal_unit.c` for problematic constructs
  - May need to regenerate or fix SH.cpp code generator
  - Consider reducing complexity or splitting large generated functions

### 2. Missing ICU Headers
- **Files**: `unicode/dtptngen.h`, `unicode/timezone.h`
- **Error**: `fatal error C1083: Cannot open include file`
- **Location**: `lib\Platform\Intl\PlatformIntlICU.cpp`
- **Status**: NEEDS STUBBING (per user request)
- **Cause**: Limited ICU installation in `external\icu_decls` doesn't include these headers
- **Next Steps**: 
  - User will handle stubbing approach differently
  - May need to create stub implementations or disable Intl features

### 3. Platform Unicode Linker Errors
- **Functions**: `convertToCase`, `normalize`
- **Error**: `error LNK2019: unresolved external symbol`
- **Location**: `PlatformUnicodeTests.exe`
- **Status**: MISSING IMPLEMENTATION
- **Cause**: These functions are declared but not implemented for Windows
- **Next Steps**:
  - Find platform-specific Unicode implementation
  - May need Windows-specific ICU or system API calls
  - Could be in `lib\Platform\Unicode\` directory

## Medium Priority Issues

### 4. GTest Template Issues
- **Files**: `BCP47ParserTest.cpp`, `GCBasicsTest.cpp`
- **Error**: `operator<<` template conflicts with `char16_t*`
- **Status**: MSVC/GTest COMPATIBILITY
- **Cause**: MSVC C++20 has stricter template resolution for `char16_t*` printing
- **Next Steps**:
  - Add custom GTest printer for `char16_t*`
  - Or modify test code to avoid direct `char16_t*` comparisons
  - Consider using string conversion wrappers

### 5. Hermes NAPI Missing APIs
- **File**: `API\hermes_shared\hermes_napi.cpp`
- **Missing Types/Methods**:
  - `BigStorage` type
  - `createWithoutPrototype` method
  - `registerLazyIdentifier` signature mismatch
  - `isNativeValue` method
  - `BytecodeSerializer` type
  - Various template casting issues
- **Status**: API EVOLUTION ISSUES
- **Cause**: Hermes VM API has evolved, NAPI wrapper is outdated
- **Next Steps**:
  - Update NAPI wrapper to match current Hermes VM API
  - May need to find equivalent methods or provide polyfills
  - Review Hermes VM header changes

## Low Priority Issues

### 6. Compiler Warnings
- **Warning**: `overriding '/EHs' with '/EHs-'` (exception handling)
- **Status**: COSMETIC
- **Cause**: CMake configuration conflict
- **Next Steps**: Fix CMake flags to be consistent

### 7. Assembly File Warnings (RESOLVED)
- **Status**: ✅ FIXED - Boost Context MASM files updated

### 8. Cross-compiler Macros (RESOLVED)
- **Status**: ✅ FIXED - `static_h.h` updated with MSVC equivalents

## Build Strategy

### Recommended Order:
1. **Internal Compiler Error** - Must be resolved first as it's blocking
2. **ICU Headers** - User will handle stubbing differently
3. **Platform Unicode** - Find/implement missing functions
4. **GTest Issues** - Fix template compilation errors
5. **NAPI APIs** - Update to match current Hermes VM API
6. **Warnings** - Clean up CMake configuration

### Tools/Approaches:
- Use `grep_search` and `file_search` to locate implementations
- Use `read_file` to examine generated code issues
- Use conditional compilation (`#ifdef`) for platform differences
- Consider creating stub implementations where full ICU is unavailable

## Notes

- User specifically requested to stop ICU/Intl changes and handle stubbing differently
- Focus should be on the internal compiler error as it's completely blocking
- Many issues stem from cross-platform compatibility (GCC/Clang → MSVC)
- Generated code (SH bytecode) may need special handling for MSVC

## Progress Tracking

- [x] Cross-compiler macros (static_h.h)
- [x] Boost Context assembly files
- [x] SH code generator array forward declarations
- [x] Missing inspector headers
- [x] JSI protected member access
- [ ] Internal compiler error (CRITICAL)
- [ ] ICU header stubbing (USER HANDLING)
- [ ] Platform Unicode implementation
- [ ] GTest template fixes
- [ ] NAPI API updates
