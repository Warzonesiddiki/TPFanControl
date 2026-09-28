// Tests for fancontrol/core/ecdiag.{h,cpp}, the read-only diagnostic (T5-09).
//
// Two properties are what this suite exists for.
//
// 1. A diagnostic run must not write. That is asserted against the bus's own
//    write trace and the fake EC's committed-write log, not against the tool's
//    intent: "we meant to only read" is a claim about the code, and what the
//    report needs to be able to say is a claim about the machine. The same
//    suite then shows the guard firing, by performing a real register write
//    through a real EcBus and checking that the run notices.
//
// 2. The report must be machine-readable. That is checked by parsing the JSON
//    with the strict parser below, which is itself self-tested against input
//    that is not JSON. Grepping the output for a substring would have proved
//    nothing about whether a consumer could read it, and would not have caught
//    an unescaped quote in a message.
//
// Every assertion uses CHECK, not assert: NDEBUG removes assert, and the
// Release configurations the CI build uses define it.

#include "test_check.h"

#include "../fancontrol/core/ec_access.h"
#include "../fancontrol/core/ec_protocol.h"
#include "../fancontrol/core/ecdiag.h"

#include "fake_ec.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace tpfancontrol {
namespace test {
namespace {

using core::EcBus;
using core::EcBusConfig;
using core::EcBusReader;
using core::EcDiagOptions;
using core::EcDiagOutcome;
using core::EcDiagPlan;
using core::EcDiagRun;
using core::EcDiagTarget;
using core::EcDiagTargetResult;
using core::IoErrorCode;
using core::IoResult;
using core::ReaderAudit;

int testCount = 0;

void noteTest()
{
    ++testCount;
}

// ---------------------------------------------------------------------------
// A strict JSON reader
// ---------------------------------------------------------------------------
//
// Deliberately strict: it rejects a trailing comma, a leading zero, a single
// quoted string, trailing content after the top-level value, and an unescaped
// control character. A permissive parser would accept output no real consumer
// could read, which is the failure a "machine-readable" claim is supposed to
// rule out.
//
// One deliberate limitation: a \uXXXX escape for a surrogate code unit is
// rejected rather than combined. The writer in ec_protocol never emits one -
// it escapes only the seven two-character forms and the C0 controls - and a
// parser that silently produced invalid UTF-8 would be worse than one that says
// what it cannot do.
struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    std::string number;
    std::string text;
    std::vector<JsonValue> items;
    std::vector<std::pair<std::string, JsonValue> > members;

    const JsonValue* member(const std::string& key) const
    {
        for (const std::pair<std::string, JsonValue>& entry : members) {
            if (entry.first == key) {
                return &entry.second;
            }
        }
        return nullptr;
    }

    bool isNull() const { return type == Type::Null; }
    long long asInteger() const { return std::strtoll(number.c_str(), nullptr, 10); }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text) {}

    bool parse(JsonValue& out)
    {
        skip();
        if (!parseValue(out)) {
            return false;
        }
        skip();
        if (pos_ != text_.size()) {
            return fail("unexpected trailing content");
        }
        return true;
    }

    const std::string& error() const { return error_; }

private:
    bool fail(const std::string& what)
    {
        if (error_.empty()) {
            error_ = what + " at byte " + std::to_string(pos_);
        }
        return false;
    }

    bool done() const { return pos_ >= text_.size(); }
    char peek() const { return text_[pos_]; }

