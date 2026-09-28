# Security and dependency policy

## 1. Security objectives

TPFanControl is a hardware-control application. Security includes both protection of the operating system and protection against unsafe hardware writes.

The project must not require users to disable Secure Boot, HVCI, Memory Integrity, or the vulnerable-driver blocklist as normal installation steps.

## 2. Privilege boundaries

The application may require elevation for low-level hardware access. Elevation must be:

- declared in the manifest or explicit installer action;
- explained to the user;
- limited to the hardware-control process;
- absent from monitor-only tools where possible.

A visible tray UI should run in the interactive user session. A privileged service, if later required, must be a separate process with an authenticated IPC boundary.

## 3. Kernel/backend dependencies

Before adopting a backend, document:

- source or vendor;
- license;
- exact release;
- driver architecture;
- signature chain;
- HVCI/Secure Boot behavior;
- exposed device/API;
- uninstall method;
- known vulnerabilities;
- redistribution rights.

Do not rely on a backend merely because a community utility loads it on one machine.

The record itself is [DEPENDENCIES.md](DEPENDENCIES.md), one section per
dependency, each field labelled with how it is known. `scripts/check_dependencies.py`
fails when a recorded hash stops matching the file, when a section is missing one
of the ten fields, or when a tracked binary artefact has no checksum row.

## 4. Input validation

Treat all configuration as untrusted input. Validate:

- integer ranges;
- hexadecimal syntax;
- machine/profile identity;
- curve ordering;
- fan command allow-list;
- temperature thresholds;
- timeouts and retry counts;
- paths and log destinations.

Do not pass profile strings directly to shell commands. The legacy use of shell-based file operations must be removed or replaced with safe Win32/C++ file APIs.

## 5. Logging and privacy

Logs must not contain by default:

- serial numbers;
- Windows user names;
- access tokens;
- private directories;
- unrelated process command lines.

Rotate logs and enforce a size limit. Provide a diagnostic export that redacts identity fields.

## 6. Update and rollback

A future updater must verify package integrity before replacing the executable. It must retain the previous working build until the new build starts and reports a healthy backend/profile state.

Rollback must restore the application and profile, not write undocumented EC values.

## 7. Feature-specific security controls

- Workload/application profiles must use an explicit allowlist and disclose which process names are inspected.
- Diagnostic bundles must be generated from an allowlist and redact serials, usernames, credentials, and private paths.
- A future local API must be disabled by default, user-scoped, authenticated, and read-only unless a separate security review approves otherwise.
- Notifications and telemetry must not transmit data off the machine by default.
- Profile import must validate schema, ranges, topology, and verification status before saving or activation.

## 8. Review triggers

Require additional security review for:

- new kernel/backend dependencies;
- new EC write registers;
- MSR write support;
- service/IPC implementation;
- automatic driver installation;
- installer changes;
- remote or network control;
- changes that disable or bypass Windows security;
- workload/process monitoring or a local API;
- diagnostic archive/export handling;
- profile import/export or update logic.
