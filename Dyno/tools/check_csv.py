#!/usr/bin/env python3
"""Check a dyno serial capture against CSV schema version 1 (#38).

Usage:
    python3 tools/check_csv.py capture.csv
    python3 tools/check_csv.py docs/csv-sample.csv

Exit code 0 = valid, 1 = problems found (each printed with its line number),
2 = bad usage / unreadable file. Python 3.9+, standard library only.

Checks: the header is exactly the 27 schema-1 names; every data row has 27
cells, schema_version 1, and only numeric or empty cells; masks are 0-127
with can_fresh_mask inside can_received_mask; 0/1 flags are 0 or 1;
output_dropped_rows never goes down. Lines starting "# " are diagnostics and
are skipped. A second header means the board restarted: checks start over.
"""

import re
import sys

# Schema version 1, in order (docs/csv-output.md). Written out here on purpose
# instead of read from the firmware, so this script is an independent check.
COLUMNS = [
    "schema_version", "timestamp_ms", "can_received_mask", "can_fresh_mask",
    "rpm", "lambda_avg", "lambda_a", "lambda_b",
    "fuel_inj_pulse_width_ms", "fuel_inj_duty_pct", "knock_detected", "rev_limit_rpm",
    "lambda_target", "battery_v", "intake_air_temp_c", "coolant_temp_c", "gear",
    "loadcell_raw", "force_n", "torque_nm", "power_w", "horsepower_hp",
    "loadcell_valid", "calibration_ready", "calibration_version", "tare_version",
    "output_dropped_rows",
]
HEADER = ",".join(COLUMNS)
COL = {name: i for i, name in enumerate(COLUMNS)}
ALWAYS_PRESENT = ["schema_version", "timestamp_ms", "can_received_mask", "can_fresh_mask",
                  "loadcell_valid", "calibration_ready", "output_dropped_rows"]
NUMBER = re.compile(r"^-?[0-9]+(\.[0-9]+)?$")


def check(lines):
    """lines: raw text lines, each with its line ending. Returns (errors, stats)."""
    errors = []
    stats = {"rows": 0, "diagnostics": 0, "headers": 0, "lf_only": 0}
    seen_header = False
    last_dropped = None

    for number, raw in enumerate(lines, start=1):
        if raw.endswith("\r\n"):
            text = raw[:-2]
        elif raw.endswith("\n"):
            text = raw[:-1]
            stats["lf_only"] += 1
        else:
            text = raw  # last line with no ending: an unfinished line
            errors.append(f"line {number}: no line ending (capture cut off mid-line?)")
            continue

        def err(message):
            errors.append(f"line {number}: {message}")

        if text.startswith("# "):
            stats["diagnostics"] += 1
            continue
        if text == HEADER:
            stats["headers"] += 1
            seen_header = True
            last_dropped = None  # board restarted: counters start again
            continue
        if text.startswith("schema_version"):
            err("header does not match schema 1 (columns missing, renamed or reordered)")
            seen_header = True
            continue
        if not seen_header:
            err("data before the header (capture started late? reset the board before capturing)")
            continue

        cells = text.split(",")
        if len(cells) != len(COLUMNS):
            err(f"{len(cells)} cells, expected {len(COLUMNS)}")
            continue
        stats["rows"] += 1

        bad = [COLUMNS[i] for i, c in enumerate(cells) if c and not NUMBER.match(c)]
        if bad:
            err("not a number: " + ", ".join(bad))
            continue
        missing = [n for n in ALWAYS_PRESENT if not cells[COL[n]]]
        if missing:
            err("blank but always present: " + ", ".join(missing))
            continue

        if cells[COL["schema_version"]] != "1":
            err(f"schema_version {cells[COL['schema_version']]}, this script checks version 1")
        received = int(cells[COL["can_received_mask"]])
        fresh = int(cells[COL["can_fresh_mask"]])
        if received > 127 or fresh > 127:
            err("CAN mask above 127 (only 7 messages)")
        if fresh & ~received:
            err("can_fresh_mask has a message that can_received_mask doesn't")
        for flag in ("loadcell_valid", "calibration_ready"):
            if cells[COL[flag]] not in ("0", "1"):
                err(f"{flag} must be 0 or 1")
        if cells[COL["loadcell_valid"]] == "0" and cells[COL["loadcell_raw"]]:
            err("loadcell_raw printed while loadcell_valid is 0")
        dropped = int(cells[COL["output_dropped_rows"]])
        if last_dropped is not None and dropped < last_dropped:
            err("output_dropped_rows went down without a new header")
        last_dropped = dropped

    if stats["headers"] == 0 and not any("header" in e for e in errors):
        errors.append("no header line found")
    return errors, stats


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    try:
        with open(argv[1], "r", encoding="ascii", newline="") as f:
            lines = f.readlines()
    except (OSError, UnicodeDecodeError) as e:
        print(f"cannot read {argv[1]}: {e}", file=sys.stderr)
        return 2

    errors, stats = check(lines)
    for e in errors:
        print(e)
    print(f"{argv[1]}: {stats['rows']} rows, {stats['diagnostics']} '# ' lines, "
          f"{stats['headers']} header(s)")
    if stats["lf_only"]:
        print(f"note: {stats['lf_only']} line(s) end in LF, not CRLF "
              "(the capture tool may have changed line endings)")
    print("OK" if not errors else f"FAILED: {len(errors)} problem(s)")
    return 0 if not errors else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