    void skip()
    {
        while (!done()) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
                continue;
            }
            break;
        }
    }

    bool literal(const char* word)
    {
        const std::size_t length = std::strlen(word);
        if (text_.compare(pos_, length, word) != 0) {
            return false;
        }
        pos_ += length;
        return true;
    }

    bool parseValue(JsonValue& out)
    {
        skip();
        if (done()) {
            return fail("expected a value");
        }
        const char c = peek();
        if (c == '{') {
            return parseObject(out);
        }
        if (c == '[') {
            return parseArray(out);
        }
        if (c == '"') {
            out.type = JsonValue::Type::String;
            return parseString(out.text);
        }
        if (c == 't' || c == 'f') {
            if (literal("true")) {
                out.type = JsonValue::Type::Bool;
                out.boolean = true;
                return true;
            }
            if (literal("false")) {
                out.type = JsonValue::Type::Bool;
                out.boolean = false;
                return true;
            }
            return fail("expected true or false");
        }
        if (c == 'n') {
            if (!literal("null")) {
                return fail("expected null");
            }
            out.type = JsonValue::Type::Null;
            return true;
        }
        return parseNumber(out);
    }

    bool parseObject(JsonValue& out)
    {
        out.type = JsonValue::Type::Object;
        ++pos_;                       // '{'
        skip();
        if (done()) {
            return fail("unterminated object");
        }
        if (peek() == '}') {
            ++pos_;
            return true;
        }
        for (;;) {
            skip();
            if (done() || peek() != '"') {
                return fail("expected a quoted member name");
            }
            std::string key;
            if (!parseString(key)) {
                return false;
            }
            skip();
            if (done() || peek() != ':') {
                return fail("expected ':' after a member name");
            }
            ++pos_;
            JsonValue value;
            if (!parseValue(value)) {
                return false;
            }
            out.members.push_back(std::make_pair(key, value));
            skip();
            if (done()) {
                return fail("unterminated object");
            }
            if (peek() == ',') {
                ++pos_;
                skip();
                // A trailing comma leaves '}' where a member name belongs, and
                // the next iteration reports it.
                continue;
            }
            if (peek() == '}') {
                ++pos_;
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    bool parseArray(JsonValue& out)
    {
        out.type = JsonValue::Type::Array;
        ++pos_;                       // '['
        skip();
        if (done()) {
            return fail("unterminated array");
        }
        if (peek() == ']') {
            ++pos_;
            return true;
        }
        for (;;) {
            JsonValue value;
            if (!parseValue(value)) {
                return false;
            }
            out.items.push_back(value);
            skip();
            if (done()) {
                return fail("unterminated array");
            }
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == ']') {
                ++pos_;
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    bool parseString(std::string& out)
    {
        ++pos_;                       // opening quote
        while (!done()) {
            const unsigned char c = static_cast<unsigned char>(peek());
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (c < 0x20) {
                return fail("unescaped control character in a string");
            }
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                ++pos_;
                continue;
            }
            ++pos_;
            if (done()) {
                return fail("unterminated escape");
            }
            const char escape = peek();
            ++pos_;
            switch (escape) {
            case '"':  out.push_back('"');  break;
            case '\\': out.push_back('\\'); break;
            case '/':  out.push_back('/');  break;
            case 'b':  out.push_back('\b'); break;
            case 'f':  out.push_back('\f'); break;
            case 'n':  out.push_back('\n'); break;
            case 'r':  out.push_back('\r'); break;
            case 't':  out.push_back('\t'); break;
            case 'u': {
                if (pos_ + 4 > text_.size()) {
                    return fail("truncated \\u escape");
                }
                unsigned code = 0;
                for (int digit = 0; digit < 4; ++digit) {
                    const char hex = text_[pos_ + static_cast<std::size_t>(digit)];
                    unsigned value = 0;
                    if (hex >= '0' && hex <= '9') {
                        value = static_cast<unsigned>(hex - '0');
                    } else if (hex >= 'a' && hex <= 'f') {
                        value = static_cast<unsigned>(hex - 'a') + 10u;
                    } else if (hex >= 'A' && hex <= 'F') {
                        value = static_cast<unsigned>(hex - 'A') + 10u;
                    } else {
                        return fail("\\u escape with a non-hex digit");
                    }
                    code = (code << 4) | value;
                }
                pos_ += 4;
                if (code >= 0xD800 && code <= 0xDFFF) {
                    return fail("surrogate escape is not decoded by this test parser");
                }
                if (code < 0x80) {
                    out.push_back(static_cast<char>(code));
                } else if (code < 0x800) {
                    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                } else {
                    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
                break;
            }
            default:
                return fail("unknown escape");
            }
        }
        return fail("unterminated string");
    }

    bool parseNumber(JsonValue& out)
    {
        const std::size_t start = pos_;
        if (!done() && peek() == '-') {
            ++pos_;
        }
        if (done()) {
            return fail("truncated number");
        }
        if (peek() == '0') {
            ++pos_;
            if (!done() && peek() >= '0' && peek() <= '9') {
                return fail("a number may not have a leading zero");
            }
        } else if (peek() >= '1' && peek() <= '9') {
            while (!done() && peek() >= '0' && peek() <= '9') {
                ++pos_;
            }
        } else {
            return fail("expected a value");
        }
        if (!done() && peek() == '.') {
            ++pos_;
            if (done() || peek() < '0' || peek() > '9') {
                return fail("a fraction needs at least one digit");
            }
            while (!done() && peek() >= '0' && peek() <= '9') {
                ++pos_;
            }
        }
        if (!done() && (peek() == 'e' || peek() == 'E')) {
            ++pos_;
            if (!done() && (peek() == '+' || peek() == '-')) {
                ++pos_;
            }
            if (done() || peek() < '0' || peek() > '9') {
                return fail("an exponent needs at least one digit");
            }
            while (!done() && peek() >= '0' && peek() <= '9') {
                ++pos_;
            }
        }
        out.type = JsonValue::Type::Number;
        out.number = text_.substr(start, pos_ - start);
        return true;
    }

    const std::string& text_;
    std::size_t pos_ = 0;
    std::string error_;
};

bool parseJson(const std::string& text, JsonValue& out, std::string& error)
{
    JsonParser parser(text);
    if (parser.parse(out)) {
        return true;
    }
    error = parser.error();
    return false;
}

// Parses, aborting the suite with a readable message if the output is not JSON.
JsonValue mustParseJson(const std::string& text)
{
    JsonValue value;
    std::string error;
    if (!parseJson(text, value, error)) {
        std::fprintf(stderr, "ecdiag output is not valid JSON: %s\n", error.c_str());
        std::fprintf(stderr, "%.400s\n", text.c_str());
        std::fflush(stderr);
        std::abort();
    }
    return value;
}

const JsonValue& memberOf(const JsonValue& object, const std::string& key)
{
    const JsonValue* value = object.member(key);
    if (value == nullptr) {
        std::fprintf(stderr, "ecdiag report has no member %s\n", key.c_str());
        std::fflush(stderr);
        std::abort();
    }
    return *value;
}

// ---------------------------------------------------------------------------
// Test doubles
// ---------------------------------------------------------------------------

// A reader that is scripted rather than modelled: it answers with whatever the
// test put in it and records what it was asked. It is used for the cases where
// a real bus cannot produce the condition under test - a reader whose audit
// overstates what it did, or one that counts its calls wrongly.
class ScriptedReader final : public core::IRegisterReader {
public:
    void setValue(std::uint8_t address, std::uint8_t value)
    {
        values_[address] = value;
    }

    // Returns values from this list in order, cycling. A register whose value
    // changes between two reads taken milliseconds apart is not a stable
    // reading, and the report has to say so.
    void setSequence(const std::vector<std::uint8_t>& values)
    {
        sequence_ = values;
        sequenceIndex_ = 0;
    }

    // Every read fails with this code until it is cleared.
    void setPersistentFailure(IoErrorCode code, const std::string& message)
    {
        hasFailure_ = true;
        failureCode_ = code;
        failureMessage_ = message;
    }

    void clearPersistentFailure()
    {
        hasFailure_ = false;
    }

    // The next `count` reads fail and the one after that succeeds.
    void failNextReads(int count, IoErrorCode code)
    {
        pendingFailures_ = count;
        pendingCode_ = code;
    }

    // Direct access to what this reader claims it did, so a test can make the
    // audit disagree with reality.
    ReaderAudit& claimedAudit() { return claimed_; }

    IoResult readRegister(std::uint8_t address) override
    {
        if (pendingFailures_ > 0) {
            --pendingFailures_;
            return IoResult::failure(pendingCode_, "scripted transient failure");
        }
        if (hasFailure_) {
            return IoResult::failure(failureCode_, failureMessage_);
        }
        if (!sequence_.empty()) {
            const std::uint8_t value = sequence_[sequenceIndex_ % sequence_.size()];
            ++sequenceIndex_;
            return IoResult::success(value);
        }
        const std::map<std::uint8_t, std::uint8_t>::const_iterator it = values_.find(address);
        return IoResult::success(it == values_.end() ? 0 : it->second);
    }

    ReaderAudit audit() const override { return claimed_; }

private:
    std::map<std::uint8_t, std::uint8_t> values_;
    std::vector<std::uint8_t> sequence_;
    std::size_t sequenceIndex_ = 0;

    bool hasFailure_ = false;
    IoErrorCode failureCode_ = IoErrorCode::ReadFailure;
    std::string failureMessage_;

    int pendingFailures_ = 0;
    IoErrorCode pendingCode_ = IoErrorCode::Timeout;

    ReaderAudit claimed_;
};

// ---------------------------------------------------------------------------
// Shared fixtures
// ---------------------------------------------------------------------------

EcBusConfig testConfig()
{
    EcBusConfig config;
    config.timeoutMs = 20;
    config.pollIntervalMs = 1;
    config.maximumAttempts = 3;
    return config;
}

EcDiagPlan smallPlan(std::uint8_t address = 0x2F, int samples = 3)
{
    EcDiagPlan plan;
    plan.targets.push_back(EcDiagTarget{address, "test target"});
    plan.samplesPerTarget = samples;
    return plan;
}

// The default plan, with a populated fake EC behind it.
EcDiagPlan candidatePlan(int samples = 3)
{
    EcDiagPlan plan;
    plan.targets = core::candidateT14ReadOnlyTargets();
    plan.samplesPerTarget = samples;
    return plan;
}

void populate(FakeEcBackend& backend)
{
    backend.setRegister(0x2F, 0x80);
    backend.setRegister(0x31, 0x00);
    backend.setRegister(0x84, 0x10);
    backend.setRegister(0x85, 0x02);
    for (int address = 0x78; address <= 0x7F; ++address) {
        backend.setRegister(static_cast<std::uint8_t>(address),
                            static_cast<std::uint8_t>(0x30 + address - 0x78));
    }
    for (int address = 0xC0; address <= 0xC3; ++address) {
        backend.setRegister(static_cast<std::uint8_t>(address),
                            static_cast<std::uint8_t>(0x40 + address - 0xC0));
    }
}

EcDiagOptions baseOptions(const EcBusConfig& config)
{
    EcDiagOptions options;
    options.config = config;
    options.configSource = "test-config";
    options.backend.name = "fake-ec";
    options.backend.version = "test";
    options.backend.haveCapabilities = true;
    options.backend.state = core::BackendState::Ready;
    options.backend.canReadPorts = true;
    options.backend.canWritePorts = true;
    return options;
}

// ---------------------------------------------------------------------------
// The plan
// ---------------------------------------------------------------------------

void testDefaultPlanIsTheDocumentedCandidateSet()
{
    noteTest();
    const std::vector<EcDiagTarget> targets = core::candidateT14ReadOnlyTargets();

    // EC_REGISTER_MAP.md section 2: 0x2F, 0x31, 0x84, 0x85, 0x78-0x7F, 0xC0-0xC3.
    std::set<int> expected;
    expected.insert(0x2F);
    expected.insert(0x31);
    expected.insert(0x84);
    expected.insert(0x85);
    for (int address = 0x78; address <= 0x7F; ++address) {
        expected.insert(address);
    }
    for (int address = 0xC0; address <= 0xC3; ++address) {
        expected.insert(address);
    }

    std::set<int> actual;
    for (const EcDiagTarget& target : targets) {
        CHECK(actual.insert(target.address).second);   // no address twice
        CHECK(!target.label.empty());                  // every address is explained
    }
    CHECK(actual == expected);
    CHECK(targets.size() == expected.size());
    CHECK(targets.size() == 16u);

    // Order is the documented order, so a report reads like the register map.
    CHECK(targets.front().address == 0x2F);
    CHECK(targets[1].address == 0x31);
    CHECK(targets[2].address == 0x84);
    CHECK(targets[3].address == 0x85);
    CHECK(targets[4].address == 0x78);
    CHECK(targets[15].address == 0xC3);

    // The plan is not a sweep: it must not name every address.
    CHECK(targets.size() < 256u);
}

void testPlanRejectsAnEmptyTargetList()
{
    noteTest();
    EcDiagPlan plan;
    plan.targets.clear();
    CHECK(!core::validatePlan(plan).empty());

    ScriptedReader reader;
    const EcDiagRun run = core::runDiagnostic(reader, plan, baseOptions(testConfig()));
    CHECK(run.outcome == EcDiagOutcome::ConfigInvalid);
    CHECK(!run.configurationError.empty());
    CHECK(core::exitCodeFor(run.outcome) == 3);
    // Nothing was read, and the audit stays empty rather than claiming zero
    // reads that reached a reader.
    CHECK(run.audit.readsIssued == 0u);
    CHECK(run.results.empty());
}

void testPlanRejectsTooManyTargets()
{
    noteTest();
    EcDiagPlan plan;
    plan.samplesPerTarget = 1;
    for (int address = 0; address <= static_cast<int>(core::kEcDiagMaximumTargets); ++address) {
        plan.targets.push_back(EcDiagTarget{static_cast<std::uint8_t>(address), "sweep"});
    }
    CHECK(plan.targets.size() > core::kEcDiagMaximumTargets);
    CHECK(!core::validatePlan(plan).empty());
    CHECK(core::validatePlan(plan).find("not a sweep") != std::string::npos);
}

void testPlanRejectsAnImpossibleSampleCount()
{
    noteTest();
    EcDiagPlan plan = smallPlan(0x2F, 0);
    CHECK(!core::validatePlan(plan).empty());

    plan.samplesPerTarget = core::kEcDiagMaximumSamplesPerTarget + 1;
    CHECK(!core::validatePlan(plan).empty());

    plan.samplesPerTarget = core::kEcDiagMaximumSamplesPerTarget;
    CHECK(core::validatePlan(plan).empty());
}

void testAddTargetDeduplicatesAndKeepsOrder()
{
    noteTest();
    std::vector<EcDiagTarget> targets;
    core::addTarget(targets, 0x84, "first");
    core::addTarget(targets, 0x2F, "second");
    core::addTarget(targets, 0x84, "duplicate");      // ignored
    core::addTargetRange(targets, 0x90, 0x92, "range");

    CHECK(targets.size() == 5u);
    CHECK(targets[0].address == 0x84);
    CHECK(targets[0].label == "first");               // the first label wins
    CHECK(targets[1].address == 0x2F);
    CHECK(targets[2].address == 0x90);
    CHECK(targets[4].address == 0x92);
}

// ---------------------------------------------------------------------------
// Read-only by construction: the headline property
// ---------------------------------------------------------------------------

void testFullRunThroughARealBusWritesNoRegister()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    populate(backend);

    FakeClock clock;
    EcBus bus(backend, config, clock);
    EcBusReader reader(bus);

    const EcDiagPlan plan = candidatePlan(3);
    const EcDiagRun run = core::runDiagnostic(reader, plan, baseOptions(config));

    // Every requested read succeeded, and the run says so.
    CHECK(run.outcome == EcDiagOutcome::Ok);
    CHECK(run.readOnlyInvariantHeld());
    CHECK(run.writeCommandsObserved == 0u);
    CHECK(run.successfulSamples == 16u * 3u);
    CHECK(run.failedSamples == 0u);
    CHECK(run.audit.readsIssued == 16u * 3u);

    // The value read from each register is the value that was put there. A
    // diagnostic that reported values would be useless if it reported the wrong
    // ones, and this is the cheapest way to catch an address being rewritten on
    // the way to the wire.
    CHECK(run.results.size() == 16u);
    CHECK(run.results[0].target.address == 0x2F);
    CHECK(run.results[0].samples.front().value == 0x80);
    CHECK(run.results[1].samples.front().value == 0x00);
    CHECK(run.results[0].stable());
    CHECK(run.results[0].distinctValues() == 1);

    // --- the claim about the machine, from the bus's own trace --------------
    const std::vector<core::BusWrite> trace = bus.writeTrace();
    CHECK(!trace.empty());

    std::set<int> statusValues;
    std::set<int> dataValues;
    std::set<int> planned;
    for (const EcDiagTarget& target : plan.targets) {
        planned.insert(target.address);
    }
    for (const core::BusWrite& write : trace) {
        if (write.port == config.statusPort) {
            statusValues.insert(write.value);
        } else if (write.port == config.dataPort) {
            dataValues.insert(write.value);
            // The data port may only ever be told an address we planned to
            // read. A presented value would be a write.
            CHECK(planned.count(write.value) == 1);
        }
    }
    // Exactly one value ever reached the status port, and it is the read
    // command. A write command there is the defect this whole test exists for.
    CHECK(statusValues.size() == 1u);
    CHECK(*statusValues.begin() == static_cast<int>(config.readCommand));
    CHECK(statusValues.count(config.writeCommand) == 0);
    CHECK(dataValues.size() == planned.size());

    // --- and from the fake's committed-write log ---------------------------
    for (const FakeEcBackend::Write& write : backend.committedWrites()) {
        if (write.port == config.statusPort) {
            CHECK(write.value == config.readCommand);
        }
    }
    // No register was written: writtenRegisters() reconstructs addresses from
    // write commands, and a read issues none.
    CHECK(backend.writtenRegisters().empty());
    CHECK(backend.registerValue(0x2F) == 0x80);       // unchanged
}

void testReadOnlyInvariantFiresWhenARealWriteReachesTheBus()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    populate(backend);

    FakeClock clock;
    EcBus bus(backend, config, clock);

    // A realistic cause, not a lying double: a future control path enabling
    // register writes and using them. The diagnostic must notice.
    bus.setRegisterWritesAllowed(true);
    const IoResult write = bus.writeRegister(0x2F, 0x40);
    CHECK(write.ok);
    CHECK(backend.registerValue(0x2F) == 0x40);
    CHECK(backend.writtenRegisters().size() == 1u);

    EcBusReader reader(bus);
    const EcDiagRun run = core::runDiagnostic(reader, smallPlan(0x2F, 1), baseOptions(config));

    CHECK(run.outcome == EcDiagOutcome::InvariantViolated);
    CHECK(!run.readOnlyInvariantHeld());
    CHECK(run.writeCommandsObserved == 1u);
    CHECK(core::exitCodeFor(run.outcome) == 1);
    CHECK(run.invariantFailures.size() >= 1u);
    // The failure names the byte, so a reader does not have to go looking.
    CHECK(run.invariantFailures.front().find("0x81") != std::string::npos);
    // The reads still happened, and are still reported. The point is that the
    // report cannot be read as clean.
    CHECK(run.results.size() == 1u);
    CHECK(run.results.front().allSucceeded());
}

void testReadOnlyInvariantFiresOnAnUnplannedDataPortWrite()
{
    noteTest();
    ScriptedReader reader;
    ReaderAudit& claimed = reader.claimedAudit();
    claimed.readsIssued = 3;
    claimed.statusPortWriteValues.push_back(0x80);
    claimed.statusPortWriteValues.push_back(0x80);
    claimed.statusPortWriteValues.push_back(0x80);
    // 0x40 is not 0x2F: something other than the planned address reached the
    // data port.
    claimed.dataPortWriteValues.push_back(0x2F);
    claimed.dataPortWriteValues.push_back(0x40);

    const EcDiagRun run = core::runDiagnostic(reader, smallPlan(0x2F, 3), baseOptions(testConfig()));

    CHECK(run.outcome == EcDiagOutcome::InvariantViolated);
    CHECK(run.writeCommandsObserved == 0u);           // the commands were fine
    bool namedTheAddress = false;
    for (const std::string& failure : run.invariantFailures) {
        if (failure.find("0x40") != std::string::npos) {
            namedTheAddress = true;
        }
    }
    CHECK(namedTheAddress);
}

void testReadOnlyInvariantFiresOnDataWithoutACommand()
{
    noteTest();
    ScriptedReader reader;
    ReaderAudit& claimed = reader.claimedAudit();
    claimed.readsIssued = 1;
    // One command write, two data writes: the second data byte had no command
    // in front of it.
    claimed.statusPortWriteValues.push_back(0x80);
    claimed.dataPortWriteValues.push_back(0x2F);
    claimed.dataPortWriteValues.push_back(0x2F);

    const EcDiagRun run = core::runDiagnostic(reader, smallPlan(0x2F, 1), baseOptions(testConfig()));

    CHECK(run.outcome == EcDiagOutcome::InvariantViolated);
    CHECK(run.invariantFailures.size() >= 1u);
}

void testReadOnlyInvariantFiresWhenTheReaderCountDisagreesWithThePlan()
{
    noteTest();
    ScriptedReader reader;
    // The reader says it received fewer calls than the plan required. A reader
    // that silently drops reads would otherwise look like a run in which every
    // target happened to have no samples.
    reader.claimedAudit().readsIssued = 2;

    const EcDiagRun run = core::runDiagnostic(reader, smallPlan(0x2F, 3), baseOptions(testConfig()));

    CHECK(run.outcome == EcDiagOutcome::InvariantViolated);
    CHECK(run.invariantFailures.size() >= 1u);
    CHECK(run.invariantFailures.front().find("3") != std::string::npos);
}

void testInvariantViolationOutranksACompleteSetOfReadings()
{
    noteTest();
    ScriptedReader reader;
    reader.setValue(0x2F, 0x80);
    // Every read succeeds; the audit is the only thing wrong.
    reader.claimedAudit().readsIssued = 3;
    reader.claimedAudit().statusPortWriteValues.push_back(0x80);
    reader.claimedAudit().statusPortWriteValues.push_back(0x81);   // the write command
    reader.claimedAudit().statusPortWriteValues.push_back(0x80);
    reader.claimedAudit().dataPortWriteValues.push_back(0x2F);

    const EcDiagRun run = core::runDiagnostic(reader, smallPlan(0x2F, 3), baseOptions(testConfig()));

    CHECK(run.successfulSamples == 3u);                // all three reads worked
    CHECK(run.failedSamples == 0u);
    CHECK(run.outcome == EcDiagOutcome::InvariantViolated);
    CHECK(core::exitCodeFor(run.outcome) == 1);
}

// ---------------------------------------------------------------------------
// Reading and reporting
// ---------------------------------------------------------------------------

void testEveryPlannedReadHappensThePlannedNumberOfTimes()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    populate(backend);
    FakeClock clock;
    EcBus bus(backend, config, clock);
    EcBusReader reader(bus);

    EcDiagPlan plan = candidatePlan(2);
    const EcDiagRun run = core::runDiagnostic(reader, plan, baseOptions(config));

    CHECK(run.results.size() == plan.targets.size());
    for (const EcDiagTargetResult& result : run.results) {
        CHECK(result.samples.size() == 2u);
        CHECK(result.samples[0].index == 0);
        CHECK(result.samples[1].index == 1);
    }
    CHECK(run.audit.readsIssued == 16u * 2u);
    CHECK(run.audit.dataPortWriteValues.size() == 16u * 2u);   // one address per read
    // The reads arrived at the fake, not merely at the reader.
    CHECK(backend.readLog().size() > 0u);
}

void testChangingValueIsReportedUnstableWithItsDistinctCount()
{
    noteTest();
    ScriptedReader reader;
    reader.setSequence(std::vector<std::uint8_t>{0x01, 0x02, 0x03});
    reader.claimedAudit().readsIssued = 3;

    const EcDiagRun run = core::runDiagnostic(reader, smallPlan(0x2F, 3), baseOptions(testConfig()));

    CHECK(run.outcome == EcDiagOutcome::Ok);
    CHECK(run.results.size() == 1u);
    const EcDiagTargetResult& result = run.results.front();
    CHECK(!result.stable());
    CHECK(result.allSucceeded());
    CHECK(result.distinctValues() == 3);
    CHECK(result.minimumValue == 0x01);
    CHECK(result.maximumValue == 0x03);

    // The report must not present a changing register as a single value.
    const JsonValue json = mustParseJson(core::toJson(run));
    const JsonValue& reads = memberOf(json, "reads");
    CHECK(reads.items.size() == 1u);
    CHECK(memberOf(reads.items[0], "stable").boolean == false);
    CHECK(memberOf(reads.items[0], "value").isNull());
    CHECK(memberOf(reads.items[0], "distinct_values").asInteger() == 3);
    CHECK(memberOf(reads.items[0], "minimum_value").asInteger() == 1);
    CHECK(memberOf(reads.items[0], "maximum_value").asInteger() == 3);
}

void testFailedReadReportsNoValueRatherThanZero()
{
    noteTest();
    ScriptedReader reader;
    reader.setPersistentFailure(IoErrorCode::Timeout, "the EC never answered");
    reader.claimedAudit().readsIssued = 3;

    const EcDiagRun run = core::runDiagnostic(reader, smallPlan(0x84, 3), baseOptions(testConfig()));

    CHECK(run.outcome == EcDiagOutcome::NoReadSucceeded);
    CHECK(core::exitCodeFor(run.outcome) == 6);
    const EcDiagTargetResult& result = run.results.front();
    CHECK(result.failureCount() == 3);
    CHECK(!result.anySucceeded());
    CHECK(!result.samples.front().hasValue);          // no value, not a zero
    CHECK(result.samples.front().value == 0);
    CHECK(result.hasError);
    CHECK(result.firstError == IoErrorCode::Timeout);

    const JsonValue json = mustParseJson(core::toJson(run));
    const JsonValue& reads = memberOf(json, "reads");
    CHECK(memberOf(reads.items[0], "value").isNull());
    CHECK(memberOf(reads.items[0], "value_hex").isNull());
    CHECK(memberOf(reads.items[0], "minimum_value").isNull());
    CHECK(memberOf(reads.items[0], "stable").boolean == false);
    CHECK(memberOf(reads.items[0], "error").text == "Timeout");
    const JsonValue& samples = memberOf(reads.items[0], "samples");
    CHECK(samples.items.size() == 3u);
    for (const JsonValue& sample : samples.items) {
        CHECK(memberOf(sample, "value").isNull());
        CHECK(memberOf(sample, "ok").boolean == false);
        CHECK(memberOf(sample, "error").text == "Timeout");
    }
}

void testZeroIsReportedAsAValueNotAsAnAbsence()
{
    noteTest();
    ScriptedReader reader;
    reader.setValue(0x2F, 0x00);                      // fan off is a legitimate byte
    reader.claimedAudit().readsIssued = 2;

    const EcDiagRun run = core::runDiagnostic(reader, smallPlan(0x2F, 2), baseOptions(testConfig()));

    CHECK(run.outcome == EcDiagOutcome::Ok);
    CHECK(run.results.front().samples.front().hasValue);
    CHECK(run.results.front().samples.front().value == 0x00);
    CHECK(run.results.front().stable());

    const JsonValue json = mustParseJson(core::toJson(run));
    const JsonValue& read = memberOf(json, "reads").items[0];
    CHECK(!memberOf(read, "value").isNull());         // present...
    CHECK(memberOf(read, "value").asInteger() == 0);  // ...and zero
    CHECK(memberOf(read, "value_hex").text == "0x00");
}

void testPartialFailureKeepsTheReadsThatWorked()
{
    noteTest();
    ScriptedReader reader;
    reader.setValue(0x2F, 0x80);
    // The first two reads fail, then the reader recovers: a transient fault,
    // which must not discard the readings that did arrive.
    reader.failNextReads(2, IoErrorCode::ReadFailure);
    reader.claimedAudit().readsIssued = 4;

    const EcDiagRun run = core::runDiagnostic(reader, smallPlan(0x2F, 4), baseOptions(testConfig()));

    CHECK(run.outcome == EcDiagOutcome::PartialReadFailure);
    CHECK(core::exitCodeFor(run.outcome) == 6);
    CHECK(run.successfulSamples == 2u);
    CHECK(run.failedSamples == 2u);
    const EcDiagTargetResult& result = run.results.front();
    CHECK(result.anySucceeded());
    CHECK(!result.allSucceeded());
    CHECK(!result.stable());                          // a partially read target is not stable
    CHECK(result.distinctValues() == 1);              // among the reads that worked
}

void testBackendUnavailableIsDistinctFromASensorFailure()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    populate(backend);
    // Advertises the capability and then denies it, or simply cannot write
    // ports: either way every read fails with a standing condition, which is a
    // backend problem rather than an EC problem.
    backend.setReadOnly();

    FakeClock clock;
    EcBus bus(backend, config, clock);
    EcBusReader reader(bus);

    EcDiagOptions options = baseOptions(config);
    // The report records what the backend actually claims, so the reader of the
    // report is not told a capability the backend does not have.
    options.backend.canWritePorts = backend.capabilities().canWritePorts;
    const EcDiagRun run = core::runDiagnostic(reader, candidatePlan(1), options);

    CHECK(run.outcome == EcDiagOutcome::BackendUnavailable);
    CHECK(core::exitCodeFor(run.outcome) == 4);
    CHECK(run.successfulSamples == 0u);
    CHECK(run.failedSamples == 16u);
    // The reader was still called: a refusal is reported, not skipped.
    CHECK(run.audit.readsIssued == 16u);
    // Nothing reached the ports, so the gate held.
    CHECK(bus.writeTrace().empty());
    CHECK(run.readOnlyInvariantHeld());
    CHECK(run.results.front().firstError == IoErrorCode::Unsupported);

    // The report says why a read needs a writable port, because that is
    // counter-intuitive and is exactly the ADR-023 lesson.
    bool explained = false;
    for (const std::string& limitation : run.limitations) {
        if (limitation.find("command byte") != std::string::npos) {
            explained = true;
        }
    }
    CHECK(explained);
}

