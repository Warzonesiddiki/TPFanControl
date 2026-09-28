# Dependency record

## 1. Why this file exists

The project depends on a kernel component it does not write, and the legacy
build links a vendor library that it does not own. Both are recorded here, field
by field, before they are relied on.

The policy this file implements:

- [SECURITY.md](SECURITY.md) §3 — before adopting a backend, document its source,
  license, exact release, driver architecture, signature chain, HVCI/Secure Boot
  behaviour, exposed device/API, uninstall method, known vulnerabilities and
  redistribution rights.
- [BUILD.md](BUILD.md) §2 — record name, exact version, download URL, license,
  architecture, signature status, checksum, install/uninstall, and whether
  HVCI/Secure Boot were tested.
- [DRIVER_BACKENDS.md](DRIVER_BACKENDS.md) §8 — do not commit proprietary driver
  binaries unless redistribution rights are documented; prefer a user-installed
  official driver over silently bundling a kernel component.
- [`LICENSE`](../LICENSE) — the two bundled third-party items are excepted from
  the public-domain dedication and are re-checked before redistribution.

`scripts/check_dependencies.py` keeps this record true: it verifies the hashes
and sizes in §4.2 against the files on disk, fails if a tracked binary artefact
is not in the record, and fails if a dependency section is missing one of the ten
fields. Like every other check here, it has a self-test.

## 2. How to read a field

| Label | Meaning |
|---|---|
| **Verified here** | Determined from an artefact in this repository, in this environment, on the date given. Reproducible by anyone with the same checkout. |
| **Vendor statement** | The vendor's own published words, with the URL and the date they were read. True of the vendor's *current* package, not necessarily of the file in this tree. |
| **Third-party report** | A vulnerability catalogue or research publication. Cited, not reproduced, and **not** tested here. |
| **Not established** | Nobody has evidence yet. The task that must produce it is named. A field in this state is not "zero" or "no" — it is unknown. |

Nothing in this file is hardware evidence. No entry here may be quoted as proof
that a driver loads, that a machine was tested, or that a version matches.

## 3. Register

| Dependency | Role | State in this repository | Verified on hardware |
|---|---|---|---|
| TVicPort (EnTech Taiwan) | The legacy baseline's port I/O backend | One 32-bit import library and its header are tracked; **no driver binary is tracked** | No — Phase 0 open |
| PawnIO (namazso) | Candidate modern backend, [DRIVER_BACKENDS.md](DRIVER_BACKENDS.md) §4 | None. Not adopted, not downloaded, no header or library in the tree | No — T5-02 blocked |
| Portable core and `ecdiag` | The code this project owns | No third-party libraries; C++17 standard library only | N/A |

## 4. TVicPort

The legacy application calls `OpenTVicPort`, `ReadPort` and `WritePort`
(`fancontrol/approot.cpp`, `fancontrol/fanstuff.cpp`, `fancontrol/portio.cpp`).
The modern core does not: it reaches the ports through `core/io_backend.h`, and
the TVicPort adapter is T5-01.

### 4.1 The ten fields

