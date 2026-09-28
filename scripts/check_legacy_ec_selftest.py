#!/usr/bin/env python3
"""Prove that check_legacy_ec.py catches what it claims to catch.

A guard nobody has tested is a comment. This runs every rule in that checker
against a table of lines, half of which must be reported and half of which must
not, and exits non-zero on any mismatch.

The negative half matters more than the positive half. A rule that matches
everything reports every violation and is useless; a rule that matches nothing
reports none and is worse, because the build goes green while the defect it
was written for is sitting in the tree. Both failure modes look like success
from the job log, which is exactly why this file exists.

Run:  python3 scripts/check_legacy_ec_selftest.py
Exit: 0 if every rule behaves as specified, 1 otherwise.
"""

import importlib.util
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def load_checker():
    spec = importlib.util.spec_from_file_location(
        "check_legacy_ec", HERE / "check_legacy_ec.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def build_cases(rules):
    by_id = {rule_id: pattern for rule_id, pattern, _why in rules}
    selector = by_id["fan-selector-write"]
    level = by_id["fan-level-write-outside-core"]
    curve = by_id["legacy-curve-decision"]
    def write(line):
        return "ok= this->WriteByteToEC(%s);" % line

    return [
        # (line, rule, must_match, why)
        (write("TP_ECOFFSET_FAN_SWITCH, TP_ECOFFSET_FAN1"), selector, True,
         "the legacy defect, by macro name"),
        (write("0x31, 0"), selector, True, "the legacy defect, by literal"),
        (write("0X31, 0"), selector, True, "uppercase hex"),
        (write("(char)0x31, 0"), selector, True, "the legacy cast form"),
        (write("TP_ECOFFSET_FAN_SWITCH, 1"), selector, True, "any value"),
        (write("0x310, 0"), selector, False, "0x310 is not 0x31"),
        (write("TP_ECOFFSET_FAN_SELECTOR, 0"), selector, False,
         "a different name is a different register"),
        (write("ReadByteFromEC(0x31, &v)"), selector, False,
         "reading the selector is how a register map is built"),

        (write("TP_ECOFFSET_FAN, desired"), level, True, "level by macro name"),
        (write("0x2F, 0"), level, True, "level by literal"),
        (write("0x2f, 0"), level, True, "lowercase hex"),
        (write("TP_ECOFFSET_FAN1, 0"), level, False,
         "FAN1 is a fan index, not the level"),
        (write("TP_ECOFFSET_FAN2, 0"), level, False, "FAN2 likewise"),
        (write("TP_ECOFFSET_FAN_SWITCH, 0"), level, False,
         "the selector is not the level"),
        (write("TP_ECOFFSET_FAN_SPEED, 0"), level, False,
         "the tachometer is not the level"),
        (write("HdwOffset, value"), level, False,
         "an unrelated HDW write is not a fan write"),

        (write("0x2F, level"), level, True, "level, two hex digits"),

        # The deleted legacy decision procedure, and the lines that legitimately
        # read the same table. The negative half here is the whole point: the
        # config parser, the Fahrenheit sniff, the dialog's table display and
        # CoreInit's curve construction all name SmartLevels and must stay legal.
        ("if (this->MaxTemp>=this->SmartLevels[i].temp && this->SmartLevels[i].fan>=fanctrl)",
         curve, True, "the up-trigger scan"),
        ("if (this->MaxTemp <= this->SmartLevels[i].temp && this->SmartLevels[i].fan < fanctrl) {",
         curve, True, "the down-trigger scan"),
        ("if (this->MaxTemp > SmartLevels[i].temp)", curve, True,
         "any comparison, any spelling"),
        ("newfanctrl= (this->MaxTemp>=this->SmartLevels[i].temp) ? this->SmartLevels[i].fan : newfanctrl;",
         curve, True, "the same decision in one statement"),
        ("// the old scan was: if (this->MaxTemp>=this->SmartLevels[i].temp) newfanctrl= this->SmartLevels[i].fan;",
         curve, False,
         "a comment about the old scan is not the old scan (comments are stripped first)"),
        ("curveRows[curveRowCount].temperatureC= this->SmartLevels[i].temp;",
         curve, False, "CoreInit maps the table into the curve"),
        ('sscanf_s(buf+6, "%d %d", &this->SmartLevels[lcnt].temp, &this->SmartLevels[lcnt].fan);',
         curve, False, "the config parser fills the table"),
        ("if(this->SmartLevels[0].temp >= 80) Fahrenheit = TRUE;",
         curve, False, "the parser's Fahrenheit sniff, before conversion"),
        ('sprintf_s(buf+strlen(buf),sizeof(buf)-strlen(buf), "%s", this->SmartLevels[i].temp);',
         curve, False, "the dialog and the log print the table"),
        ("for (i= 0; i < 32 && this->SmartLevels[i].temp >= 0; i++) {",
         curve, False, "the end-marker scan in CoreInit"),
    ]


def main():
    checker = load_checker()
    cases = build_cases(checker.RULES)

    failures = 0
    for line, pattern, must_match, why in cases:
        matched = bool(pattern.search(checker._strip_line(line)))
        ok = matched == must_match
        if not ok:
            failures += 1
        print("  %s  %-38s %-5s (expected %-5s)  %s"
              % ("ok  " if ok else "FAIL", why, matched, must_match, line.strip()))

    print("")
    if failures:
        print("check_legacy_ec_selftest: %d of %d cases behave wrongly"
              % (failures, len(cases)))
        return 1

    print("check_legacy_ec_selftest: all %d cases behave as specified"
          % len(cases))
    return 0


if __name__ == "__main__":
    sys.exit(main())