void testAnUnusableConfigurationIsRefusedBeforeAnythingIsRead()
{
    noteTest();
    EcBusConfig config = testConfig();
    config.statusPort = config.dataPort;               // the same port twice

    ScriptedReader reader;
    const EcDiagRun run = core::runDiagnostic(reader, smallPlan(0x2F, 1), baseOptions(config));

    CHECK(run.outcome == EcDiagOutcome::ConfigInvalid);
    CHECK(core::exitCodeFor(run.outcome) == 3);
    CHECK(!run.configurationError.empty());
    CHECK(run.configurationError.find("statusPort") != std::string::npos);
    CHECK(run.audit.readsIssued == 0u);                // nothing was read
    CHECK(run.results.empty());

    const JsonValue json = mustParseJson(core::toJson(run));
    CHECK(!memberOf(json, "configuration_error").isNull());
    CHECK(memberOf(json, "config").member("sane") != nullptr);
    CHECK(memberOf(memberOf(json, "config"), "sane").boolean == false);
    CHECK(memberOf(json, "exit_code").asInteger() == 3);
}

void testTimeoutsAreVisibleInTheAudit()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    populate(backend);
    // The EC accepts the command and never produces a result.
    backend.setFault(EcFault::OutputBufferStuck);

    FakeClock clock;
    EcBus bus(backend, config, clock);
    EcBusReader reader(bus);

    const EcDiagRun run = core::runDiagnostic(reader, smallPlan(0x84, 3), baseOptions(config));

    CHECK(run.outcome == EcDiagOutcome::NoReadSucceeded);
    CHECK(run.results.front().firstError == IoErrorCode::Timeout);
    // Three attempts per read, three reads.
    CHECK(run.audit.timeoutCount == 9u);
    CHECK(run.audit.transactionCount == 3u);

    const JsonValue json = mustParseJson(core::toJson(run));
    CHECK(memberOf(memberOf(json, "audit"), "bus_timeouts").asInteger() == 9);
    CHECK(memberOf(memberOf(json, "audit"), "bus_transactions").asInteger() == 3);
}

