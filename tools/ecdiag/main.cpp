// ecdiag - the command-line front end for the read-only EC diagnostic (T5-09).
//
// What this program can do here
// -----------------------------
// There is no hardware backend yet. T5-01 (a TVicPort adapter) and T5-02 (the
// PawnIO spike) are both open, so on a real laptop this build has nothing to
// read through. Rather than pretend otherwise, every backend name is answered
// with the truth about why it is unavailable and exit code 4 - and two modes
// remain fully usable:
//
//   --plan      records the proposed plan and the configuration, performing no
//               I/O at all. This is what makes the tool reviewable before it is
//               pointed at a machine, and it is the mode a hardware report can
//               cite for "this is exactly what was going to be read, through
//               this mapping".
//   --simulate  runs the whole pipeline against an invented register table, so
//               the argument handling, the report writer and the exit codes can
//               be exercised end to end. Every report it produces is stamped
//               simulated and says so in a field a machine can read.
//
// The tool never writes. That is not a property of this file: the engine is
// handed a core::IRegisterReader, which has one read operation and no write
// member, and this file has no way to construct anything else.
//
// Usage:       ecdiag [options]
// Exit codes:  0 completed successfully (including --plan)
//              1 general error, including the read-only invariant being
//                violated - a defect in the tool, not a finding about the EC
//              2 invalid command-line arguments
//              3 configuration or plan invalid
//              4 backend unavailable or unsupported
//              6 sensor data unavailable or invalid (some or all reads failed)
//              8 the report could not be written to --output
//
// Codes 5 (hardware identity not verified) and 7 (safety state active) are
// unreachable here by design: ecdiag has no control path to gate and no failsafe
// state. See docs/ECDIAG.md.

#include "simulated_ec.h"

#include "ecdiag.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace {

using tpfancontrol::core::EcBusConfig;
using tpfancontrol::core::EcDiagOptions;
using tpfancontrol::core::EcDiagPlan;
using tpfancontrol::core::EcDiagRun;
using tpfancontrol::core::EcDiagTarget;

// The exit codes this front end can produce, named so the mapping is visible in
// one place rather than scattered through the argument handling. The numbers
// are the table in docs/CLI_DIAGNOSTICS.md section 3.
enum ExitCode {
    kOk = 0,
    kGeneralError = 1,
    kUsage = 2,
    kConfigInvalid = 3,
    kBackendUnavailable = 4,
    kSensorInvalid = 6,
    kExportFailed = 8
};

const char* const kUsageText =
    "ecdiag - read-only EC diagnostic (TPFanControl, T5-09)\n"
    "\n"
    "Reads a fixed list of candidate embedded-controller offsets and writes a\n"
    "machine-readable report. It performs no register writes, and it has no\n"
    "option that would allow one.\n"
    "\n"
    "Modes (default: --plan)\n"
    "  --plan                record the plan and configuration; perform no I/O\n"
    "  --simulate            run against an invented register table. NOT evidence\n"
    "  --backend <name>      hardware backend to use. None is available in this\n"
    "                        build; each name explains why and exits 4.\n"
    "                        Names: tvicport, pawnio, none\n"
    "\n"
    "Plan\n"
    "  --address <byte>      add an offset (0x2F or 47); repeatable\n"
    "  --range <first-last>  add an inclusive range (0x78-0x7F); repeatable\n"
    "  --candidates          use the documented candidate set (the default when\n"
    "                        no --address or --range is given)\n"
    "  --samples <n>         reads per offset (default 3, 1..32)\n"
    "  --list-candidates     print the candidate offsets and exit\n"
    "\n"
    "Configuration - recorded in the report. The defaults are CANDIDATE values\n"
    "transcribed from the legacy source, not measurements of any machine.\n"
    "  --status-port <port>   default 0x1604      --data-port <port>   default 0x1600\n"
    "  --read-command <byte>  default 0x80        --write-command <byte> default 0x81\n"
    "  --ibf-mask <byte>      default 0x02        --obf-mask <byte>    default 0x01\n"
    "  --timeout-ms <n>       default 50          --poll-ms <n>        default 1\n"
    "  --attempts <n>         default 3           --recovery-ms <n>    default 100\n"
    "\n"
    "Output\n"
    "  --json                write the machine-readable report to stdout\n"
    "  --output <file>       also write the report to this file (JSON)\n"
    "  --force               allow --output to replace an existing file\n"
    "  --quiet               suppress the human-readable report\n"
    "  --version             print the version and exit\n"
    "  --help                print this text and exit\n"
    "\n"
    "Exit codes: 0 ok, 1 general error, 2 bad arguments, 3 invalid configuration,\n"
    "            4 backend unavailable, 6 sensor data unavailable, 8 report not\n"
    "            written. Codes 5 and 7 are unreachable: there is no control path\n"
    "            to gate and no failsafe state to report.\n";

