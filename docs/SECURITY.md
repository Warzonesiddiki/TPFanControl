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

## 7. Review triggers

Require additional security review for:

- new kernel/backend dependencies;
- new EC write registers;
- MSR write support;
- service/IPC implementation;
- automatic driver installation;
- installer changes;
- remote or network control;
- changes that disable or bypass Windows security.