| Field | Record |
|---|---|
| Source or vendor | **Verified here:** the vendored header says EnTech Taiwan, `http://www.entechtaiwan.com`, copyright 1997–2005. **Vendor statement:** the product page is EnTech Taiwan's. The repository's `LICENSE` previously credited the library to an individual; neither the artefact nor the vendor's site corroborates that, and the attribution was corrected in the same change that added this file. |
| License | **Vendor statement** (read 2026-09-28): "TVicPort is now FREE for personal (non-commercial) use!" The package is "fully functional so you can write and debug your application before you buy the commercial license to obtain DLL source code and the right to redistribute the driver with your application." **Consequence:** commercial use, redistribution, and driver source all require the commercial licence. |
| Exact release | **Verified here:** the vendored library carries no version string; the header it ships with states "Version 4.0"; every archive member is timestamped 2005-04-19. **Vendor statement:** the freeware package download is dated 2005-12-09; the product page was last modified 2008-12-20. **Do not conflate** this with the 5.2.1.0 file version in §4.5 — that is a later 64-bit driver, not a file in this repository. |
| Driver architecture | **Not established for the driver**: no driver binary is tracked, and none has been examined. **Verified here for the import library:** all 51 COFF members are `pe-i386` (32-bit), and the version of the vendor DLL it binds to is therefore 32-bit. **Vendor statement:** support is listed for Windows 95/98/Me and NT/2000/XP/XP 64-bit. |
| Signature chain | **Not established.** A third-party catalogue describes a 2006-dated AMD64 sample (file version 5.2.1.0) as signed, publisher EnTech Taiwan; that is not a statement about the file any user would install today, and nothing about signing has been checked on this machine. T5-03 records the result where it must be recorded. |
| HVCI / Secure Boot | **Not tested.** Blocked behind T5-02, measured for the baseline by T4-06. This field is not permitted to be worked around: [SAFETY.md](SAFETY.md) and [DRIVER_BACKENDS.md](DRIVER_BACKENDS.md) §5 forbid asking a user to disable Secure Boot, HVCI, SBE or the vulnerable-driver blocklist to make a backend load. |
| Exposed device or API | **Verified here (user-mode API):** the vendored import library exposes 47 distinct `__stdcall` names, including `OpenTVicPort`, `CloseTVicPort`, `IsDriverOpened`, `TestHardAccess`, `SetHardAccess`, `ReadPort`, `WritePort`, and — the ones that matter for security review — `MapPhysToLinear`, `GetMem`, `GetMemL`, `GetMemW`, `SetMem`, `SetMemL`, `SetMemW`, `UnmapMemory`. **Third-party report:** analysis of `TVicPort64.sys` 5.2.1.0 describes device `\\.\TVicPortDevice0` with no DACL, reachable from Low Integrity, and IOCTL `0x80002008` mapping arbitrary physical memory into the caller (see §4.5). **Not established:** whether the 2005 32-bit driver in the legacy baseline has the same IOCTL surface. |
| Uninstall method | **Not established.** The vendor package has not been examined here and no install/rollback has been performed. [DRIVER_BACKENDS.md](DRIVER_BACKENDS.md) §5 requires uninstall and rollback to be tested before installation is recommended, which is Phase 0/Phase 1 work. |
| Known vulnerabilities | **Third-party reports, listed in §4.5.** The family has a published local privilege-escalation issue in its 64-bit driver, with no vendor fix recorded at the time this was written. |
| Redistribution | **Vendor statement:** the driver may be redistributed only under the commercial licence; the free package is for personal, non-commercial use. **Not established:** the redistribution basis for the import library that is already tracked in this repository. `LICENSE` describes it as "redistributed unmodified under its own terms"; no vendor statement found grants that, so this is an open item before any public binary release, not a settled one. |

### 4.2 Artefacts in this repository (hashes checked by `scripts/check_dependencies.py`)

| Path | SHA-256 | Size | What it is |
|---|---|---|---|
| `fancontrol/TVicPort.lib` | `9771f02b9dcb27a127ac0df54e4d04a23646eed2660c7635c08cd26b7e8d4592` | 33012 | MSVC import library for `TVicPort.dll`. Contains no driver code; it is the linker's record of the DLL's exported names. |
| `fancontrol/TVicPort.h` | `0f0a83b0e8df7ec244ede5b628946b752e52f2049df5c347b96ad754c7643d80` | 3195 | The vendor's C header: "TVicPort DLL interface / Version 4.0 / Copyright (c) 1997-2005, EnTech Taiwan". |

There is deliberately **no `.dll` or `.sys` in this repository**, and
`scripts/check_dependencies.py` will fail if one is added without being recorded
here. The driver is the user's to install, from the vendor, or it is not there.

### 4.3 What the artefact itself shows (verified here, 2026-09-28)

- An archive of 51 members, every one reported as `pe-i386` — this is a 32-bit
  import library. An x64 link cannot consume it (see §4.4).