void fail(const std::string& message)
{
    std::fprintf(stderr, "ecdiag: %s\n", message.c_str());
}

// Accepts "0x2F" (hex with a prefix) or "47" (decimal). Returns false for
// anything else, including an empty string and an out-of-range value: a typo
// must be refused rather than silently read as zero, and a byte that does not
// fit must be refused rather than truncated.
bool parseUnsigned(const std::string& text, unsigned long maximum, unsigned long& out)
{
    if (text.empty()) {
        return false;
    }
    std::size_t index = 0;
    unsigned long value = 0;
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        index = 2;
    }
    for (; index < text.size(); ++index) {
        const char c = text[index];
        unsigned digit = 0;
        if (c >= '0' && c <= '9') {
            digit = static_cast<unsigned>(c - '0');
        } else if (base == 16 && c >= 'a' && c <= 'f') {
            digit = static_cast<unsigned>(c - 'a') + 10u;
        } else if (base == 16 && c >= 'A' && c <= 'F') {
            digit = static_cast<unsigned>(c - 'A') + 10u;
        } else {
            return false;
        }
        value = value * static_cast<unsigned long>(base) + digit;
        if (value > maximum) {
            return false;
        }
    }
    out = value;
    return true;
}

// A UTC timestamp for the report. Empty when the platform refuses to supply
// one, which the engine reports as null rather than inventing a time.
std::string utcNow()
{
    const std::time_t now = std::time(nullptr);
#if defined(_MSC_VER)
    std::tm parts;
    if (gmtime_s(&parts, &now) != 0) {
        return std::string();
    }
    const std::tm* broken = &parts;
#else
    const std::tm* broken = std::gmtime(&now);
    if (broken == nullptr) {
        return std::string();
    }
#endif
    char buffer[32];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", broken) == 0) {
        return std::string();
    }
    return std::string(buffer);
}

bool writeFile(const std::string& path, const std::string& content, bool force,
               std::string& error)
{
    // The path is opened directly and never handed to a shell, as
    // CLI_DIAGNOSTICS.md section 4 requires. There is no command construction
    // anywhere in this file, so there is nothing for a path to escape into.
    if (!force) {
        std::FILE* existing = std::fopen(path.c_str(), "rb");
        if (existing != nullptr) {
            std::fclose(existing);
            error = path + " already exists; pass --force to replace it";
            return false;
        }
    }
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        error = path + " could not be opened for writing";
        return false;
    }
    const std::size_t written = content.empty()
        ? 0u
        : std::fwrite(content.data(), 1u, content.size(), file);
    const int closed = std::fclose(file);
    if (written != content.size() || closed != 0) {
        error = path + " could not be written completely";
        return false;
    }
    return true;
}