void testElapsedTimesAreReportedWhenAClockIsSupplied()
{
    noteTest();
    ScriptedReader reader;
    reader.claimedAudit().readsIssued = 3;

    FakeClock clock;
    EcDiagOptions options = baseOptions(testConfig());
    options.nowMs = [&clock]() { return clock.nowMs(); };

    EcDiagPlan plan = smallPlan(0x2F, 3);

    // A tiny adapter, because the engine takes the interface and the clock is
    // injected separately. The clock only moves here, so the elapsed time of
    // each read is exactly what the test set.
    class TimingReader final : public core::IRegisterReader {
    public:
        TimingReader(ScriptedReader& inner, FakeClock& clock) : inner_(inner), clock_(clock) {}
        IoResult readRegister(std::uint8_t address) override
        {
            clock_.advance(4);
            return inner_.readRegister(address);
        }
        ReaderAudit audit() const override { return inner_.audit(); }

    private:
        ScriptedReader& inner_;
        FakeClock& clock_;
    };
    TimingReader timing(reader, clock);

    const EcDiagRun run = core::runDiagnostic(timing, plan, options);
    CHECK(run.outcome == EcDiagOutcome::Ok);
    CHECK(run.results.front().samples.front().hasElapsed);
    CHECK(run.results.front().samples.front().elapsedMs == 4u);

    const JsonValue json = mustParseJson(core::toJson(run));
    const JsonValue& sample = memberOf(memberOf(json, "reads").items[0], "samples").items[0];
    CHECK(memberOf(sample, "elapsed_ms").asInteger() == 4);

    // Without a clock the field is null, never a zero that reads as a very fast
    // read. "Unknown" and "instant" are different answers.
    const EcDiagRun untimed = core::runDiagnostic(reader, plan, baseOptions(testConfig()));
    const JsonValue untimedJson = mustParseJson(core::toJson(untimed));
    const JsonValue& untimedSample = memberOf(memberOf(untimedJson, "reads").items[0], "samples").items[0];
    CHECK(memberOf(untimedSample, "elapsed_ms").isNull());
}

