#pragma once

#include "ec_access.h"
#include "io_backend.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tpfancontrol {
namespace core {

// ---------------------------------------------------------------------------
// ecdiag - the read-only EC diagnostic (T5-09)
// ---------------------------------------------------------------------------
//
// What this is for
// ----------------
// Phase 0 has to classify the target machine's embedded controller before any
// control path may be enabled: which ports address it, which offsets hold the
// fan command, the tachometer and the temperature block, and whether a value
// read from an offset is plausible at all. Every T4 row needs that, and every
// T4 row is blocked on a machine. What is NOT blocked is the instrument: the
// part that performs the reads, records exactly what it did, and emits a report
// a person can attach to a hardware report.
//
// This file is that instrument. It is deliberately not a control path. It is
// not a backend either - see "read-only by construction" below.
//
// Read-only by construction, not by convention
// --------------------------------------------
// The tool is handed an IRegisterReader and nothing else. That interface has
// exactly one operation, readRegister(). There is no write method to call, so
// "ecdiag never writes a register" is not a rule the code follows; it is a
// statement about which types the code can name. A convention would be worth
// much less: the failure this project is guarding against (EC_REGISTER_MAP.md
// section 10) was an unconditional write that every reviewer had read and none
// had noticed.
//
// The construction is defended three more ways, because the type system only
// covers the code inside this file:
//
//   1. scripts/check_ecdiag_readonly.py fails the build if these sources name a
//      write API, a bus, or a bridge, and it has a self-test proving the rules
//      still match what they claim to.
//   2. tests/ecdiag_tests.cpp runs a full diagnostic through a real EcBus over a
//      fake EC and asserts, from the bus's own write trace and the fake's own
//      committed-write log, that no write command was ever issued. That is a
//      claim about what reached the primitive, not about what the code meant.
//   3. The report cross-checks the reader's own audit and fails loudly if the
//      status port ever carried anything but a read command.
//
// A read is not "no port writes"
// ------------------------------
// The EC protocol writes a command byte to the status port even to read
// (ADR-020), so a diagnostic that performed no port writes would read nothing.
// What is forbidden is a REGISTER write, and the distinguishing byte is the
// command byte: 0x80 for a read, 0x81 for a write, in the transcribed encoding.
// The audit below records the command bytes that were actually issued, so the
// reader is not taken at its word about its own read-only-ness.
//
// What this deliberately does not do
// ----------------------------------
//  * It does not sweep the address space. It reads an explicit plan: the
//    candidate offsets documented in EC_REGISTER_MAP.md section 2, plus any the
//    operator named. A tool that reads every register of an unknown EC is a
//    discovery mechanism nobody has reviewed, and unbounded reads on an
//    unverified part can have side effects of their own (status latches).
//  * It does not decide anything. It reports raw values. Correlating a raw
//    value with an independent measurement is a human step
//    (HARDWARE_VERIFICATION.md section 7), and a tool that guessed would be
//    manufacturing the evidence it exists to collect.
//  * It does not write. Anywhere. Ever. There is no option that changes that.

// The schema identifier of the machine-readable report. Bump it when a field is
// removed or changes meaning; adding a field does not need a bump.
inline constexpr const char* kEcDiagSchema = "tpfancontrol.ecdiag/1";

// The tool's own version, recorded in every report so a report can be tied to
// the build that produced it.
std::string ecDiagToolVersion();

// "release" or "debug". Recorded because the answer changes what a report
// proves: NDEBUG is what compiles assertion-based checks out.
std::string ecDiagBuildFlavour();

// The read-only seam this tool receives - IRegisterReader and ReaderAudit - is
// declared in ec_access.h rather than here. That header contains no write
// operation at all, which is why including it costs the tool nothing in
// guarantee. ec_protocol.h is deliberately NOT included: it declares EcBus, and
// a diagnostic that cannot name the bus cannot write through it.

// ---------------------------------------------------------------------------
// The plan
// ---------------------------------------------------------------------------

struct EcDiagTarget {
    std::uint8_t address = 0;

