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

See [DRIVER_BACKENDS.md](DRIVER_BACKENDS.md). The records themselves are in
[DEPENDENCIES.md](DEPENDENCIES.md); `scripts/check_dependencies.py` fails when a
recorded checksum no longer matches the file, when a dependency section is
missing one of the ten fields, or when a tracked binary artefact has no record.

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

The solution now contains the x64 configuration, so this command is usable. It has been
declared and XML-validated, but it has **not** been executed: no MSVC toolchain is
available in the authoring environment, and the first real build is the
`windows-build` CI job (T1-01). Treat the command as unverified until that job is
green.

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

### What is actually set (T1-04, T1-06, T1-07)

| Setting | Legacy application sources | `fancontrol/core/*` and the test projects |
|---|---|---|
| Warning level | `/W4` | `/W4` + `/WX` |
| `/permissive-`, `/EHsc` | inherited defaults | explicit |
| Source charset | `/source-charset:utf-8` | `/utf-8` |
| Runtime library | `/MT` (static) | `/MD` in the test executables, inherited `/MT` in the app |
| Output | `out/$(Platform)/$(Configuration)/` | `out/tests/$(Platform)/$(Configuration)/` |

`/utf-8` is **not** used on the legacy sources. It sets the execution charset as
well as the source charset, which would make the narrow degree-sign literals
(`"%d°C"`, `"Fan: 0x%02x / Switch: %d°C (%s)"`) render as `Â°` in the ANSI UI.
`/source-charset:utf-8` plus a UTF-8 BOM decodes the source deterministically while
leaving the execution charset at the system ANSI code page, which is correct for an
ANSI application. See [DECISIONS.md](DECISIONS.md) ADR-018.

Every source file carries a UTF-8 BOM. During T1 this was found to be a live bug, not
a style choice: `fanstuff.cpp` was the only file containing non-ASCII literals *without*
a BOM, so it was decoded through the ANSI code page and produced `Â°` in the
minimised-window title and the fan-status string on every build.

Pointer-sized API fixes applied for x64 (T1-07): `SetWindowLong`/`GetWindowLong`
→ `SetWindowLongPtr`/`GetWindowLongPtr`, `GWL_USERDATA` → `GWLP_USERDATA`,
`(ULONG)this` → `(LONG_PTR)this`, in all three code trees (`fancontrol/`,
`TPFCIcon/`, `TPFCIcon_noballons/`). The worker thread entry point was changed from
`ULONG` to `LPVOID`, which truncates a pointer on x64, and `CreateThread` was
replaced with `_beginthreadex`: the worker is created and destroyed once per data
cycle, and `CreateThread` neither initialises nor releases the per-thread CRT block, so
each cycle leaked one.

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


## 11. Windows test target

`tests/core_tests.vcxproj` builds the portable core suite under MSVC. It compiles
exactly the same translation units as `tests/run_core_tests.sh`, so the two paths
cannot drift:

| Source | Role |
|---|---|
| `tests/core_tests.cpp` | the 20 test cases, `main()` |
| `tests/fake_backend.cpp` | in-memory `io_backend`, no hardware |
| `fancontrol/core/*.cpp` | the portable core |

It is a `Console` project linked with `/MD`, so it shares no CRT with the
application and needs no driver, no device, and no administrator rights. It is
built at `/W4 /WX` in all four configurations. It is a member of
`fancontrol/fancontrol.sln` but deliberately has **no** project dependency on the
application, so running the tests never forces a build of the executable.

```powershell
msbuild tests\core_tests.vcxproj /p:Configuration=Release /p:Platform=x64
.\out\tests\x64\Release\core_tests.exe
```

The suite never touches EC registers. It uses a fake backend, so it is safe to run on
any machine, including one with no ThinkPad hardware.

## 12. Continuous integration

`.github/workflows/ci.yml` defines five jobs. None of them load a kernel driver or
write to an EC register.

| Job | Runs on | Covers |
|---|---|---|
| `portable-tests` | ubuntu, macos, windows | `tests/run_core_tests.sh`; plus ASan + UBSan on the non-Windows legs |
| `windows-build` | windows | `Debug`/`Release` x `Win32`/`x64`; runs `core_tests.exe`; archives the build log |
| `release-warnings-as-errors` | windows | Release x64 core build with warnings-as-errors, the suite, and a step that asserts the flags actually reached every core unit |
| `hygiene` | ubuntu | `git diff --check`, Markdown link check, no tracked build artifacts, all text files valid UTF-8 |
| `static-analysis` | windows | MSVC `/analyze` on the core (enforced) and the application (reported until T1-05 closes) |

The `hygiene` job runs `scripts/check_links.py`, which resolves every relative
Markdown link and in-document anchor against the tracked files. External `http(s)`
and `mailto` links are counted but never fetched, so the check stays offline and
hermetic. The script has been negative-tested: seeded broken file links, broken
anchors, and absolute filesystem paths are all reported and it exits non-zero.

Because the core is compiled with `/WX`, any new warning in `fancontrol/core/*` fails
the build. The legacy sources are `/W4` but warning-only until their baseline is
clean — that is task T1-05, which needs the `windows-build` log to enumerate the
actual list.
