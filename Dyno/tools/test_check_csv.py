#!/usr/bin/env python3
"""Tests for tools/check_csv.py. Run: python3 tools/test_check_csv.py"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_csv  # noqa: E402

SAMPLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "csv-sample.csv")
HEADER = check_csv.HEADER + "\r\n"
BLANK_ROW = "1,0,0,0,,,,,,,,,,,,,,,,,,,0,0,,,0\r\n"


def row(**cells):
    """BLANK_ROW with some cells replaced by name."""
    values = BLANK_ROW[:-2].split(",")
    for name, value in cells.items():
        values[check_csv.COL[name]] = str(value)
    return ",".join(values) + "\r\n"


class CheckCsv(unittest.TestCase):
    def assertProblem(self, lines, fragment):
        errors, _ = check_csv.check(lines)
        self.assertTrue(any(fragment in e for e in errors), f"no '{fragment}' in {errors}")

    def test_published_sample_passes(self):
        with open(SAMPLE, "r", encoding="ascii", newline="") as f:
            errors, stats = check_csv.check(f.readlines())
        self.assertEqual([], errors)
        self.assertEqual(7, stats["rows"])
        self.assertEqual(0, stats["lf_only"])
        self.assertEqual(0, check_csv.main(["check_csv.py", SAMPLE]))

    def test_header_has_the_27_issue_names(self):
        self.assertEqual(27, len(check_csv.COLUMNS))
        self.assertTrue(check_csv.HEADER.startswith("schema_version,timestamp_ms,can_received_mask,"))
        self.assertTrue(check_csv.HEADER.endswith(",calibration_version,tare_version,output_dropped_rows"))

    def test_reordered_header_fails(self):
        swapped = check_csv.HEADER.replace("lambda_a,lambda_b", "lambda_b,lambda_a")
        self.assertProblem([swapped + "\r\n", BLANK_ROW], "header does not match")

    def test_wrong_cell_count_fails(self):
        self.assertProblem([HEADER, BLANK_ROW.replace(",0\r\n", "\r\n")], "26 cells")

    def test_text_in_a_cell_fails(self):
        self.assertProblem([HEADER, row(rpm="nan")], "not a number: rpm")

    def test_fresh_outside_received_fails(self):
        self.assertProblem([HEADER, row(can_received_mask=1, can_fresh_mask=3)], "can_fresh_mask")

    def test_drop_count_going_down_fails(self):
        self.assertProblem([HEADER, row(output_dropped_rows=3), row(output_dropped_rows=2)], "went down")

    def test_board_restart_resets_drop_count(self):
        errors, stats = check_csv.check([HEADER, row(output_dropped_rows=3), HEADER, row()])
        self.assertEqual([], errors)
        self.assertEqual(2, stats["headers"])

    def test_late_capture_without_header_fails(self):
        self.assertProblem([BLANK_ROW], "data before the header")

    def test_cut_off_last_line_fails(self):
        self.assertProblem([HEADER, BLANK_ROW[:10]], "no line ending")

    def test_diagnostic_lines_are_skipped(self):
        errors, stats = check_csv.check([HEADER, "# tare accepted\r\n", BLANK_ROW])
        self.assertEqual([], errors)
        self.assertEqual(1, stats["diagnostics"])

    def test_lf_endings_are_noted_not_failed(self):
        errors, stats = check_csv.check([HEADER[:-2] + "\n", BLANK_ROW[:-2] + "\n"])
        self.assertEqual([], errors)
        self.assertEqual(2, stats["lf_only"])


if __name__ == "__main__":
    unittest.main()