- It binds to `TVicPort.dll`, by name, with `__stdcall` imports.
- It exports evaluation-build symbols: `EvaluationDaysLeft` and the mangled
  `?GetTrialDays@@YGXHH@Z`. The library that was committed is from the
  evaluation/freeware distribution, not a licensed commercial build.
- The names it imports include the physical-memory calls listed under "Exposed
  device or API" above. Those are the same class of access that the current
  vulnerability report describes being reachable from user mode.

### 4.4 Consequences for this repository

1. **The x64 configurations cannot link the vendored library.** The solution
   declares `Debug|x64` and `Release|x64` for the application, and the project
   lists `TVicPort.lib` as a library item in all four configurations, but the
   file is 32-bit. Until a 64-bit import library is obtained from the vendor and
   recorded here, the x64 application cannot link, and [BUILD.md](BUILD.md) §5's
   x64 target is not reachable through this backend. Which failure occurs, and
   whether x64 is dropped for the baseline instead, is decided by the T1-01 build
   log — not by this document.
2. **Nothing here may be bundled.** The driver is not redistributable without the
   commercial licence, and the import library's own basis is not established.
   [RELEASE.md](RELEASE.md) has to answer this before any public binary release.
3. **The driver is a known local-privilege-escalation target.** The project's
   response is to record it, never to ask a user to weaken a machine to use it.
   A backend that the operating system's own blocklist refuses is a backend this
   project does not ship a workaround for.
4. **The baseline remains the baseline.** TVicPort is what the legacy build
   already does; documenting it is not adopting it. No decision exists yet on
   the modern backend — that is T5-02's output, recorded in
   [DRIVER_BACKENDS.md](DRIVER_BACKENDS.md) §6 and the Phase 0 report.

### 4.5 Known vulnerabilities (third-party reports)

