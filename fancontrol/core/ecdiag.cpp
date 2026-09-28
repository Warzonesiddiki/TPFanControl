#include "ecdiag.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace tpfancontrol {
namespace core {

namespace {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

const char* kHexDigits = "0123456789ABCDEF";

std::string hexByte(std::uint8_t value)
{
    std::string out = "0x";
    out.push_back(kHexDigits[(value >> 4) & 0x0F]);
    out.push_back(kHexDigits[value & 0x0F]);
    return out;
}

// Ports are 16 bits. Rendering one through hexByte would print 0x1604 as
// 0x04 - a different port, printed in the field that exists to say which port
// the readings came from.
std::string hexWord(std::uint16_t value)
{
    std::string out = "0x";
    for (int shift = 12; shift >= 0; shift -= 4) {
        out.push_back(kHexDigits[(value >> shift) & 0x0F]);
    }
    return out;
}

// JSON string escaping. Quotes, backslashes and control characters are escaped
// as JSON requires. Bytes above 0x7F are passed through untouched: they are
// already UTF-8, and escaping them byte-wise - which is what \u00XX would do -
// would turn a correctly encoded message into mojibake in every consumer.
std::string quoteJson(const std::string& text)
{
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (char raw : text) {
        const unsigned char c = static_cast<unsigned char>(raw);
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04X", static_cast<unsigned>(c));
                out += buffer;
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    out.push_back('"');
    return out;
}

// Null when the field is absent. Never an empty string standing in for unknown,
// and never a zero standing in for unknown.
std::string jsonStringOrNull(bool present, const std::string& text)
{
    return present ? quoteJson(text) : std::string("null");
}

std::string jsonNumberOrNull(bool present, std::uint64_t value)
{
    return present ? std::to_string(value) : std::string("null");
}

std::string jsonBool(bool value)
{
    return value ? "true" : "false";
}

// Hex strings for the byte arrays, so a reader does not have to re-do the
// conversion the report is supposed to be making unnecessary. Decimal values
// are in a parallel array rather than instead of it, because a regex over a
// decimal list is a poor way to check for 0x81.
std::string jsonHexByteArray(const std::vector<std::uint8_t>& values, const std::string& indent)
{
    if (values.empty()) {
        return "[]";
    }
    std::string out = "[\n";
    for (std::size_t index = 0; index < values.size(); ++index) {
        out += indent + "  " + quoteJson(hexByte(values[index]));
        out += (index + 1 < values.size()) ? ",\n" : "\n";
    }
    out += indent + "]";
    return out;
}

std::vector<std::uint8_t> distinctSorted(const std::vector<std::uint8_t>& values)
{
    std::vector<std::uint8_t> out = values;
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// One line per distinct failure class, capped: a reader that reports a
// thousand bad writes should not produce a thousand-line report, and the count
// is in the message anyway.
constexpr std::size_t kMaximumInvariantMessages = 8;

void noteInvariantFailure(EcDiagRun& run, std::size_t& suppressed, const std::string& message)
{
    if (run.invariantFailures.size() < kMaximumInvariantMessages) {
        run.invariantFailures.push_back(message);
    } else {
        ++suppressed;
    }
}

bool isStandingBackendCondition(IoErrorCode code) noexcept
{
    switch (code) {
    case IoErrorCode::NotInitialized:
    case IoErrorCode::AccessDenied:
    case IoErrorCode::Unsupported:
    case IoErrorCode::Disconnected:
    case IoErrorCode::InvalidPort:
        return true;
    case IoErrorCode::None:
    case IoErrorCode::Timeout:
    case IoErrorCode::ReadFailure:
    case IoErrorCode::WriteFailure:
        return false;
    }
    return false;
}

std::string padRight(const std::string& text, std::size_t width)
{
    if (text.size() >= width) {
        return text;
    }
    return text + std::string(width - text.size(), ' ');
}

} // namespace

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

std::string ecDiagToolVersion()
{
    return "1.0.0";
}

std::string ecDiagBuildFlavour()
{
#ifdef NDEBUG
    // Recorded because the answer changes what a report proves: a Release build
    // is the one whose assertions are compiled out, and a test suite that ran
    // under it without CHECK would have proved nothing.
    return "release";
#else
    return "debug";
#endif
}

// ---------------------------------------------------------------------------
// The plan
// ---------------------------------------------------------------------------

void addTarget(std::vector<EcDiagTarget>& targets, std::uint8_t address,
               const std::string& label)
{
    for (const EcDiagTarget& existing : targets) {
        if (existing.address == address) {
            return;
        }
    }
    EcDiagTarget target;
    target.address = address;
    target.label = label;
    targets.push_back(target);
}

void addTargetRange(std::vector<EcDiagTarget>& targets, std::uint8_t first,
                    std::uint8_t last, const std::string& label)
{
    for (unsigned value = first; value <= static_cast<unsigned>(last); ++value) {
        addTarget(targets, static_cast<std::uint8_t>(value), label);
    }
}

std::vector<EcDiagTarget> candidateT14ReadOnlyTargets()
{
    std::vector<EcDiagTarget> targets;
    addTarget(targets, 0x2F, "fan-control command (candidate)");
    addTarget(targets, 0x31, "fan selector on some dual-fan models (candidate; read only)");
    addTarget(targets, 0x84, "fan tachometer low byte (candidate)");
    addTarget(targets, 0x85, "fan tachometer high byte (candidate)");
    addTargetRange(targets, 0x78, 0x7F, "primary temperature block (candidate)");
    addTargetRange(targets, 0xC0, 0xC3, "extended temperature block (candidate)");
    return targets;
}

std::string validatePlan(const EcDiagPlan& plan)
{
    if (plan.targets.empty()) {
        return "ecdiag plan: no addresses to read. A diagnostic with an empty plan would "
               "report success having measured nothing.";
    }
    if (plan.targets.size() > kEcDiagMaximumTargets) {
        return "ecdiag plan: " + std::to_string(plan.targets.size()) + " addresses exceeds the "
               "limit of " + std::to_string(kEcDiagMaximumTargets) + ". A plan is a short list a "
               "person wrote, not a sweep of an unverified address space.";
    }
    if (plan.samplesPerTarget < 1 || plan.samplesPerTarget > kEcDiagMaximumSamplesPerTarget) {
        return "ecdiag plan: samplesPerTarget " + std::to_string(plan.samplesPerTarget)
            + " is outside 1.." + std::to_string(kEcDiagMaximumSamplesPerTarget);
    }
    return std::string();
}

// ---------------------------------------------------------------------------
// Target result summaries
// ---------------------------------------------------------------------------

int EcDiagTargetResult::successCount() const noexcept
{
    int count = 0;
    for (const EcDiagSample& sample : samples) {
        if (sample.ok) {
            ++count;
        }
    }
    return count;
}

int EcDiagTargetResult::failureCount() const noexcept
{
    return static_cast<int>(samples.size()) - successCount();
}

bool EcDiagTargetResult::anySucceeded() const noexcept
{
    return successCount() > 0;
}

bool EcDiagTargetResult::allSucceeded() const noexcept
{
    return !samples.empty() && failureCount() == 0;
}

bool EcDiagTargetResult::stable() const noexcept
{
    // Every sample must have succeeded AND every value must agree. A target
    // that failed one read out of three is not called stable, because the
    // missing sample is exactly the one that could have differed.
    if (!allSucceeded()) {
        return false;
    }
    const std::uint8_t first = samples.front().value;
    for (const EcDiagSample& sample : samples) {
        if (sample.value != first) {
            return false;
        }
    }
    return true;
}

int EcDiagTargetResult::distinctValues() const noexcept
{
    std::vector<std::uint8_t> values;
    for (const EcDiagSample& sample : samples) {
        if (sample.ok) {
            values.push_back(sample.value);
        }
    }
    if (values.empty()) {
        return 0;
    }
    values = distinctSorted(values);
    return static_cast<int>(values.size());
}

bool EcDiagRun::readOnlyInvariantHeld() const noexcept
{
    return invariantFailures.empty();
}

int EcDiagRun::targetCount() const noexcept
{
    return static_cast<int>(results.size());
}

// ---------------------------------------------------------------------------
// The run
// ---------------------------------------------------------------------------

EcDiagRun runDiagnostic(IRegisterReader& reader, const EcDiagPlan& plan,
                        const EcDiagOptions& options)
{
    EcDiagRun run;
    run.schema = kEcDiagSchema;
    run.toolVersion = ecDiagToolVersion();
    run.hasGeneratedUtc = !options.generatedUtc.empty();
    run.generatedUtc = options.generatedUtc;
    run.config = options.config;
    run.configSource = options.configSource;
    run.backend = options.backend;
    run.simulated = options.simulated;
    run.plan = plan;
    run.limitations = options.limitations;

    // Refuse before reading. A refused plan must not produce a half-filled
    // results array that a reader could mistake for a partial success.
    const std::string planError = validatePlan(plan);
    if (!planError.empty()) {
        run.configurationError = planError;
        run.outcome = EcDiagOutcome::ConfigInvalid;
        return run;
    }
    const std::string configError = options.config.validationMessage();
    if (!configError.empty()) {
        run.configurationError = configError;
        run.outcome = EcDiagOutcome::ConfigInvalid;
        return run;
    }

    const bool timed = static_cast<bool>(options.nowMs);

    for (const EcDiagTarget& target : plan.targets) {
        EcDiagTargetResult result;
        result.target = target;
        result.samples.reserve(static_cast<std::size_t>(plan.samplesPerTarget));

        for (int index = 0; index < plan.samplesPerTarget; ++index) {
            EcDiagSample sample;
            sample.index = index;

            const std::uint64_t started = timed ? options.nowMs() : 0;
            const IoResult io = reader.readRegister(target.address);
            const std::uint64_t finished = timed ? options.nowMs() : 0;

            sample.ok = io.ok;
            sample.error = io.error;
            sample.message = io.message;
            if (io.ok) {
                // Value and "there is a value" are recorded separately. Zero is
                // a legitimate EC byte and must not double as "unknown".
                sample.hasValue = true;
                sample.value = io.value;
            }
            if (timed) {
                sample.hasElapsed = true;
                // A clock that went backwards is not evidence of a negative
                // duration; the sample reports zero and the run continues.
                sample.elapsedMs = finished >= started ? finished - started : 0;
            }

            if (io.ok) {
                ++run.successfulSamples;
            } else {
                ++run.failedSamples;
                if (!result.hasError) {
                    result.hasError = true;
                    result.firstError = io.error;
                    result.firstErrorMessage = io.message;
                }
            }
            ++run.totalSamples;
            result.samples.push_back(sample);
        }

        // Range over the successful samples only. A failed read contributes no
        // value, so it cannot widen a range it is not part of.
        bool hasRange = false;
        std::uint8_t minimum = 0;
        std::uint8_t maximum = 0;
        for (const EcDiagSample& sample : result.samples) {
            if (!sample.ok) {
                continue;
            }
            if (!hasRange) {
                hasRange = true;
                minimum = sample.value;
                maximum = sample.value;
                continue;
            }
            minimum = std::min(minimum, sample.value);
            maximum = std::max(maximum, sample.value);
        }
        result.hasRange = hasRange;
        result.minimumValue = minimum;
        result.maximumValue = maximum;

        run.results.push_back(result);
    }

    // -----------------------------------------------------------------------
    // The audit, and the read-only invariant
    // -----------------------------------------------------------------------
    //
    // Everything below is a cross-check between what the tool asked for and
    // what the reader says actually reached the port primitives. Each check can
    // fire in a realistic scenario, which is the only reason to have it: a
    // guard that cannot fire reads as protection while the tree is unprotected.
    run.audit = reader.audit();

    std::vector<bool> plannedAddress(256, false);
    for (const EcDiagTarget& target : plan.targets) {
        plannedAddress[target.address] = true;
    }

    std::size_t suppressed = 0;

    // 1. The status port is where the command byte goes. A read puts the read
    //    command there; a write would put the write command there. Anything
    //    that is not the read command means this run was not read-only.
    for (std::uint8_t value : run.audit.statusPortWriteValues) {
        if (value != run.config.readCommand) {
            ++run.writeCommandsObserved;
            noteInvariantFailure(run, suppressed,
                "the status port was written with " + hexByte(value)
                + ", which is not the read command " + hexByte(run.config.readCommand)
                + " - a register write reached the bus during a diagnostic run");
        }
    }

    // 2. The data port carries the register address on a read, and the address
    //    followed by the value on a write. An address the plan never named means
    //    either an unplanned read or a write to something else.
    for (std::uint8_t value : run.audit.dataPortWriteValues) {
        if (!plannedAddress[value]) {
            noteInvariantFailure(run, suppressed,
                "the data port was written with " + hexByte(value)
                + ", which is not one of the planned addresses");
        }
    }

    // 3. Every data-port write follows a command write. More data writes than
    //    command writes means a byte reached the data port with no command
    //    before it, which is not a transaction this protocol defines.
    if (run.audit.dataPortWriteValues.size() > run.audit.statusPortWriteValues.size()) {
        noteInvariantFailure(run, suppressed,
            "the data port was written " + std::to_string(run.audit.dataPortWriteValues.size())
            + " time(s) after only "
            + std::to_string(run.audit.statusPortWriteValues.size())
            + " command write(s); a data byte with no command before it is not a read");
    }

    // 4. The reader's own count of the calls it received against the plan. A
    //    reader that swallows reads would otherwise be reported as a run where
    //    every target silently had no samples.
    const std::uint64_t expectedReads =
        static_cast<std::uint64_t>(plan.targets.size())
        * static_cast<std::uint64_t>(plan.samplesPerTarget);
    if (run.audit.readsIssued != expectedReads) {
        noteInvariantFailure(run, suppressed,
            "the reader received " + std::to_string(run.audit.readsIssued)
            + " read(s) but the plan called for " + std::to_string(expectedReads));
    }

    if (suppressed != 0) {
        run.invariantFailures.push_back(
            std::to_string(suppressed) + " further invariant finding(s) not listed");
    }

    // -----------------------------------------------------------------------
    // Outcome
    // -----------------------------------------------------------------------
    if (!run.invariantFailures.empty()) {
        // A diagnostic that was not read-only has no results worth reading, so
        // this outranks every other outcome including a complete set of reads.
        run.outcome = EcDiagOutcome::InvariantViolated;
    } else if (run.failedSamples == 0) {
        run.outcome = EcDiagOutcome::Ok;
    } else if (run.successfulSamples == 0) {
        bool allStanding = true;
        for (const EcDiagTargetResult& result : run.results) {
            for (const EcDiagSample& sample : result.samples) {
                if (!sample.ok && !isStandingBackendCondition(sample.error)) {
                    allStanding = false;
                }
            }
        }
        run.outcome = allStanding ? EcDiagOutcome::BackendUnavailable
                                  : EcDiagOutcome::NoReadSucceeded;
    } else {
        run.outcome = EcDiagOutcome::PartialReadFailure;
    }

    // -----------------------------------------------------------------------
    // Limitations, stated rather than implied
    // -----------------------------------------------------------------------
    run.limitations.push_back(
        "ecdiag performs reads only. It does not establish that a value read from an "
        "offset means what that offset's candidate label says.");
    run.limitations.push_back(
        "Values are raw EC bytes. Correlating one with an independent measurement is a "
        "human step (docs/HARDWARE_VERIFICATION.md section 7); a plausible value is not "
        "proof on its own.");
    if (run.configSource.rfind("default", 0) == 0) {
        run.limitations.push_back(
            "The port mapping and command encoding are unverified candidate values "
            "transcribed from fancontrol/portio.cpp, not measurements of this machine "
            "(docs/EC_REGISTER_MAP.md section 3.1).");
    }
    if (run.simulated) {
        run.limitations.push_back(
            "The values in this report were not read from hardware. A simulated run "
            "checks the tool's contract and is not hardware evidence.");
    }
    if (!run.backend.canWritePorts && run.failedSamples != 0) {
        run.limitations.push_back(
            "The backend does not report port-write capability, and the EC protocol writes "
            "a command byte to the status port even to read (ADR-023), so a backend that "
            "cannot write ports cannot read either.");
    }
    if (run.simulated) {
        run.limitations.push_back(
            "No register write path exists in this tool: it is handed an interface with a "
            "single read operation, and the report above is a cross-check against that "
            "interface's own audit rather than against a second implementation.");
    }

    return run;
}

// ---------------------------------------------------------------------------
// Presentation
// ---------------------------------------------------------------------------

int exitCodeFor(EcDiagOutcome outcome) noexcept
{
    switch (outcome) {
    case EcDiagOutcome::Ok:
        return 0;
    case EcDiagOutcome::ConfigInvalid:
        return 3;
    case EcDiagOutcome::BackendUnavailable:
        return 4;
    case EcDiagOutcome::NoReadSucceeded:
    case EcDiagOutcome::PartialReadFailure:
        return 6;
    case EcDiagOutcome::InvariantViolated:
        // Not 6: this is not a problem with the data, it is a problem with the
        // tool. A script must be able to tell "the EC did not answer" from "the
        // diagnostic wrote something", and only one of those is an emergency.
        return 1;
    }
    return 1;
}

const char* toText(EcDiagOutcome outcome) noexcept
{
    switch (outcome) {
    case EcDiagOutcome::Ok:                 return "ok";
    case EcDiagOutcome::PartialReadFailure: return "partial_read_failure";
    case EcDiagOutcome::NoReadSucceeded:    return "no_read_succeeded";
    case EcDiagOutcome::BackendUnavailable: return "backend_unavailable";
    case EcDiagOutcome::ConfigInvalid:      return "config_invalid";
    case EcDiagOutcome::InvariantViolated:  return "read_only_invariant_violated";
    }
    return "unknown";
}

namespace {

const char* backendStateName(BackendState state) noexcept
{
    switch (state) {
    case BackendState::Unavailable: return "Unavailable";
    case BackendState::Ready:       return "Ready";
    case BackendState::Faulted:     return "Faulted";
    case BackendState::Stopped:     return "Stopped";
    }
    return "Unknown";
}

std::string sampleValueJson(const EcDiagSample& sample)
{
    return jsonNumberOrNull(sample.hasValue, sample.value);
}

std::string sampleValueHexJson(const EcDiagSample& sample)
{
    return sample.hasValue ? quoteJson(hexByte(sample.value)) : std::string("null");
}

} // namespace

std::string toJson(const EcDiagRun& run)
{
    std::string out;
    out += "{\n";
    out += "  \"schema\": " + quoteJson(run.schema) + ",\n";

    out += "  \"tool\": {\n";
    out += "    \"name\": \"ecdiag\",\n";
    out += "    \"version\": " + quoteJson(run.toolVersion) + ",\n";
    out += "    \"build_flavour\": " + quoteJson(ecDiagBuildFlavour()) + ",\n";
    out += "    \"generated_utc\": " + jsonStringOrNull(run.hasGeneratedUtc, run.generatedUtc) + ",\n";
    out += "    \"purpose\": \"read-only embedded-controller diagnostic; performs no register writes\"\n";
    out += "  },\n";

    out += "  \"evidence\": {\n";
    out += "    \"grade\": " + std::string(run.simulated ? "\"simulated\"" : "\"hardware\"") + ",\n";
    out += "    \"not_evidence_reason\": "
        + (run.simulated
               ? quoteJson("the values were produced by a simulated reader, not read from hardware")
               : std::string("null"))
        + "\n";
    out += "  },\n";

    out += "  \"outcome\": " + quoteJson(toText(run.outcome)) + ",\n";
    out += "  \"exit_code\": " + std::to_string(exitCodeFor(run.outcome)) + ",\n";
    out += "  \"configuration_error\": "
        + jsonStringOrNull(!run.configurationError.empty(), run.configurationError) + ",\n";

    out += "  \"config\": {\n";
    out += "    \"status_port\": " + std::to_string(run.config.statusPort) + ",\n";
    out += "    \"status_port_hex\": " + quoteJson(hexWord(run.config.statusPort)) + ",\n";
    out += "    \"data_port\": " + std::to_string(run.config.dataPort) + ",\n";
    out += "    \"data_port_hex\": " + quoteJson(hexWord(run.config.dataPort)) + ",\n";
    out += "    \"ibf_mask\": " + quoteJson(hexByte(run.config.ibfMask)) + ",\n";
    out += "    \"obf_mask\": " + quoteJson(hexByte(run.config.obfMask)) + ",\n";
    out += "    \"read_command\": " + quoteJson(hexByte(run.config.readCommand)) + ",\n";
    out += "    \"write_command\": " + quoteJson(hexByte(run.config.writeCommand)) + ",\n";
    out += "    \"timeout_ms\": " + std::to_string(run.config.timeoutMs) + ",\n";
    out += "    \"poll_interval_ms\": " + std::to_string(run.config.pollIntervalMs) + ",\n";
    out += "    \"maximum_attempts\": " + std::to_string(run.config.maximumAttempts) + ",\n";
    out += "    \"recovery_timeout_ms\": " + std::to_string(run.config.recoveryTimeoutMs) + ",\n";
    out += "    \"source\": " + quoteJson(run.configSource) + ",\n";
    // Always this value. The tool cannot know a mapping is verified, and it
    // deliberately offers no switch that would let an operator assert it: a
    // profile is not verified because it says so, and neither is a diagnostic.
    out += "    \"encoding_status\": \"unverified-candidate\",\n";
    out += "    \"sane\": " + jsonBool(run.config.isSane()) + ",\n";
    out += "    \"validation_message\": " + jsonStringOrNull(!run.config.isSane(), run.config.validationMessage()) + "\n";
    out += "  },\n";

    out += "  \"backend\": {\n";
    out += "    \"name\": " + quoteJson(run.backend.name) + ",\n";
    out += "    \"version\": " + jsonStringOrNull(!run.backend.version.empty(), run.backend.version) + ",\n";
    out += "    \"state\": " + quoteJson(backendStateName(run.backend.state)) + ",\n";
    out += std::string("    \"capabilities\": ") + (run.backend.haveCapabilities ? "{" : "null") + "\n";
    if (run.backend.haveCapabilities) {
        out += "      \"can_read_ports\": " + jsonBool(run.backend.canReadPorts) + ",\n";
        out += "      \"can_write_ports\": " + jsonBool(run.backend.canWritePorts) + ",\n";
        out += "      \"can_read_msr\": " + jsonBool(run.backend.canReadMsr) + ",\n";
        out += "      \"signed_and_approved\": " + jsonBool(run.backend.signedAndApproved) + "\n";
        out += "    }\n";
    }
    out += "  },\n";

    out += "  \"plan\": {\n";
    out += "    \"samples_per_target\": " + std::to_string(run.plan.samplesPerTarget) + ",\n";
    out += "    \"target_count\": " + std::to_string(run.plan.targets.size()) + ",\n";
    out += "    \"targets\": [\n";
    for (std::size_t index = 0; index < run.plan.targets.size(); ++index) {
        const EcDiagTarget& target = run.plan.targets[index];
        out += "      {\"address\": " + quoteJson(hexByte(target.address))
            + ", \"address_decimal\": " + std::to_string(target.address)
            + ", \"candidate_meaning\": " + quoteJson(target.label) + "}";
        out += (index + 1 < run.plan.targets.size()) ? ",\n" : "\n";
    }
    out += "    ]\n";
    out += "  },\n";

    out += "  \"summary\": {\n";
    out += "    \"targets\": " + std::to_string(run.results.size()) + ",\n";
    out += "    \"samples\": " + std::to_string(run.totalSamples) + ",\n";
    out += "    \"successful\": " + std::to_string(run.successfulSamples) + ",\n";
    out += "    \"failed\": " + std::to_string(run.failedSamples) + "\n";
    out += "  },\n";

    out += "  \"reads\": [\n";
    for (std::size_t index = 0; index < run.results.size(); ++index) {
        const EcDiagTargetResult& result = run.results[index];
        out += "    {\n";
        out += "      \"address\": " + quoteJson(hexByte(result.target.address)) + ",\n";
        out += "      \"address_decimal\": " + std::to_string(result.target.address) + ",\n";
        out += "      \"candidate_meaning\": " + quoteJson(result.target.label) + ",\n";
        out += "      \"ok\": " + jsonBool(result.allSucceeded()) + ",\n";
        out += "      \"success_count\": " + std::to_string(result.successCount()) + ",\n";
        out += "      \"failure_count\": " + std::to_string(result.failureCount()) + ",\n";
        out += "      \"stable\": " + jsonBool(result.stable()) + ",\n";
        out += "      \"distinct_values\": " + jsonNumberOrNull(result.anySucceeded(),
            static_cast<std::uint64_t>(result.distinctValues())) + ",\n";
        // A failed read reports null, never 0: 0 is a legitimate byte.
        out += "      \"value\": " + jsonNumberOrNull(result.stable() && result.hasRange,
            result.minimumValue) + ",\n";
        out += "      \"value_hex\": "
            + ((result.stable() && result.hasRange) ? quoteJson(hexByte(result.minimumValue))
                                                    : std::string("null")) + ",\n";
        out += "      \"minimum_value\": " + jsonNumberOrNull(result.hasRange, result.minimumValue) + ",\n";
        out += "      \"maximum_value\": " + jsonNumberOrNull(result.hasRange, result.maximumValue) + ",\n";
        out += "      \"error\": " + jsonStringOrNull(result.hasError, toText(result.firstError)) + ",\n";
        out += "      \"message\": " + jsonStringOrNull(!result.firstErrorMessage.empty(), result.firstErrorMessage) + ",\n";
        out += "      \"samples\": [\n";
        for (std::size_t sampleIndex = 0; sampleIndex < result.samples.size(); ++sampleIndex) {
            const EcDiagSample& sample = result.samples[sampleIndex];
            out += "        {\"index\": " + std::to_string(sample.index)
                + ", \"ok\": " + jsonBool(sample.ok)
                + ", \"value\": " + sampleValueJson(sample)
                + ", \"value_hex\": " + sampleValueHexJson(sample)
                + ", \"error\": " + jsonStringOrNull(!sample.ok, toText(sample.error))
                + ", \"message\": " + jsonStringOrNull(!sample.message.empty(), sample.message)
                + ", \"elapsed_ms\": " + jsonNumberOrNull(sample.hasElapsed, sample.elapsedMs)
                + "}";
            out += (sampleIndex + 1 < result.samples.size()) ? ",\n" : "\n";
        }
        out += "      ]\n";
        out += "    }";
        out += (index + 1 < run.results.size()) ? ",\n" : "\n";
    }
    out += "  ],\n";

    out += "  \"audit\": {\n";
    out += "    \"reads_received_by_the_reader\": " + std::to_string(run.audit.readsIssued) + ",\n";
    out += "    \"bus_transactions\": " + std::to_string(run.audit.transactionCount) + ",\n";
    out += "    \"bus_timeouts\": " + std::to_string(run.audit.timeoutCount) + ",\n";
    out += "    \"status_port_write_count\": " + std::to_string(run.audit.statusPortWriteValues.size()) + ",\n";
    out += "    \"status_port_write_values_distinct\": "
        + jsonHexByteArray(distinctSorted(run.audit.statusPortWriteValues), "    ") + ",\n";
    out += "    \"data_port_write_count\": " + std::to_string(run.audit.dataPortWriteValues.size()) + ",\n";
    out += "    \"data_port_write_values\": "
        + jsonHexByteArray(run.audit.dataPortWriteValues, "    ") + "\n";
    out += "  },\n";

    out += "  \"read_only_invariant\": {\n";
    out += "    \"status\": " + std::string(run.readOnlyInvariantHeld() ? "\"held\"" : "\"violated\"") + ",\n";
    out += "    \"write_commands_observed\": " + std::to_string(run.writeCommandsObserved) + ",\n";
    out += "    \"failures\": [\n";
    for (std::size_t index = 0; index < run.invariantFailures.size(); ++index) {
        out += "      " + quoteJson(run.invariantFailures[index]);
        out += (index + 1 < run.invariantFailures.size()) ? ",\n" : "\n";
    }
    out += "    ]\n";
    out += "  },\n";

    out += "  \"limitations\": [\n";
    for (std::size_t index = 0; index < run.limitations.size(); ++index) {
        out += "    " + quoteJson(run.limitations[index]);
        out += (index + 1 < run.limitations.size()) ? ",\n" : "\n";
    }
    out += "  ]\n";
    out += "}\n";
    return out;
}

std::string toTextReport(const EcDiagRun& run)
{
    std::string out;
    out += "ecdiag " + run.toolVersion + " - read-only EC diagnostic ("
        + ecDiagBuildFlavour() + " build)\n";
    out += "generated: "
        + (run.hasGeneratedUtc ? run.generatedUtc : std::string("not recorded")) + "\n";
    if (run.simulated) {
        out += "MODE: SIMULATED - the values below were not read from hardware and are "
               "not evidence\n";
    } else {
        out += "MODE: hardware backend\n";
    }

    out += "backend: " + run.backend.name
        + (run.backend.version.empty() ? std::string() : " " + run.backend.version)
        + " (" + backendStateName(run.backend.state)
        + (run.backend.haveCapabilities
               ? ", read ports " + std::string(run.backend.canReadPorts ? "yes" : "no")
                     + ", write ports " + std::string(run.backend.canWritePorts ? "yes" : "no")
               : ", capabilities not reported")
        + ")\n";

    out += "config: status " + hexWord(run.config.statusPort)
        + ", data " + hexWord(run.config.dataPort)
        + ", read command " + hexByte(run.config.readCommand)
        + ", write command " + hexByte(run.config.writeCommand)
        + ", timeout " + std::to_string(run.config.timeoutMs) + " ms"
        + ", poll " + std::to_string(run.config.pollIntervalMs) + " ms"
        + ", attempts " + std::to_string(run.config.maximumAttempts) + "\n";
    out += "        source: " + run.configSource + " (unverified candidate encoding)\n\n";

    out += padRight("address", 9) + padRight("candidate meaning", 56)
        + padRight("values", 22) + "stable\n";

    for (const EcDiagTargetResult& result : run.results) {
        std::string values;
        if (!result.anySucceeded()) {
            values = "no reading";
        } else if (result.stable()) {
            values = hexByte(result.minimumValue) + " x" + std::to_string(result.successCount());
        } else {
            values = std::to_string(result.distinctValues()) + " distinct /"
                + std::to_string(result.samples.size());
        }
        std::string stable = "no";
        if (result.stable()) {
            stable = "yes";
        } else if (!result.anySucceeded() && result.hasError) {
            stable = std::string(toText(result.firstError));
        } else if (!result.allSucceeded()) {
            stable = "partial";
        } else {
            stable = "unstable";
        }
        out += padRight(hexByte(result.target.address), 9)
            + padRight(result.target.label, 56)
            + padRight(values, 22) + stable + "\n";
    }

    out += "\naudit: " + std::to_string(run.audit.readsIssued) + " read(s) received by the reader, "
        + std::to_string(run.audit.transactionCount) + " bus transaction(s), "
        + std::to_string(run.audit.timeoutCount) + " timeout(s), "
        + std::to_string(run.audit.statusPortWriteValues.size()) + " status-port write(s), "
        + std::to_string(run.audit.dataPortWriteValues.size()) + " data-port write(s)\n";
    out += "read-only invariant: "
        + std::string(run.readOnlyInvariantHeld() ? "held" : "VIOLATED")
        + " (" + std::to_string(run.writeCommandsObserved) + " write command(s) observed)\n";
    for (const std::string& failure : run.invariantFailures) {
        out += "  ! " + failure + "\n";
    }

    out += "result: " + std::to_string(run.successfulSamples) + "/"
        + std::to_string(run.totalSamples) + " reading(s) succeeded, "
        + std::to_string(run.failedSamples) + " failed\n";
    if (!run.configurationError.empty()) {
        out += "configuration refused: " + run.configurationError + "\n";
    }
    out += "outcome: " + std::string(toText(run.outcome))
        + " (exit " + std::to_string(exitCodeFor(run.outcome)) + ")\n";

    if (!run.limitations.empty()) {
        out += "\nlimitations:\n";
        for (const std::string& limitation : run.limitations) {
            out += "  - " + limitation + "\n";
        }
    }
    return out;
}

} // namespace core
} // namespace tpfancontrol