// ---------------------------------------------------------------------------
// The machine-readable contract
// ---------------------------------------------------------------------------

void testReportCarriesTheConfigurationItUsed()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    populate(backend);
    FakeClock clock;
    EcBus bus(backend, config, clock);
    EcBusReader reader(bus);

    EcDiagOptions options = baseOptions(config);
    options.generatedUtc = "2026-09-28T12:00:00Z";
    options.configSource = "operator-supplied";
    const EcDiagRun run = core::runDiagnostic(reader, candidatePlan(1), options);

    const JsonValue json = mustParseJson(core::toJson(run));
    CHECK(memberOf(json, "schema").text == core::kEcDiagSchema);

    const JsonValue& tool = memberOf(json, "tool");
    CHECK(memberOf(tool, "name").text == "ecdiag");
    CHECK(memberOf(tool, "version").text == core::ecDiagToolVersion());
    CHECK(memberOf(tool, "generated_utc").text == "2026-09-28T12:00:00Z");
    CHECK(!memberOf(tool, "build_flavour").text.empty());

    const JsonValue& reported = memberOf(json, "config");
    CHECK(memberOf(reported, "status_port").asInteger() == config.statusPort);
    CHECK(memberOf(reported, "data_port").asInteger() == config.dataPort);
    CHECK(memberOf(reported, "status_port_hex").text == "0x1604");
    CHECK(memberOf(reported, "data_port_hex").text == "0x1600");
    CHECK(memberOf(reported, "ibf_mask").text == "0x02");
    CHECK(memberOf(reported, "obf_mask").text == "0x01");
    CHECK(memberOf(reported, "read_command").text == "0x80");
    CHECK(memberOf(reported, "write_command").text == "0x81");
    CHECK(memberOf(reported, "timeout_ms").asInteger() == 20);
    CHECK(memberOf(reported, "poll_interval_ms").asInteger() == 1);
    CHECK(memberOf(reported, "maximum_attempts").asInteger() == 3);
    CHECK(memberOf(reported, "source").text == "operator-supplied");
    // The tool has no way to declare an encoding verified, and says so.
    CHECK(memberOf(reported, "encoding_status").text == "unverified-candidate");
    CHECK(memberOf(reported, "sane").boolean == true);

    const JsonValue& backendJson = memberOf(json, "backend");
    CHECK(memberOf(backendJson, "name").text == "fake-ec");
    CHECK(memberOf(backendJson, "state").text == "Ready");
    CHECK(memberOf(memberOf(backendJson, "capabilities"), "can_read_ports").boolean == true);

    const JsonValue& plan = memberOf(json, "plan");
    CHECK(memberOf(plan, "samples_per_target").asInteger() == 1);
    CHECK(memberOf(plan, "target_count").asInteger() == 16);
    const JsonValue& targets = memberOf(plan, "targets");
    CHECK(targets.items.size() == 16u);
    CHECK(memberOf(targets.items[0], "address").text == "0x2F");
    CHECK(memberOf(targets.items[0], "address_decimal").asInteger() == 0x2F);
    CHECK(!memberOf(targets.items[0], "candidate_meaning").text.empty());

    CHECK(memberOf(memberOf(json, "audit"), "status_port_write_values_distinct").items.size() == 1u);
    CHECK(memberOf(memberOf(json, "audit"), "status_port_write_values_distinct").items[0].text == "0x80");
    CHECK(memberOf(memberOf(json, "read_only_invariant"), "status").text == "held");
    CHECK(memberOf(json, "limitations").items.size() >= 2u);
    CHECK(!memberOf(json, "evidence").isNull());
    CHECK(memberOf(memberOf(json, "evidence"), "grade").text == "hardware");
    CHECK(memberOf(memberOf(json, "evidence"), "not_evidence_reason").isNull());
}