    // The candidate meaning, copied from EC_REGISTER_MAP.md section 2. The word
    // "candidate" is load-bearing: this is a hypothesis to be tested by the
    // reading, not an assertion about what the offset is.
    std::string label;
};

struct EcDiagPlan {
    std::vector<EcDiagTarget> targets;

    // Repeats per target. A single reading cannot distinguish a stable register
    // from one that changes, and EC_REGISTER_MAP.md section 6 asks for repeated
    // samples before a reading is treated as valid.
    int samplesPerTarget = 3;
};

// The candidate offsets from EC_REGISTER_MAP.md section 2, in that order:
// 0x2F, 0x31, 0x84, 0x85, 0x78-0x7F and 0xC0-0xC3. 0x31 is included because
// reading it is how its meaning would ever be established; the prohibition in
// that document is on writing it, and reading a register map into existence is
// the only sanctioned discovery method (ADR-004).
std::vector<EcDiagTarget> candidateT14ReadOnlyTargets();

// Bounds. A plan is a list a person wrote, not a sweep.
inline constexpr std::size_t kEcDiagMaximumTargets = 64;
inline constexpr int kEcDiagMaximumSamplesPerTarget = 32;

// Empty when the plan is acceptable, otherwise the reason it is not.
std::string validatePlan(const EcDiagPlan& plan);

// Appends one target unless that address is already present, so a plan cannot
// silently read the same register twice and report it as two findings.
void addTarget(std::vector<EcDiagTarget>& targets, std::uint8_t address,
               const std::string& label);

// Adds every address in [first, last], in order.
void addTargetRange(std::vector<EcDiagTarget>& targets, std::uint8_t first,
                    std::uint8_t last, const std::string& label);

// ---------------------------------------------------------------------------
// The run
// ---------------------------------------------------------------------------

// What the caller knows about the backend that the reader does not.
struct EcDiagBackendInfo {
    std::string name = "unknown";
    std::string version;                 // empty means not reported
    bool haveCapabilities = false;
    BackendState state = BackendState::Unavailable;
    bool canReadPorts = false;
    bool canWritePorts = false;
    bool canReadMsr = false;
    bool signedAndApproved = false;
};

struct EcDiagOptions {
    // The configuration under test, recorded verbatim in the report. Evidence
    // without its configuration is not evidence: a plausible-looking set of
    // readings taken through the wrong port mapping is a wrong answer that reads
    // as a right one.
    EcBusConfig config;

    // Where those values came from. The default is the encoding transcribed
    // from fancontrol/portio.cpp, which is a record of what the shipped
    // application does and not a measurement of what this machine requires
    // (EC_REGISTER_MAP.md section 3.1).
    std::string configSource = "default-transcribed-from-portio.cpp";

    EcDiagBackendInfo backend;

    // True when the values did not come from hardware. A simulated run is a
    // contract check, never evidence, and the report says so in a field a
    // machine can read as well as in prose.
    bool simulated = false;

    // UTC timestamp for the report, "YYYY-MM-DDTHH:MM:SSZ". Empty means the
    // caller did not supply one and the report carries null.
    std::string generatedUtc;

    // Monotonic milliseconds, injected as a plain callable so this header needs
    // no clock type and a test can supply one that never advances. Empty means
    // per-sample elapsed times are reported as null rather than as zero.
    std::function<std::uint64_t()> nowMs;

    // Extra statements for the report's limitations list.
    std::vector<std::string> limitations;
};

struct EcDiagSample {
    int index = 0;
    bool ok = false;

    // hasValue is separate from value so a failed read can never be reported as
    // a reading of zero. Zero is a legitimate EC byte and a legitimate fan
    // command; it must not double as "unknown" (OBSERVABILITY.md section 1).
    bool hasValue = false;
    std::uint8_t value = 0;

    IoErrorCode error = IoErrorCode::None;
    std::string message;

    bool hasElapsed = false;
    std::uint64_t elapsedMs = 0;
};

struct EcDiagTargetResult {
    EcDiagTarget target;
    std::vector<EcDiagSample> samples;

    int successCount() const noexcept;
    int failureCount() const noexcept;
    bool anySucceeded() const noexcept;
    bool allSucceeded() const noexcept;

