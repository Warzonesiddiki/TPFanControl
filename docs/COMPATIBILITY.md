# Compatibility and capability matrix

Compatibility is determined by evidence, not by product-name similarity. This matrix is the user-facing summary of what the application knows and what it refuses to assume.

## 1. Current status

| Area | Status | Meaning |
|---|---|---|
| Standard T14 Gen 1 Intel identity | Target family only | Exact machine type still must be recorded |
| T14 fan topology | Unverified | Do not enable a single-fan profile yet |
| EC registers | Candidate only | No automatic write probing |
| TVicPort | Legacy dependency | Compatibility and security settings require testing |
| PawnIO | Feasibility pending | SDK/API/licensing/module compatibility must be verified |
| Windows edition/build | Not supplied | Record during Phase 0 |
| Legacy dual-fan profiles | Existing code | Keep isolated from T14 behavior |

## 2. Capability states

Each machine/profile/backend combination should resolve to one of:

- `unsupported`: identity or required capability is incompatible;
- `unknown`: evidence is missing;
- `monitor_only`: read-only status is possible but control is not approved;
- `control_eligible`: all gates pass for the selected profile;
- `failsafe`: control was stopped after a safety or communication failure.

Unknown and unsupported must not be treated as control-eligible.

## 3. Required evidence for control eligibility

The application must have all of the following:

- exact machine type match;
- profile revision linked to a hardware report;
- verified fan topology;
- verified command register and allowed levels;
- verified BIOS/automatic restore behavior;
- approved backend capability;
- valid temperature provider;
- readback or equivalent command verification;
- physical manual-control test where applicable.

## 4. Compatibility report

The status and diagnostic bundle should include:

```text
machine identity result
profile result and evidence reference
backend and dependency result
sensor-provider result
fan-topology result
control eligibility result and blocking reason
```

Do not include serial numbers in public reports.

## 5. Future support process

To add another model:

1. copy the hardware report template;
2. collect identity and read-only evidence;
3. create an ADR for topology and protocol decisions;
4. add a profile marked unverified;
5. add fake-backend tests;
6. perform controlled physical tests;
7. update this matrix and the release compatibility table.

A community report may identify a lead, but it cannot change a profile to verified by itself.