void testSimulatedRunIsLabelledAsNotEvidence()
{
    noteTest();
    EcDiagOptions options = baseOptions(testConfig());
    options.simulated = true;

    ScriptedReader reader;
    reader.setValue(0x2F, 0x80);
    reader.claimedAudit().readsIssued = 1;

    const EcDiagRun run = core::runDiagnostic(reader, smallPlan(0x2F, 1), options);

    const JsonValue json = mustParseJson(core::toJson(run));
    const JsonValue& evidence = memberOf(json, "evidence");
    CHECK(memberOf(evidence, "grade").text == "simulated");
    CHECK(memberOf(evidence, "not_evidence_reason").text.find("not read from hardware")
          != std::string::npos);

    bool warned = false;
    for (const std::string& limitation : run.limitations) {
        if (limitation.find("not read from hardware") != std::string::npos) {
            warned = true;
        }
    }
    CHECK(warned);
    CHECK(core::toTextReport(run).find("SIMULATED") != std::string::npos);
}

void testReportEscapesWhatItMustAndStaysParseable()
{
    noteTest();
    ScriptedReader reader;
    // A message with every character the escaping rules exist for. The message
    // is the easiest place for output to stop being JSON, because it is the one
    // field whose content the tool does not control.
    const std::string nasty = "quote \" backslash \\ newline \n tab \t snowman \xE2\x98\x83";
    reader.setPersistentFailure(IoErrorCode::ReadFailure, nasty);
    reader.claimedAudit().readsIssued = 1;

    const EcDiagRun run = core::runDiagnostic(reader, smallPlan(0x2F, 1), baseOptions(testConfig()));
    const std::string json = core::toJson(run);

    // The raw newline and the raw quote must not appear inside the string.
    CHECK(json.find("newline \n") == std::string::npos);
    CHECK(json.find("\"quote\"") == std::string::npos);

    const JsonValue parsed = mustParseJson(json);
    const JsonValue& message = memberOf(memberOf(parsed, "reads").items[0], "message");
    CHECK(message.type == JsonValue::Type::String);
    // Escaping is lossless: the message survives the round trip byte for byte,
    // including the UTF-8 sequence that must NOT have been escaped byte-wise.
    CHECK(message.text == nasty);
}

