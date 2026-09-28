# Release and distribution plan

## 1. Release goals

A release must be reproducible, reversible, and honest about hardware support. It must not silently install an unverified kernel component or claim support based only on compilation.

## 2. Release channels

### Development build

- May require manual backend installation.
- May have diagnostic logging enabled.
- Must default to monitor-only when profile verification is incomplete.
- Must not be presented as safe for unattended use.

### Release candidate

- Clean Release build.
- Hardware test report completed.
- Safety and fault-injection tests completed.
- Known limitations published.
- Rollback procedure tested.

### Stable release

- Exact supported machine types listed.
- Backend versions listed and tested.
- Installer/uninstaller tested.
- No unresolved high-risk safety issue.
- Release artifacts checksummed.

## 3. Package layout

```text
TPFanControl-<version>-win-x64/
  TPFanControl.exe
  TPFanControl.ini
  profiles/
    T14Gen1-Intel.ini
  README.txt
  LICENSE.txt
  THIRD-PARTY-NOTICES.txt
  docs/
    BUILD.md
    TROUBLESHOOTING.md
    SAFETY.md
    USER_GUIDE.md
    COMPATIBILITY.md
```

Do not include private user logs, `.pdb` files, `.obj` files, Visual Studio databases, or developer-specific paths.

## 4. Driver distribution

Do not bundle a third-party kernel driver until redistribution rights, signing, architecture, installation, and uninstall behavior are documented.

Prefer:

1. detect whether the approved backend is installed;
2. show the official installation source;
3. verify the installed version and capabilities;
4. provide a monitor-only fallback;
5. never ask users to disable Windows security controls as a prerequisite.

If an installer later installs the backend, it must:

- request elevation explicitly;
- display the driver name/version;
- verify signature and architecture;
- record installation result;
- support uninstall;
- fail without touching EC settings if installation fails.

## 5. Versioning

Use semantic versioning for the application once the first modern MVP exists:

```text
MAJOR.MINOR.PATCH
```

Increase:

- MAJOR for profile/config incompatibility or safety behavior changes;
- MINOR for backward-compatible features;
- PATCH for fixes that do not change the profile contract.

Hardware profile revisions should be tracked separately:

```ini
ProfileSchemaVersion=1
ProfileRevision=2
```

## 6. Release checklist

### Source and build

- [ ] Clean checkout builds.
- [ ] Release x64 build passes.
- [ ] Win32 compatibility status is documented.
- [ ] Unit tests pass.
- [ ] Fake-backend safety tests pass.
- [ ] `git diff --check` passes.
- [ ] No generated or private files are packaged.

### Hardware

- [ ] Exact machine types listed.
- [ ] EC map evidence attached.
- [ ] Single/dual topology documented.
- [ ] Manual-level test report attached.
- [ ] Smart-curve test report attached.
- [ ] BIOS restore tested.
- [ ] Failure-path tests attached.

### Security

- [ ] Backend dependency and version documented.
- [ ] Signature/HVCI result documented.
- [ ] No security bypass instructions in the normal installation path.
- [ ] No secrets or private paths in release artifacts.

### User experience

- [ ] First run is safe.
- [ ] Unsupported backend gives an actionable message.
- [ ] Monitor-only mode is obvious.
- [ ] Return-to-BIOS action is available.
- [ ] Monitor-only status is obvious.
- [ ] Diagnostic export is redacted and bounded.
- [ ] Manual override expiry is visible and tested.
- [ ] Uninstall instructions are tested.
- [ ] Rollback instructions are tested.

## 7. Rollback

The release must document how to:

1. stop TPFanControl;
2. restore BIOS/automatic mode;
3. disable/remove startup;
4. uninstall the optional backend if installed by the user;
5. remove the application without deleting user evidence;
6. return to the previous known-good build.

A rollback must not require writing undocumented EC registers.