// A reader that must never be called.
//
// `--plan` needs no reader at all, but the engine's signature takes one. This
// is that argument, and it is deliberately not a stub that returns success: if
// the plan-only path ever started reading, every one of these calls would fail
// and the run's outcome would stop being PlanOnly. The test suite asserts
// exactly that (`readsIssued == 0`, outcome PlanOnly), so a regression here is
// a failing test rather than a silent change of meaning.
class NeverCalledReader final : public tpfancontrol::core::IRegisterReader {
public:
    tpfancontrol::core::IoResult readRegister(std::uint8_t) override
    {
        ++reads_;
        return tpfancontrol::core::IoResult::failure(
            tpfancontrol::core::IoErrorCode::NotInitialized,
            "a plan-only run has no backend and must not read");
    }

    tpfancontrol::core::ReaderAudit audit() const override
    {
        tpfancontrol::core::ReaderAudit out;
        out.readsIssued = reads_;
        return out;
    }

private:
    std::uint64_t reads_ = 0;
};

struct Settings {
    bool help = false;
    bool version = false;
    bool listCandidates = false;
    bool simulate = false;
    bool json = false;
    bool quiet = false;
    bool force = false;
    bool candidatesRequested = false;
    bool anyExplicitAddress = false;
    std::string backend;
    std::string output;
    EcBusConfig config;
    EcDiagPlan plan;
};

// Explains why a named backend is not usable here. The answer is specific
// rather than a shrug: a person who runs this on the target machine needs to
// know which task is missing, not merely that "no backend".
std::string backendUnavailableReason(const std::string& name)
{
    if (name == "tvicport") {
        return "the TVicPort adapter is T5-01 and is not implemented. It needs Windows and "
               "the vendor driver, and it must be verified against the target machine "
               "before it can be used.";
    }
    if (name == "pawnio") {
        return "the PawnIO backend is T5-02, which is blocked: it needs a disposable "
               "Windows install, a driver download and an HVCI-capable machine. No "
               "substitute evidence is acceptable, so there is nothing to bind to yet.";
    }
    if (name == "none") {
        return "no backend was requested. Pass --simulate to exercise the tool, or --plan "
               "to record the configuration a hardware run would use.";
    }
    return "unknown backend '" + name + "'. Known names: tvicport, pawnio, none.";
}

int emit(const Settings& settings, const EcDiagRun& run)
{
    if (!settings.quiet && !settings.json) {
        std::fputs(tpfancontrol::core::toTextReport(run).c_str(), stdout);
    }
    if (settings.json) {
        std::fputs(tpfancontrol::core::toJson(run).c_str(), stdout);
    }
    if (!settings.output.empty()) {
        std::string error;
        if (!writeFile(settings.output, tpfancontrol::core::toJson(run), settings.force, error)) {
            fail(error);
            return kExportFailed;
        }
        std::fprintf(stderr, "ecdiag: report written to %s\n", settings.output.c_str());
    }
    return tpfancontrol::core::exitCodeFor(run.outcome);
}

int runPlanOnly(const Settings& settings)
{
    NeverCalledReader reader;
    EcDiagOptions options;
    options.config = settings.config;
    options.planOnly = true;
    options.generatedUtc = utcNow();
    options.backend.name = "none";
    options.backend.state = tpfancontrol::core::BackendState::Unavailable;
    options.limitations.push_back(
        "No backend is selected in this build, so a run of this plan could not be "
        "performed yet even with --backend.");

    return emit(settings, tpfancontrol::core::runDiagnostic(reader, settings.plan, options));
}

int runSimulated(const Settings& settings)
{
    tpfancontrol::ecdiag::SimulatedEc reader;
    EcDiagOptions options;
    options.config = settings.config;
    options.simulated = true;
    options.generatedUtc = utcNow();
    options.backend.name = "simulated";
    options.backend.version = tpfancontrol::core::ecDiagToolVersion();
    options.backend.haveCapabilities = false;   // there is no port backend to describe
    options.backend.state = tpfancontrol::core::BackendState::Ready;
    options.limitations.push_back(
        "This run used the simulated reader: an invented register table with no ports "
        "behind it. It proves that the tool and its reports work. It says nothing about "
        "any machine.");

    return emit(settings, tpfancontrol::core::runDiagnostic(reader, settings.plan, options));
}