void testJsonValidatorRejectsWhatIsNotJson()
{
    noteTest();
    const char* valid[] = {
        "{}",
        "[]",
        "{\"a\": [1, 2, {\"b\": null}], \"c\": \"x\\ny\"}",
        "{\"n\": -0.5e+3, \"t\": true, \"f\": false}",
        "{\"esc\": \"\\\" \\\\ \\/ \\b \\f \\n \\r \\t \\u0041\"}",
    };
    for (const char* text : valid) {
        JsonValue value;
        std::string error;
        CHECK(parseJson(text, value, error));
    }

    const char* invalid[] = {
        "{\"a\": 1,}",            // trailing comma
        "{\"a\" 1}",              // missing colon
        "[1, 2",                  // unterminated array
        "{\"a\": \"unterminated}", // unterminated string
        "{\"a\": 01}",            // leading zero
        "{\"a\": +1}",            // leading plus
        "{\"a\": True}",          // JS spelling
        "{'a': 1}",               // single quotes
        "{\"a\": 1} trailing",    // content after the top-level value
        "{\"a\": \"x\ty\"}",      // raw control character
        "",                       // nothing at all
        "{\"a\": 1e}",            // empty exponent
    };
    for (const char* text : invalid) {
        JsonValue value;
        std::string error;
        CHECK(!parseJson(text, value, error));
        CHECK(!error.empty());
    }
}

