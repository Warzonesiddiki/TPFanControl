# Build and development environment

## 1. Supported development environment

Recommended validation environment (the actual target Windows version is still unknown):

- Windows 10 or Windows 11 x64, with the exact edition and build recorded in the hardware report
- Visual Studio 2022 Community or later
- Desktop development with C++ workload
- Windows 10/11 SDK
- v143 toolset
- C++17 or later as selected by the solution

The legacy repository currently contains old Visual Studio project files and generated build artifacts. A successful build must come from source, not from checked-in object files or precompiled binaries.

## 2. Dependency policy

Do not commit kernel drivers, vendor DLLs, private SDKs, signing keys, or user-specific configuration unless redistribution rights are documented.

For each external dependency record:

- name;
- exact version;
- download URL;
- license;
- architecture;
- signature status;
- checksum;
- install/uninstall method;
- whether HVCI/Secure Boot were tested.

See [DRIVER_BACKENDS.md](DRIVER_BACKENDS.md).

## 3. Clean checkout preparation

From a Developer PowerShell for Visual Studio:

```powershell
git status --short

# Do not build from stale output directories.
Remove-Item -Recurse -Force .\out -ErrorAction SilentlyContinue
New-Item -ItemType Directory .\out | Out-Null
```

Do not delete the source repository or `.git` directory. Generated output should go under an ignored output directory in future project-file revisions.

## 4. Current legacy build

Until the solution is migrated, use the existing configuration only for baseline comparison. Run the solution from Visual Studio and record:

- compiler version;
- selected platform/configuration;
- warnings;
- linker errors;
- runtime backend initialization result.

A baseline build that uses old TVicPort is not proof that the modern target is supported.

## 5. Target build configurations

The modernization should add and validate:

```text
Debug|Win32
Release|Win32
Debug|x64
Release|x64
```

The x64 configuration is the primary build. Win32 remains only while the selected backend and tests prove that it is supported. The target Windows edition and build must be recorded before release.

Example future command:

```powershell
msbuild .\fancontrol\fancontrol.sln `
  /m `
  /t:Build `
  /p:Configuration=Release `
  /p:Platform=x64 `
  /p:OutDir="$pwd\out\Release-x64\"
```

Do not document this command as usable until the solution contains the x64 configuration.

## 6. Compiler requirements

The target project should enable, as appropriate:

- `/W4`
- `/permissive-`
- `/EHsc`
- warning-as-error for new code after the baseline is clean;
- UTF-8 source handling;
- explicit runtime library choice;
- reproducible output paths;
- no machine-specific absolute paths.

Fix pointer-sized Windows APIs when enabling x64:

```cpp
SetWindowLongPtr
GetWindowLongPtr
GWLP_USERDATA
LONG_PTR
```

Do not replace a pointer cast with another integer cast of a different width.

## 7. Output layout

The intended output layout is:

```text
out/
  Debug-x64/
    TPFanControl.exe
    TPFanControl.ini
  Release-x64/
    TPFanControl.exe
    TPFanControl.ini
  diagnostics/
    ecdiag.exe
```

Logs and user-edited profiles should not be written into the source tree. The application should use a documented per-user or installation data directory.

## 8. Driver/backend verification after build

A build is not hardware-ready until the following are checked:

- executable architecture;
- imported backend libraries;
- driver/device presence;
- code-integrity events;
- administrator requirement;
- monitor-only behavior when the backend is absent;
- clean shutdown.

Use [TESTING.md](TESTING.md) for the evidence format.

## 9. Local validation checklist

- [ ] Clean checkout builds from source.
- [ ] No absolute paths from the developer machine remain.
- [ ] No generated artifacts are required for linking.
- [ ] No private SDK or driver is committed.
- [ ] Debug build logs backend errors clearly.
- [ ] Release build does not enable unsafe expert options by default.
- [ ] `git diff --check` passes.
- [ ] Tests pass without a physical device.