void printCandidates()
{
    const std::vector<EcDiagTarget> targets = tpfancontrol::core::candidateT14ReadOnlyTargets();
    std::printf("Candidate offsets from docs/EC_REGISTER_MAP.md section 2.\n");
    std::printf("Every one of these is a HYPOTHESIS. None has been measured on the target\n");
    std::printf("machine, and a value read from an offset does not confirm that the offset\n");
    std::printf("means what its label says.\n\n");
    for (const EcDiagTarget& target : targets) {
        std::printf("  0x%02X  %s\n", static_cast<unsigned>(target.address), target.label.c_str());
    }
    std::printf("\nOffsets outside this list may be added explicitly with --address.\n");
    std::printf("There is deliberately no option that reads a whole address space at once.\n");
}

} // namespace

int main(int argc, char** argv)
{
    Settings settings;
    settings.plan.samplesPerTarget = 3;

    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
    for (int index = 1; index < argc; ++index) {
        arguments.push_back(std::string(argv[index]));
    }

    // Every option that takes a value is resolved here, one at a time, so an
    // unknown option or a missing value is refused rather than ignored. The
    // value helpers exit on a usage error; a command-line front end has nothing
    // to unwind at that point.
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string option = arguments[index];
        const bool hasNext = index + 1 < arguments.size();

        const auto value = [&](const char* what) -> std::string {
            if (!hasNext) {
                fail("option " + option + " needs a value (" + what + ")");
                std::exit(kUsage);
            }
            ++index;
            return arguments[index];
        };
        const auto number = [&](const char* what, unsigned long maximum) -> unsigned long {
            const std::string text = value(what);
            unsigned long parsed = 0;
            if (!parseUnsigned(text, maximum, parsed)) {
                fail("option " + option + " does not accept '" + text + "' (" + what + ")");
                std::exit(kUsage);
            }
            return parsed;
        };

        if (option == "--help") {
            settings.help = true;
        } else if (option == "--version") {
            settings.version = true;
        } else if (option == "--plan") {
            settings.simulate = false;
        } else if (option == "--simulate") {
            settings.simulate = true;
        } else if (option == "--backend") {
            settings.backend = value("a backend name");
        } else if (option == "--json") {
            settings.json = true;
        } else if (option == "--quiet") {
            settings.quiet = true;
        } else if (option == "--force") {
            settings.force = true;
        } else if (option == "--output") {
            settings.output = value("a file path");
        } else if (option == "--list-candidates") {
            settings.listCandidates = true;
        } else if (option == "--candidates") {
            settings.candidatesRequested = true;
        } else if (option == "--samples") {
            const unsigned long samples = number("1..32", 32);
            if (samples < 1) {
                fail("--samples must be at least 1");
                return kUsage;
            }
            settings.plan.samplesPerTarget = static_cast<int>(samples);
        } else if (option == "--address") {
            const unsigned long address = number("an offset, 0..255", 255);
            settings.anyExplicitAddress = true;
            tpfancontrol::core::addTarget(settings.plan.targets,
                                          static_cast<std::uint8_t>(address),
                                          "operator-supplied address");
        } else if (option == "--range") {
            const std::string text = value("first-last, for example 0x78-0x7F");
            const std::size_t dash = text.find('-', 1);
            if (dash == std::string::npos) {
                fail("--range needs a range such as 0x78-0x7F");
                return kUsage;
            }
            unsigned long first = 0;
            unsigned long last = 0;
            if (!parseUnsigned(text.substr(0, dash), 255, first)
                || !parseUnsigned(text.substr(dash + 1), 255, last)) {
                fail("--range '" + text + "' is not a pair of offsets");
                return kUsage;
            }
            if (first > last) {
                fail("--range '" + text + "' has its bounds the wrong way round");
                return kUsage;
            }
            settings.anyExplicitAddress = true;
            tpfancontrol::core::addTargetRange(settings.plan.targets,
                                               static_cast<std::uint8_t>(first),
                                               static_cast<std::uint8_t>(last),
                                               "operator-supplied range");
        } else if (option == "--status-port") {
            settings.config.statusPort =
                static_cast<std::uint16_t>(number("a port, 0..65535", 65535));
        } else if (option == "--data-port") {
            settings.config.dataPort =
                static_cast<std::uint16_t>(number("a port, 0..65535", 65535));
        } else if (option == "--read-command") {
            settings.config.readCommand = static_cast<std::uint8_t>(number("a byte, 0..255", 255));
        } else if (option == "--write-command") {
            settings.config.writeCommand = static_cast<std::uint8_t>(number("a byte, 0..255", 255));
        } else if (option == "--ibf-mask") {
            settings.config.ibfMask = static_cast<std::uint8_t>(number("a byte, 0..255", 255));
        } else if (option == "--obf-mask") {
            settings.config.obfMask = static_cast<std::uint8_t>(number("a byte, 0..255", 255));
        } else if (option == "--timeout-ms") {
            settings.config.timeoutMs = number("milliseconds, 1..60000", 60000);
            if (settings.config.timeoutMs == 0) {
                fail("--timeout-ms must be at least 1");
                return kUsage;
            }
        } else if (option == "--poll-ms") {
            settings.config.pollIntervalMs = number("milliseconds, 1..1000", 1000);
            if (settings.config.pollIntervalMs == 0) {
                fail("--poll-ms must be at least 1");
                return kUsage;
            }
        } else if (option == "--attempts") {
            const unsigned long attempts = number("1..10", 10);
            if (attempts < 1) {
                fail("--attempts must be at least 1");
                return kUsage;
            }
            settings.config.maximumAttempts = static_cast<int>(attempts);
        } else if (option == "--recovery-ms") {
            settings.config.recoveryTimeoutMs = number("milliseconds, 1..60000", 60000);
            if (settings.config.recoveryTimeoutMs == 0) {
                fail("--recovery-ms must be at least 1");
                return kUsage;
            }
        } else {
            fail("unknown option '" + option + "'. Try --help.");
            return kUsage;
        }
    }

    if (settings.help) {
        std::fputs(kUsageText, stdout);
        return kOk;
    }
    if (settings.version) {
        std::printf("ecdiag %s (TPFanControl portable core, %s build)\n",
                    tpfancontrol::core::ecDiagToolVersion().c_str(),
                    tpfancontrol::core::ecDiagBuildFlavour().c_str());
        return kOk;
    }
    if (settings.listCandidates) {
        printCandidates();
        return kOk;
    }

    // The default plan is the documented candidate set. It is added when it was
    // asked for explicitly, and also when no address was given at all, so that
    // running the tool with no arguments can never mean "read everything".
    if (settings.candidatesRequested || !settings.anyExplicitAddress) {
        const std::vector<EcDiagTarget> candidates = tpfancontrol::core::candidateT14ReadOnlyTargets();
        for (const EcDiagTarget& target : candidates) {
            tpfancontrol::core::addTarget(settings.plan.targets, target.address, target.label);
        }
    }

    // A backend named on the command line is never silently ignored, and never
    // silently accepted either.
    if (!settings.backend.empty()) {
        fail(backendUnavailableReason(settings.backend) + " Exiting 4 without reading.");
        return kBackendUnavailable;
    }

    const std::string planError = tpfancontrol::core::validatePlan(settings.plan);
    if (!planError.empty()) {
        fail(planError);
        return kConfigInvalid;
    }
    const std::string configError = settings.config.validationMessage();
    if (!configError.empty()) {
        fail(configError);
        return kConfigInvalid;
    }

    if (settings.simulate) {
        return runSimulated(settings);
    }
    return runPlanOnly(settings);
}