void testExitCodesMatchTheDocumentedTable()
{
    noteTest();
    // docs/CLI_DIAGNOSTICS.md section 3. Codes 5, 7 and 8 are unreachable in
    // ecdiag because it has no control path, no failsafe state and no export
    // step; that is a fact about the tool worth pinning rather than leaving to
    // a reader to work out.
    CHECK(core::exitCodeFor(EcDiagOutcome::Ok) == 0);
    CHECK(core::exitCodeFor(EcDiagOutcome::ConfigInvalid) == 3);
    CHECK(core::exitCodeFor(EcDiagOutcome::BackendUnavailable) == 4);
    CHECK(core::exitCodeFor(EcDiagOutcome::NoReadSucceeded) == 6);
    CHECK(core::exitCodeFor(EcDiagOutcome::PartialReadFailure) == 6);
    CHECK(core::exitCodeFor(EcDiagOutcome::InvariantViolated) == 1);

    // toText is spelled out in the JSON, so it must be stable and non-empty.
    CHECK(core::toText(EcDiagOutcome::Ok)[0] != '\0');
    CHECK(core::toText(EcDiagOutcome::InvariantViolated)[0] != '\0');
    CHECK(std::string(core::toText(EcDiagOutcome::Ok)) == "ok");
}

void testTextReportStatesTheOutcomeAndTheConfig()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    populate(backend);
    FakeClock clock;
    EcBus bus(backend, config, clock);
    EcBusReader reader(bus);

    const EcDiagRun run = core::runDiagnostic(reader, smallPlan(0x2F, 2), baseOptions(config));
    const std::string text = core::toTextReport(run);

    CHECK(!text.empty());
    CHECK(text.find("0x2F") != std::string::npos);
    CHECK(text.find("read-only invariant: held") != std::string::npos);
    CHECK(text.find("test-config") != std::string::npos);       // the configuration source
    CHECK(text.find("outcome: ok") != std::string::npos);
    CHECK(text.find("limitations:") != std::string::npos);
    CHECK(text.find("not read from hardware") == std::string::npos);
}

void testEveryReportStatesWhatTheToolDoesNotProve()
{
    noteTest();
    ScriptedReader reader;
    reader.claimedAudit().readsIssued = 1;
    const EcDiagRun run = core::runDiagnostic(reader, smallPlan(0x2F, 1),
                                              baseOptions(testConfig()));

    // A diagnostic that only says what it saw invites the reader to treat a
    // plausible number as a verified one. These two statements are the
    // difference between a measurement and a claim.
    bool correlationStated = false;
    bool meaningNotEstablished = false;
    for (const std::string& limitation : run.limitations) {
        if (limitation.find("human step") != std::string::npos) {
            correlationStated = true;
        }
        if (limitation.find("candidate label") != std::string::npos) {
            meaningNotEstablished = true;
        }
    }
    CHECK(correlationStated);
    CHECK(meaningNotEstablished);
}

void testDefaultConfigurationIsRecordedAsUnverified()
{
    noteTest();
    // A run that was not told otherwise must record the transcribed defaults,
    // and must mark them as candidate values rather than as the machine's map.
    EcDiagOptions options;
    options.backend.name = "none";
    ScriptedReader reader;
    EcDiagPlan plan;
    plan.targets.push_back(EcDiagTarget{0x2F, "candidate"});
    plan.samplesPerTarget = 1;
    reader.claimedAudit().readsIssued = 1;

    const EcDiagRun run = core::runDiagnostic(reader, plan, options);

    CHECK(run.config.statusPort == 0x1604);
    CHECK(run.config.dataPort == 0x1600);
    CHECK(run.config.readCommand == 0x80);
    CHECK(run.config.writeCommand == 0x81);
    CHECK(run.configSource.find("default") == 0u);

    bool flagged = false;
    for (const std::string& limitation : run.limitations) {
        if (limitation.find("portio.cpp") != std::string::npos) {
            flagged = true;
        }
    }
    CHECK(flagged);
}

void runAll()
{
    // The plan
    testDefaultPlanIsTheDocumentedCandidateSet();
    testPlanRejectsAnEmptyTargetList();
    testPlanRejectsTooManyTargets();
    testPlanRejectsAnImpossibleSampleCount();
    testAddTargetDeduplicatesAndKeepsOrder();

    // Read-only by construction
    testFullRunThroughARealBusWritesNoRegister();
    testReadOnlyInvariantFiresWhenARealWriteReachesTheBus();
    testReadOnlyInvariantFiresOnAnUnplannedDataPortWrite();
    testReadOnlyInvariantFiresOnDataWithoutACommand();
    testReadOnlyInvariantFiresWhenTheReaderCountDisagreesWithThePlan();
    testInvariantViolationOutranksACompleteSetOfReadings();

    // Reading and reporting
    testEveryPlannedReadHappensThePlannedNumberOfTimes();
    testChangingValueIsReportedUnstableWithItsDistinctCount();
    testFailedReadReportsNoValueRatherThanZero();
    testZeroIsReportedAsAValueNotAsAnAbsence();
    testPartialFailureKeepsTheReadsThatWorked();
    testBackendUnavailableIsDistinctFromASensorFailure();
    testAnUnusableConfigurationIsRefusedBeforeAnythingIsRead();
    testTimeoutsAreVisibleInTheAudit();
    testElapsedTimesAreReportedWhenAClockIsSupplied();

    // The machine-readable contract
    testReportCarriesTheConfigurationItUsed();
    testSimulatedRunIsLabelledAsNotEvidence();
    testReportEscapesWhatItMustAndStaysParseable();
    testJsonValidatorRejectsWhatIsNotJson();
    testExitCodesMatchTheDocumentedTable();
    testTextReportStatesTheOutcomeAndTheConfig();
    testEveryReportStatesWhatTheToolDoesNotProve();
    testDefaultConfigurationIsRecordedAsUnverified();
}

// The suite fails if it runs fewer tests than it declares. "The tests pass" and
// "the tests ran" are different claims, and a suite that silently shrank to
// zero tests would satisfy the first without the second.
const int kExpectedTests = 28;

} // namespace
} // namespace test
} // namespace tpfancontrol

int main()
{
    using namespace tpfancontrol::test;
    runAll();
    if (testCount != kExpectedTests) {
        std::fprintf(stderr, "ecdiag_tests: ran %d test(s), expected %d - the suite changed size\n",
                     testCount, kExpectedTests);
        return 1;
    }
    std::printf("TPFanControl ecdiag tests passed (%d tests)\n", testCount);
    return 0;
}