| Item | What it says | Source |
|---|---|---|
| CVE-2026-30769 | "An issue in the `TVicPort64.sys` component of EnTech Taiwan TVicPort Product v4.0, File v5.2.1.0 allows attackers to escalate privileges via sending crafted IOCTL `0x80002008` requests." CVSS 3.1 7.8 (High), local vector, CWE-20/CWE-269, published 2026-04-29. No vendor fix or workaround listed as of 2026-09-28. | OpenCVE record for [CVE-2026-30769](https://app.opencve.io/cve/CVE-2026-30769), read 2026-09-28 |
| LOLDrivers entry for `TVicPort64.sys` | A catalogue entry, added 2026-02-13, describes loading the driver, device `\\.\TVicPortDevice0` with **no DACL** (reachable from Low Integrity, Guest or an AppContainer), and IOCTL `0x80002008` mapping arbitrary physical memory via `ZwMapViewOfSection`, used for token-stealing local privilege escalation to SYSTEM. It records one sample: AMD64, file version 5.2.1.0, dated 2006-10-13, SHA-256 `9c9ab56c8bcf5ec958e7c2346f23a3027f69abdf8af923b591518eee64ad98ad`, publisher EnTech Taiwan. | [loldrivers.io](https://www.loldrivers.io/drivers/b4f3a1c2-e8d7-4f92-a301-5c6d9e0b1a2f/), read 2026-09-28 |

**What these reports do not say.** Neither report is about the 32-bit import
library in this repository, and neither is a statement that the file in this
tree is vulnerable. The sample hashes are not in this repository. What they do
establish is that the product family has a current, unfixed, known
privilege-escalation issue in its 64-bit driver, which is enough to settle that
it is not a candidate for the modern backend.

### 4.6 What is still not established, and who must establish it

| Field | Task |
|---|---|
| Whether the import library may be redistributed | Release gate, [RELEASE.md](RELEASE.md); if no licence is obtained, the file leaves the distributed tree and the user supplies it |
| Driver signature chain and architecture, as installed | T5-03 (record), T5-01 (adapter) |
| Loads under Secure Boot / HVCI, and whether the blocklist refuses it | T4-06, with T5-02's result for comparison |
| Uninstall and rollback | Phase 0/Phase 1, [DRIVER_BACKENDS.md](DRIVER_BACKENDS.md) §5 |
| Whether the 2005 32-bit driver shares the 5.2.1.0 IOCTL weakness | Not scheduled; if the baseline is ever used on a machine, this moves |

## 5. PawnIO (candidate, not adopted)

Nothing from PawnIO is in this repository, and the required spike
([DRIVER_BACKENDS.md](DRIVER_BACKENDS.md) §4, T5-02) has not run because it needs
a disposable Windows installation, the official package, and an HVCI-capable
machine. This section exists so that the spike has somewhere to write its
answers, not to record any.

| Field | Record |
|---|---|
| Source or vendor | **Vendor statement** (read 2026-09-28): [pawnio.eu](https://pawnio.eu/) describes a scriptable kernel driver with an official signed edition, an unsigned "unrestricted" edition, an open-source edition, and custom licensing. Download is the official GitHub release of `PawnIO.Setup`. |
| License | **Not established.** The site states an open-source version and separate custom licensing; the exact terms have not been read and may differ between editions. T5-02. |
| Exact release | **Not established.** T5-02 step 7 requires the exact tested version and checksum. |
| Driver architecture | **Not established.** T5-02. |
| Signature chain | **Not established.** T5-02, T5-03. |
| HVCI / Secure Boot | **Not established.** T5-02 step 8 is "test with Secure Boot and HVCI enabled". |
| Exposed device or API | **Not established.** The site describes Pawn modules and a Pawn interpreter; [DRIVER_BACKENDS.md](DRIVER_BACKENDS.md) §4 forbids inventing an interface or copying unofficial headers without documenting their provenance. T5-02 steps 3–5. |
| Uninstall method | **Not established.** T5-02. |
| Known vulnerabilities | **Not established.** No vulnerability research on it has been reviewed here, which is not the same as there being none. T5-02 must search for it before adoption, per [DRIVER_BACKENDS.md](DRIVER_BACKENDS.md) §5's security-history criterion. |
| Redistribution | **Not established.** T5-02 step 6. |

## 6. Build and test dependencies

These are not shipped and are recorded only so that "it builds" is a statement
about something reproducible.

| Dependency | Version in use | Note |
|---|---|---|
| C++17 compiler | whatever `CXX` names; `g++` by default | The portable core, the tool and every test build with `-std=c++17 -Wall -Wextra -Werror -pedantic`. Verified in this environment; see [TESTING.md](TESTING.md). |
| Python 3 | `python3` | Runs the static checks. No third-party modules: standard library only, verified by running them on a tree with no virtual environment. |
| Git | any recent | `check_links.py` reads `git ls-files`; `check_ci_steps.py` reads `ci/ci.yml`. |
| Visual Studio 2022 + Windows SDK | not present here | Required for the application and the eight Windows projects. **Not verified**: no MSVC exists in this environment; the first real build is the `windows-build` CI job (T1-01). |

The portable core and the `ecdiag` tool include only their own headers and the
C++ standard library. That is a property the guards can see:
`scripts/check_ecdiag_readonly.py` proves the tool's include closure, and the
build lists every file it compiles.

## 7. Keeping this record true

| Rule | Enforced by |
|---|---|
| Hashes and sizes in §4.2 match the files on disk | `scripts/check_dependencies.py` |
| Every tracked `.lib`/`.dll`/`.sys`/`.exe`/`.msi`/`.cab`/`.zip` appears in §4.2 | `scripts/check_dependencies.py` |
| Every dependency section carries all ten fields | `scripts/check_dependencies.py` |
| The check itself still detects what it claims | `scripts/check_dependencies.py --selftest` |

The record is not a substitute for the Phase 0 report. When a backend is
adopted, the hardware report ([templates/HARDWARE_REPORT.md](templates/HARDWARE_REPORT.md))
carries the machine-specific evidence, and this file keeps the version, licence
and checksum that the report refers to.
