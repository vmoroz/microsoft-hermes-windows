# Windows Fork Maintenance Strategy

## Background

This repository is a fork of [facebook/hermes](https://github.com/facebook/hermes), the JavaScript engine for React Native. The original Hermes repository primarily targets non-Windows platforms (iOS, Android, macOS, Linux). This Microsoft fork exists to enable and maintain Windows platform support with security compliance requirements.

## Challenge

Maintaining a fork presents ongoing challenges when syncing with upstream changes:

- **Merge Conflicts**: Windows-specific changes scattered throughout the codebase cause conflicts when merging upstream updates
- **Code Review Overhead**: Difficult to distinguish between upstream changes and Windows-specific modifications
- **Maintenance Burden**: Every upstream merge requires careful review and re-application of Windows-specific patches
- **Knowledge Transfer**: New team members must learn which modifications are Windows-specific vs. upstream

## Strategy: Consolidation in HermesWindows.cmake

To reduce the pain of maintaining this fork, we're consolidating all Windows-specific build configuration into a single file: [`cmake/modules/HermesWindows.cmake`](../cmake/modules/HermesWindows.cmake).

### Goals

1. **Minimize Upstream File Changes**: Keep modifications to upstream files (CMakeLists.txt, Hermes.cmake, etc.) to an absolute minimum
2. **Centralize Windows Logic**: Move all Windows-specific compiler flags, linker flags, and build settings to HermesWindows.cmake
3. **Simplify Merges**: Reduce merge conflicts by isolating Windows-specific changes
4. **Improve Maintainability**: Make it clear what's Windows-specific vs. upstream behavior

### Implementation Approach

#### What Goes in HermesWindows.cmake

- Windows compiler flags (MSVC and Clang on Windows)
- Windows linker flags (link.exe and lld-link)
- Windows security requirements (/sdl, /guard:cf, /Qspectre, etc.)
- Windows-specific build options
- CRT (C Runtime) configuration (Hybrid CRT for deployment)
- Windows platform detection and configuration

#### Overriding Upstream Warning Suppressions

A key challenge is that upstream facebook/hermes disables certain warnings (C4244, C4267) that Microsoft's security requirements mandate must be enabled. The solution uses MSVC's flag precedence where **the last flag wins**:

**Execution order:**
1. `Hermes.cmake` (upstream) runs first: `-wd4244 -wd4267` (disables warnings)
2. `HermesWindows.cmake` runs later: `/w34244 /w34267` (re-enables at warning level 3)
3. MSVC uses the last flag → warnings are enabled ✅

This allows us to maintain security compliance without modifying upstream files.

**Example flags that are overridden:**
- `C4146`: Downgraded from error to warning (promoted by `/sdl`)
- `C4244`: Re-enabled despite upstream suppression (SDL requirement)
- `C4267`: Re-enabled despite upstream suppression (SDL requirement)

#### What Stays in Upstream Files

- Minimal hooks to include HermesWindows.cmake
- Cross-platform logic that already exists upstream
- Changes that should be contributed back to facebook/hermes

### Current Implementation

The Windows-specific configuration is invoked in CMakeLists.txt:

```cmake
# Configure Hybrid CRT for Windows builds (must be before any targets)
if(WIN32)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Include Windows-specific build configuration
if(WIN32)
  include(HermesWindows)
endif()

# ... later in the file ...

# Configure Windows-specific build settings
if(WIN32)
  hermes_windows_configure_build()
  hermes_windows_show_configuration()
endif()
```

The `HermesWindows.cmake` module provides:

- `hermes_windows_configure_clang_flags()` - Clang compiler flags for Windows
- `hermes_windows_configure_msvc_flags()` - MSVC compiler flags
- `hermes_windows_configure_lld_flags()` - lld-link (Clang's linker) flags
- `hermes_windows_configure_msvc_linker_flags()` - link.exe (MSVC linker) flags
- `hermes_windows_configure_build()` - Main configuration function
- `hermes_windows_show_configuration()` - Debug output for verification

## Cleanup Progress

### Completed

- ✅ **Hybrid CRT Configuration**: Moved static C++ runtime + dynamic UCRT configuration to eliminate MSVCP140.dll dependencies
- ✅ **Security Flags**: Centralized /sdl, /guard:cf, /Qspectre, /ZH:SHA_256, /CETCOMPAT in HermesWindows.cmake
- ✅ **Warning C4146**: Consolidated handling of "unary minus on unsigned" warning (required after /sdl)
- ✅ **SDL Warning Requirements**: Override upstream suppressions to re-enable C4244 and C4267 (security compliance)
- ✅ **Linker Flags**: Separated debug vs. release linker flags with proper generator expressions
- ✅ **DLL Warning Suppressions**: Moved C4251, C4275, C4646, C4312 from CMakeLists.txt to HermesWindows.cmake

### In Progress / TODO

Items to clean up from upstream files and move to HermesWindows.cmake:

- [ ] Additional MSVC warning suppressions in CMakeLists.txt (C4068, C4200, C4201, C4530) - These appear to be from upstream
- [ ] Stack size configuration (/STACK:10485760) - Windows-specific for deep recursion tests
- [ ] MSVC_MP parallelization flag - Windows-specific build optimization
- [ ] Any other Windows-specific patches discovered during upstream merges

## How to Contribute Cleanup

When you identify Windows-specific code in upstream files that should be moved:

1. **Identify the Code**: Find Windows-specific modifications in CMakeLists.txt, Hermes.cmake, or other upstream files
2. **Move to HermesWindows.cmake**: Add the configuration to the appropriate function in HermesWindows.cmake
3. **Remove from Upstream File**: Delete or comment out the Windows-specific code, adding a note pointing to HermesWindows.cmake
4. **Test All Configurations**: Verify ninja-clang-debug, ninja-clang-release, ninja-msvc-debug, ninja-msvc-release all build correctly
5. **Document**: Update this file's "Cleanup Progress" section

## Testing Windows Builds

After making changes, verify all four build configurations:

```powershell
# Configure and build Clang Debug
cmake --preset ninja-clang-debug
cmake --build build/ninja-clang-debug --target libshared

# Configure and build Clang Release  
cmake --preset ninja-clang-release
cmake --build build/ninja-clang-release --target libshared

# Configure and build MSVC Debug
cmake --preset ninja-msvc-debug
cmake --build build/ninja-msvc-debug --target libshared

# Configure and build MSVC Release
cmake --preset ninja-msvc-release
cmake --build build/ninja-msvc-release --target libshared
```

Verify DLL dependencies (should only have UCRT, no MSVCP/VCRUNTIME):

```powershell
# Debug builds should depend on ucrtbased.dll
dumpbin /dependents build/ninja-msvc-debug/API/hermes_shared/hermes.dll

# Release builds should depend on api-ms-win-crt-*.dll
dumpbin /dependents build/ninja-msvc-release/API/hermes_shared/hermes.dll
```

## Resources

- **Upstream Hermes**: https://github.com/facebook/hermes
- **Windows Hybrid CRT**: https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/downloads#runtime-downloads
- **MSVC Security Features**: https://learn.microsoft.com/en-us/cpp/build/reference/sdl-enable-additional-security-checks

## Contact

For questions about Windows fork maintenance, consult the team members who work on the hermes-windows repository.
