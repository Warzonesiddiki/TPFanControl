# TPFanControl

Update on project status: I no longer have a thinkpad, so I won't be working any further on this project

## About

This is a Fork of https://github.com/ThinkPad-Forum/TPFanControl/tree/master/fancontrol. The original author extended it to two fan devices. That has only been tested on a P51, and the expectation that it also works on the P50, P70, P71, P52, P72, P1 and X1 Extreme is an **assumption, not evidence** — no commit in this repository records a verification run on any of those machines. See [`docs/HARDWARE_VERIFICATION.md`](docs/HARDWARE_VERIFICATION.md) for what would be required to turn an assumption into evidence.

**No prebuilt binary is distributed from this repository** (ADR-016). Build from source instead; see [`docs/BUILD.md`](docs/BUILD.md). The default profile in [`fancontrol/TPFanControl.ini`](fancontrol/TPFanControl.ini) is a quiet one, with the fans only coming on at 60 °C. Edit that file to change it. The application was built with Visual Studio 2017; later versions may work but are not tested.

## Current modernization status

The checked-in application is legacy code from a dual-fan fork and is not yet a verified T14 Gen 1 Intel release. Do not enable manual fan control on the target laptop based only on the existing P51/P-series configuration. The modernization work, hardware-verification gate, safety rules, and implementation checklist are documented in [`docs/README.md`](docs/README.md).

## Requirements

To avoid errors, either install [tvicport](https://www.entechtaiwan.com/dev/port/index.shtm) manually or install the original version of TPFanControl found [here](https://sourceforge.net/projects/tp4xfancontrol/), and run the dual-fan version instead of the original version.

## Running at startup

The easiest way to run TPFC at startup is:

- Right-click on the executable you built and select copy
- Press Windows-r or search for run in the start menu
- Type `shell:startup` in the run box
- Right click in the window that opens and select paste shortcut

Note: this won’t start TPFC until you reboot.

## Modernization documentation

The T14 Gen 1 Intel modernization plan, safety model, feature backlog, build instructions, portable safety core, testing strategy, and vibe-coding workflow are documented in [`docs/README.md`](docs/README.md). Hardware control must not be enabled until the Phase 0 verification report is complete; the new portable core does not perform EC I/O.

## Governance

- [`LICENSE`](LICENSE) — public domain dedication, with named third-party exceptions
- [`CONTRIBUTING.md`](CONTRIBUTING.md) — the contribution contract and safety rules
- [`docs/IMPLEMENTATION_CHECKLIST.md`](docs/IMPLEMENTATION_CHECKLIST.md) — the taskboard; the single source of truth for what is actually done
- [`docs/DECISIONS.md`](docs/DECISIONS.md) — architecture decision records
- [`docs/SAFETY.md`](docs/SAFETY.md) — safety requirements and the open-gap table

A loud laptop fan is the fan working. This software is not hardware-qualified; prefer your BIOS.
