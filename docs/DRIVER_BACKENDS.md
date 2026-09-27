# Driver and I/O backend strategy

## 1. Problem statement

TPFanControl requires privileged access to EC I/O ports. That access is provided by a kernel component or an approved operating-system interface. The application must not assume that a user-mode x64 build automatically grants hardware access.

The existing source imports TVicPort. The target Windows security configuration, installed driver package, signing state, and architecture must be tested on the actual laptop before replacing it.

## 2. Backend contract

All backends implement the same contract:

```cpp
struct BackendCapabilities {
    bool portIo;
    bool msrRead;
    bool msrWrite;
    bool signedPackage;
    bool supportsCurrentProcessArchitecture;
};

class IIoBackend {
public:
    virtual ~IIoBackend() = default;
    virtual bool initialize(std::string& error) = 0;
    virtual void shutdown() noexcept = 0;
    virtual BackendCapabilities capabilities() const noexcept = 0;
    virtual bool readPort8(uint16_t port, uint8_t& value, IoError& error) = 0;
    virtual bool writePort8(uint16_t port, uint8_t value, IoError& error) = 0;
    virtual bool readMsr(uint32_t msr, uint64_t& value, IoError& error) = 0;
};
```

The exact public method names can change, but the interface must provide bounded failure reporting. No controller code should depend on TVicPort, PawnIO, or another backend directly.

## 3. TVicPort compatibility adapter

Phase 1 should answer these questions instead of assuming the answer:

- Does the installed TVicPort package contain an x64-capable driver?
- Is the driver signed and accepted with Secure Boot enabled?
- Is it accepted with HVCI/Memory Integrity enabled?
- Does the existing 32-bit application load it?
- Does an x64 application load it?
- Does the vendor DLL match the linked import library?

Record the result in the Phase 0/Phase 1 report. The project must not silently require the user to disable Windows security features.

## 4. PawnIO feasibility

PawnIO is a candidate modern backend. Treat it as an external dependency whose current official API, module format, signing model, licensing, and supported architectures must be verified from the official distribution before coding against it.

Required feasibility spike:

1. Install the official signed edition on a disposable/test Windows installation.
2. Confirm the driver and device are present.
3. Build the smallest approved sample that reads a harmless port.
4. Confirm how port I/O and MSR access are exposed.
5. Confirm whether a project-specific module is required.
6. Confirm redistribution and licensing requirements.
7. Record exact tested version and checksum.
8. Test with Secure Boot and HVCI enabled.

Do not invent a `pawnio.lib` interface or copy unofficial headers without documenting their provenance.

## 5. Fallback policy

A fallback backend must not merely be “another driver that happens to work.” It must be evaluated for:

- signing;
- HVCI compatibility;
- maintenance;
- license;
- architecture;
- uninstall behavior;
- security history;
- failure behavior.

If no approved backend is available, the correct fallback is:

```text
Monitor temperatures/RPM if possible.
Do not issue manual EC writes.
Leave fan control to BIOS.
Explain the missing dependency to the user.
```

Do not disable Secure Boot, HVCI, or the vulnerable-driver blocklist as part of normal installation.

## 6. x64 and Win32 policy

x64 is the primary target because it avoids pointer-size limitations and matches modern driver tooling. The target Windows edition and build are not yet supplied and must be recorded during Phase 0. A Win32 build may be retained only if the selected backend supports it and it passes the same safety tests.

Do not advertise Win32 compatibility merely because the source compiles. Record a matrix:

| Backend | Win32 app | x64 app | Secure Boot | HVCI | Control allowed |
|---|---|---|---|---|---|
| TVicPort | Not tested | Not tested | Not tested | Not tested | No decision |
| PawnIO | Not tested | Not tested | Not tested | Not tested | No decision |
| Fake backend | Test only | Test only | N/A | N/A | Never hardware |

## 7. Error categories

Backends should distinguish:

- `NotInstalled`
- `NotSignedOrBlocked`
- `ArchitectureMismatch`
- `AccessDenied`
- `DeviceUnavailable`
- `PortReadFailed`
- `PortWriteFailed`
- `MsrUnsupported`
- `Timeout`
- `Unknown`

The UI should display an actionable message without exposing unsafe remediation such as disabling security controls.

## 8. Dependency policy

- Do not commit proprietary driver binaries unless redistribution rights are documented.
- Do not commit private credentials, certificates, or signing keys.
- Pin tested versions in `docs/BUILD.md` and release notes.
- Store checksums for installers or downloaded packages.
- Prefer a user-installed official driver over silently bundling a kernel component.
- Test uninstall and rollback before recommending installation.
