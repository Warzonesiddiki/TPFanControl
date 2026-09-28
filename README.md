# TPFanControl
Fan control for ThinkPads, being rebuilt around a portable, tested core.
> **Status: active modernization; nothing verified on hardware.** The upstream
> author stopped maintaining this fork after losing access to a ThinkPad. The
> work continues anyway, which is why every hardware-dependent claim below is
> gated: no machine has been tested, and the Windows application has not been
> compiled or run in this repository's environment. See
> [Project status](#project-status).
## About
This is a fork of https://github.com/ThinkPad-Forum/TPFanControl/tree/master/fancontrol. The original author extended it to two fan devices. That has only been tested on a P51, and the expectation that it also works on the P50, P70, P71, P52, P72, P1 and X1 Extreme is an **assumption, not evidence** — no commit in this repository records a verification run on any of those machines. See [`docs/HARDWARE_VERIFICATION.md`](docs/HARDWARE_VERIFICATION.md) for what would be required to turn an assumption into evidence.
**No prebuilt binary is distributed from this repository** (ADR-016). Build from source instead; see [`docs/BUILD.md`](docs/BUILD.md). The default profile in [`fancontrol/TPFanControl.ini`](fancontrol/TPFanControl.ini) is a quiet one, with the fans only coming on at 60 °C. Edit that file to change it.
## Project status
| Area | State |
|---|---|
| Portable core (`fancontrol/core/`) | 190 test functions across seven suites, built and run on Linux by `sh tests/run_core_tests.sh` and by the workflow in [`ci/`](ci/README.md) |
| The Win32 application | Legacy code with the core wired into it. **Not compiled or run here** — this environment has no MSVC and no Windows, so its behaviour on a laptop is design intent, not measurement |
| Hardware | **Nothing verified.** Phase 0 of [`docs/HARDWARE_VERIFICATION.md`](docs/HARDWARE_VERIFICATION.md) is open: no machine type, BIOS revision, EC port mapping, fan topology, or driver backend has been confirmed on a real machine |
| Manual fan control | **Not enabled anywhere.** The core starts monitor-only and refuses to take control until the machine, profile, topology, backend, and restore capability are verified (ADR-027) |
The portable core is exercised only against fakes in this repository. It has never touched an embedded controller.
## Requirements
To avoid errors, either install [tvicport](https://www.entechtaiwan.com/dev/port/index.shtm) manually or install the original version of TPFanControl found [here](https://sourceforge.net/projects/tp4xfancontrol/), and run the dual-fan version instead of the original version. Only the driver's port-level primitives are used; see [`docs/DRIVER_BACKENDS.md`](docs/DRIVER_BACKENDS.md).
**Read this before installing that driver.** The vendor's 64-bit TVicPort driver has an unresolved local privilege-escalation advisory — CVE-2026-30769, CVSS 7.8, no vendor fix listed as of 2026-09-28 — and is catalogued as a known vulnerable driver. Neither report is about the 32-bit import library in this tree, and whether the driver loads under Secure Boot or HVCI, including whether Microsoft's vulnerable-driver blocklist refuses it, is **unverified**. What is settled: this driver family is not a candidate for the modern backend, and this project will never ask you to weaken a machine to load a driver the operating system refuses. Details and the open questions are in [`docs/DEPENDENCIES.md`](docs/DEPENDENCIES.md) §4.5–§4.6.
## Building and testing
- **Build the application:** [`docs/BUILD.md`](docs/BUILD.md) — Visual Studio 2022 with the v143 toolset, x64. The legacy project files are Visual Studio 2017-era, and the x64 configuration exists but **has never been executed**: no MSVC toolchain is available in this repository's environment, and the x86-only `TVicPort.lib` is a known obstacle to it.
- **Test the portable core without Windows:** `sh tests/run_core_tests.sh`, described in [`tests/README.md`](tests/README.md). What each layer does and does not prove is in [`docs/TESTING.md`](docs/TESTING.md); the taskboard that decides what counts as done is [`docs/IMPLEMENTATION_CHECKLIST.md`](docs/IMPLEMENTATION_CHECKLIST.md).
- **Read-only diagnostic:** [`docs/ECDIAG.md`](docs/ECDIAG.md) — a tool that reads the EC and issues no register writes, for looking at a machine before trusting any of this with it.
## Running at startup
The easiest way to run TPFC at startup is:
- Right-click on the executable you built and select copy
- Press Windows-r or search for run in the start menu
- Type `shell:startup` in the run box
- Right click in the window that opens and select paste shortcut
Note: this won’t start TPFC until you reboot.
## Modernization documentation
The T14 Gen 1 Intel modernization plan, safety model, feature backlog, build instructions, portable safety core, testing strategy, and vibe-coding workflow are documented in [`docs/README.md`](docs/README.md). Hardware control must not be enabled until the Phase 0 verification report is complete — and even then the core will not take control until hardware, profile, topology, backend, and restore capability are each verified.
## Governance
- [`LICENSE`](LICENSE) — public domain dedication, with named third-party exceptions
- [`CONTRIBUTING.md`](CONTRIBUTING.md) — the contribution contract and safety rules
- [`docs/IMPLEMENTATION_CHECKLIST.md`](docs/IMPLEMENTATION_CHECKLIST.md) — the taskboard; the single source of truth for what is actually done
- [`docs/DECISIONS.md`](docs/DECISIONS.md) — architecture decision records
- [`docs/SAFETY.md`](docs/SAFETY.md) — safety requirements and the open-gap table
A loud laptop fan is the fan working. This software is not hardware-qualified; prefer your BIOS.

