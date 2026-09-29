# Observability, telemetry, and fan health

The application must explain its decisions without requiring raw EC knowledge. Observability is local-first, bounded, and privacy-aware.

## 1. Status snapshot

The UI, CLI, and diagnostic exporter should consume the same immutable status snapshot. It should contain:

```text
applicationVersion
profileId and profileRevision
hardwareIdentityStatus
backendName, backendVersion, and capability state
safetyState
controlMode
powerSource
maximumValidTemperature and source
all valid sensor readings with age
requestedFanCommand
measuredFanRpm and source, when available
fanHealth
manualOverrideExpiry, when active
lastCommandResult
lastSafetyEvent
```

Unknown values must be represented as unknown, not zero. A zero temperature or zero RPM must not be used as a missing-value sentinel in the public model.

## 2. Event levels

Use structured events with a timestamp, event code, severity, and redacted context:

- `Info`: profile selection, normal mode change, startup complete;
- `Notice`: monitor-only fallback, manual override started/expired, power-source change;
- `Warning`: stale sensor, suspect fan response, repeated read failure;
- `Error`: backend initialization failure, write/readback mismatch, invalid profile;
- `Critical`: thermal emergency, inability to restore automatic control, shutdown recommendation.

Event text is for humans; event codes are for tests and support tooling.

The portable event contract is `fancontrol/core/events.h` (ADR-031). Events are fixed-size typed values with monotonic timestamps; they contain no free-form strings, user identity, paths, raw EC bytes, or wall-clock time. The core sends them through `IEventSink::tryEmit`, which adapters must implement with bounded capacity, non-blocking behavior, and no exceptions. A rejected event is dropped and counted by the sink; producers do not retry, and log pressure must never change control behavior. Formatting and persistence are adapter responsibilities. This contract does not claim that a sink or event emission is implemented.

## 3. Log policy

- Use a bounded rotating log.
- Keep a configurable maximum file size and file count.
- Never log serial numbers, usernames, access tokens, or raw private paths by default.
- Do not log every successful EC read at normal verbosity.
- Permit a short-lived verbose diagnostic mode only after explicit user action.
- Mark read-only and write-capable operations distinctly.
- Include the selected profile revision and application build identifier.

A crash or restart must not be treated as proof that fan control was restored.

## 4. CSV telemetry

CSV is for local analysis, not the primary event log. Recommended columns:

```text
timestamp_utc,elapsed_ms,power_source,profile_id,safety_state,
max_temp_c,max_temp_source,requested_fan,fan_rpm,fan_health,
backend_state,last_command_result
```

Write atomically or append safely, rotate it, and stop logging cleanly on shutdown. Do not include raw EC dumps in normal CSV output.

## 5. Fan-health policy

A fan-health result is meaningful only when the profile says that RPM is supported and defines an observation window. The application must distinguish:

- RPM not supported;
- RPM temporarily unavailable;
- fan expected to be stopped;
- no response after a verified command;
- rising temperature despite a command.

See [CONTROL_ALGORITHM.md](CONTROL_ALGORITHM.md) for the state rules.

## 6. Diagnostic bundle

The diagnostic exporter should create a versioned archive or directory containing:

- sanitized system identity;
- Windows and application version;
- selected profile and verification status;
- backend capability result and dependency versions;
- current status snapshot;
- recent bounded event log;
- recent telemetry sample;
- configuration validation result;
- test/evidence references if known.

It should exclude serial numbers, Windows user names, credentials, arbitrary raw EC dumps, and unrelated files. The user must be told where the bundle was written.

## 7. Graphing requirements

A future UI graph should show, with a legend:

- maximum valid temperature and source;
- requested fan command;
- measured RPM when available;
- safety-state changes;
- profile/power-source changes.

It should label gaps caused by invalid or unavailable samples rather than drawing a false continuous line.

## 8. Support workflow

A bug report should include:

1. sanitized diagnostic bundle;
2. exact application build;
3. selected profile/revision;
4. reproduction steps;
5. whether the issue occurred on AC or battery;
6. whether BIOS/automatic control was restored;
7. physical test report if manual control was involved.
