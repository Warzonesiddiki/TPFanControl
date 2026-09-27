# Target architecture

## 1. Design goals

The modernization must be:

- hardware-aware without hardcoding unverified facts;
- safe when reads, writes, drivers, or sensors fail;
- testable without a physical laptop;
- compatible with the existing Win32 application during migration;
- explicit about which operations require administrator rights;
- able to support a single-fan T14 without breaking legacy dual-fan profiles.

## 2. Current application boundary

The legacy application currently combines UI, configuration parsing, EC access, fan control, logging, and lifecycle behavior in a small number of Win32/C++ files. The source project targets Win32 and directly imports the TVicPort API. The fan-control implementation also contains a dual-fan write path.

The modernization should not copy this coupling into new modules.

## 3. Target component model

```text
+----------------------------+
| TPFanControl UI            |
| tray + existing dialog     |
+-------------+--------------+
              | commands/status
+-------------v--------------+
| Controller / Safety        |
| curve + hysteresis         |
| state machine              |
+------+------+--------------+
       |      |
       |      +--------------------+
       |                           |
+------v-------+           +-------v--------+
| Temperature  |           | Profile/config |
| providers    |           | validation     |
+------+-------+           +----------------+
       |
+------v-------+
| EC model     |
| T14 profile  |
+------+-------+
       |
+------v-------+
| EC protocol  |
| mutex/timeouts/readback |
+------+-------+
       |
+------v-------+
| IIoBackend   |
| TVicPort / PawnIO / fake |
+---------------------------+
```

## 4. Layer responsibilities

### `IIoBackend`

Owns privileged I/O only.

Responsibilities:

- initialize and shut down the backend;
- read/write byte ports;
- optionally read MSRs;
- return typed errors;
- expose capabilities;
- never know about fan levels or temperature curves.

It must not:

- parse the INI file;
- decide whether a temperature is safe;
- write an EC register merely to probe it.

### EC protocol layer

Owns the EC transaction protocol:

- wait for IBF/OBF states;
- issue read/write commands;
- enforce timeouts;
- serialize access;
- convert backend errors to EC errors;
- never select fan 2 for a single-fan profile.

### Hardware profile

Describes model-specific facts:

```cpp
struct HardwareProfile {
    std::string id;
    std::vector<std::string> machineTypes;
    FanTopology topology;
    uint16_t dataPort;
    uint16_t commandPort;
    uint8_t fanControlOffset;
    std::optional<uint8_t> fanSelectorOffset;
    uint8_t fanSpeedLowOffset;
    std::optional<uint8_t> fanSpeedHighOffset;
    std::vector<uint8_t> temperatureOffsets;
};
```

All register values in a profile are either verified evidence or clearly marked candidate values. The profile must include a verification status and source report.

### Temperature providers

Each provider returns a timestamped, validity-checked reading:

```cpp
struct TemperatureReading {
    int valueC;
    bool valid;
    uint64_t timestampMs;
    std::string source;
    std::string error;
};
```

Providers must reject:

- `0x00`/`0x80` sentinel values where applicable;
- impossible temperatures;
- stale readings;
- failed backend reads;
- CPU readings without a valid thermal-status bit.

### Controller and safety

The controller consumes only valid readings and emits a validated command. Safety owns the right to override every normal command.

```text
Normal request -> validate -> safety gate -> EC write -> readback -> status
                                      |
                                      +-> failsafe BIOS command on failure
```

No UI component writes to the EC directly.

## 5. Threading model

Initial MVP:

- UI thread owns the window and tray messages.
- One worker performs EC reads and control decisions.
- A cancellation flag stops the worker.
- EC access is serialized by one named/local mutex.
- UI receives immutable status snapshots.

Do not use raw integer casts to pass C++ object pointers through a 32-bit API. Use `LPVOID`, `uintptr_t`, `SetWindowLongPtr`, and `GetWindowLongPtr` where appropriate. Prefer `_beginthreadex` or a controlled `std::thread` wrapper over untyped `CreateThread` callbacks when the CRT is involved.

## 6. State machine

```text
STARTING
  -> MONITOR_ONLY       backend/sensors unavailable
  -> VALIDATING          collecting valid samples
  -> CONTROLLED          profile is active
  -> FAILSAFE_BIOS       safety override or communication failure
  -> STOPPING            restore BIOS and close
```

Allowed transitions must be documented and tested. There must be no direct `MONITOR_ONLY -> CONTROLLED` transition without validation.

## 7. Migration strategy

Use a strangler migration:

1. Preserve the existing UI and configuration entry point.
2. Introduce the backend interface.
3. Move EC operations behind the interface.
4. Add a fake backend and tests.
5. Introduce the T14 profile.
6. Move controller/safety logic out of UI code.
7. Improve UI and lifecycle after hardware qualification.

Do not add a curve editor, service split, or multiple-model probe until the controller can be tested independently.

## 8. Directory direction

```text
src/
  hardware/
  sensors/
  control/
  config/
  platform/windows/
  ui/

profiles/
tests/
tools/ecdiag/
docs/
```

The existing project can be migrated incrementally. A new build system should not be introduced solely to reorganize files; keep the Visual Studio solution as the first supported build until the core is stable.