    // True when every successful sample returned the same byte. A register that
    // changes between reads taken milliseconds apart is not a stable reading,
    // and saying so is more useful than averaging it.
    bool stable() const noexcept;

    // Distinct values among the successful samples. 0 when none succeeded.
    int distinctValues() const noexcept;

    // Lowest and highest successful values. Only meaningful when successCount()
    // is greater than zero; both are 0 otherwise and hasRange is false.
    bool hasRange = false;
    std::uint8_t minimumValue = 0;
    std::uint8_t maximumValue = 0;

    // The first error reported by a failed sample, for a one-line summary. Not
    // set when every sample succeeded.
    bool hasError = false;
    IoErrorCode firstError = IoErrorCode::None;
    std::string firstErrorMessage;
};

enum class EcDiagOutcome {
    // Every requested read succeeded and the read-only invariant held.
    Ok,

    // Some reads succeeded and some failed. Still useful evidence; still not a
    // clean run.
    PartialReadFailure,

    // No read succeeded, and at least one failure was a transient or data
    // problem rather than a missing backend.
    NoReadSucceeded,

    // No read succeeded and every failure was a standing backend condition
    // (not initialized, unsupported, access denied, disconnected). This is a
    // backend or driver problem, not an EC problem, and it deserves a different
    // exit code so a script can tell them apart.
    BackendUnavailable,

    // The plan or the configuration is unusable, so nothing was read.
    ConfigInvalid,

    // The reader reported a write command byte on the status port, a data-port
    // write that was not one of the planned addresses, or a read count that
    // disagrees with the plan. A diagnostic that is not read-only has no
    // results worth reading, so this outranks every other outcome.
    InvariantViolated
};

struct EcDiagRun {
    std::string schema = kEcDiagSchema;
    std::string toolVersion = ecDiagToolVersion();

    bool hasGeneratedUtc = false;
    std::string generatedUtc;

    EcBusConfig config;
    std::string configSource;
    EcDiagBackendInfo backend;
    bool simulated = false;

    EcDiagPlan plan;
    std::vector<EcDiagTargetResult> results;

    ReaderAudit audit;

    // Non-empty only when the plan or the configuration was refused. Nothing
    // was read in that case.
    std::string configurationError;

    // Status-port writes that did not carry the read command byte. The headline
    // number of this tool: it must be zero, and a nonzero value is a defect, not
    // a finding.
    std::uint64_t writeCommandsObserved = 0;

    // Empty when the read-only invariant held.
    std::vector<std::string> invariantFailures;

    std::vector<std::string> limitations;

    EcDiagOutcome outcome = EcDiagOutcome::Ok;

    // --- derived summaries -------------------------------------------------

    std::uint64_t totalSamples = 0;
    std::uint64_t successfulSamples = 0;
    std::uint64_t failedSamples = 0;

    bool readOnlyInvariantHeld() const noexcept;
    int targetCount() const noexcept;
};

// Performs the run. Reads each target `samplesPerTarget` times, in plan order,
// and touches nothing else. Never writes, never retries beyond what the bus
// itself does, and never probes an address that is not in the plan.
EcDiagRun runDiagnostic(IRegisterReader& reader, const EcDiagPlan& plan,
                        const EcDiagOptions& options);

// ---------------------------------------------------------------------------
// Presentation
// ---------------------------------------------------------------------------

// The process exit code for an outcome, from the table in
// docs/CLI_DIAGNOSTICS.md section 3. Codes 5, 7 and 8 are unreachable by
// design: ecdiag has no control path to gate (5), no failsafe state (7) and no
// export step (8). That is stated rather than left for a reader to wonder
// about.
int exitCodeFor(EcDiagOutcome outcome) noexcept;

const char* toText(EcDiagOutcome outcome) noexcept;

// Pretty-printed JSON, one field or array element per line. Strings are escaped
// for the bytes JSON requires; unknown values are explicit null rather than
// zero.
std::string toJson(const EcDiagRun& run);

// The same content for a person: a summary line per target and a footer naming
// the configuration that produced it.
std::string toTextReport(const EcDiagRun& run);

} // namespace core
} // namespace tpfancontrol
