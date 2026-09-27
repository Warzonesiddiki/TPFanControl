# Hardware and fan profiles

## 1. Profile goals

A profile combines:

- machine identity;
- EC port and register map;
- fan topology;
- sensor selection;
- temperature curve;
- safety thresholds;
- backend capabilities.

The profile is data, not a collection of model-specific `if` statements spread through the controller.

## 2. T14 profile status

The T14 Gen 1 Intel profile is a planned profile. Its EC values must remain unapproved until the Phase 0 hardware report is complete.

Expected identity fields:

```ini
[Hardware]
ProfileId=T14Gen1-Intel
DisplayName=ThinkPad T14 Gen 1 Intel
MachineTypes=20S0,20S1
FanTopology=single
VerificationStatus=unverified
```

Do not publish a profile with `VerificationStatus=verified` without a completed hardware report.

## 3. Profile format proposal

The existing `TPFanControl.ini` format should remain readable during migration. New settings may be grouped gradually, but the parser must validate all values before control starts.

```ini
[Hardware]
ProfileId=T14Gen1-Intel
MachineTypes=20S0,20S1
FanTopology=single
VerificationStatus=unverified
EcDataPort=0x0000
EcCommandPort=0x0000
FanControlOffset=0x00
FanSpeedLowOffset=0x00
FanSpeedHighOffset=0x00

[Runtime]
Active=0
CycleSeconds=5
StartMinimized=1
LogEnabled=1

[Safety]
MaxReadErrors=3
EmergencyTemperatureC=92
ManualMinimumSafetyTemperatureC=85
SensorFreshnessSeconds=10
RestoreBiosOnExit=1

[Curve]
Point1=55,0,50
Point2=65,1,60
Point3=75,2,70
Point4=82,4,77
Point5=88,7,83
Point6=92,BIOS,88

[ManualOverride]
Enabled=0
DurationSeconds=900
MaximumLevel=7
CancelOnInvalidSensor=1
CancelOnBackendError=1

[PowerPolicies]
AcProfile=Balanced
BatteryProfile=BatterySaver

[Observability]
LogEnabled=1
CsvEnabled=0
TelemetryRetentionMinutes=120
DiagnosticVerbose=0

[Sensors]
UseEcSensors=1
UseCpuDts=0
IgnoreSensors=
```

The zero/unverified register values intentionally prevent control. They must be replaced only from verified hardware evidence.

## 4. Curve semantics

Each point is:

```text
upper/ramp-up temperature, fan command, lower/ramp-down temperature
```

Rules:

- points must be ordered by increasing temperature;
- down threshold must be lower than or equal to up threshold;
- commands must be valid for the profile;
- the curve must end in a BIOS/automatic or safe maximum policy;
- no temperature gap may leave the controller without a valid command;
- the first point must not silently turn the fan off at a high temperature;
- a curve must be validated before being activated.

The exact values above are placeholders for implementation tests, not final T14 tuning.

## 5. First-run policy

A new installation should start with:

```ini
Active=0
VerificationStatus=unverified
RestoreBiosOnExit=1
```

After hardware verification and a successful manual-control test, the user may enable smart mode. This prevents a new build from taking control using an old or incorrect profile.

## 6. Sensor policy

Do not add `IgnoreSensors=pwr` by default merely because a community report found a fixed value on another firmware revision. First record the raw sensor behavior. If a sensor is known to be invalid on the exact machine, document the evidence and exclude it with a profile-specific reason.

A sensor that cannot be read should be marked unavailable, not represented as zero degrees.

## 7. Legacy dual-fan profile

The existing P-series/dual-fan behavior must be retained as a separate profile:

```ini
[Hardware]
ProfileId=Legacy-DualFan
FanTopology=dual
VerificationStatus=legacy
```

It must never be selected automatically for the T14 profile.

## 8. Configuration validation

Reject the configuration before control starts when:

- `FanTopology` is unknown;
- required verified registers are missing;
- curve points are not monotonic;
- fan commands are not allowed;
- emergency threshold is below normal curve thresholds;
- `Active` requests control while verification is incomplete;
- a single-fan profile contains a second-fan write setting;
- backend capability does not meet profile requirements.

## 9. Profile versioning

Every profile should include:

```ini
ProfileSchemaVersion=1
ProfileRevision=1
EvidenceId=T14G1-INTEL-YYYYMMDD
```

Changing a register, topology, or safety threshold requires a revision and updated evidence.
