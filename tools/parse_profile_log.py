#!/usr/bin/env python3
"""Turn a captured hardware profile log into a table.

The tour harness writes one `[tour] RESULT ...` line per effect when it moves
on. This reads a saved log, ignores everything else in it, and writes the
effects out slowest first as markdown and as CSV.

    python tools/parse_profile_log.py profile-m1.log
    python tools/parse_profile_log.py profile-m1.log --csv profile-m1.csv

The log can be UTF-8, UTF-16 (which is what PowerShell 5.1's `>` redirection
writes on Windows) or plain ASCII, with or without the colour codes esphome
puts on the console, and the RESULT lines can be interleaved with anything
else. See docs/HARDWARE-TESTING.md.
"""

from __future__ import annotations

import argparse
import csv
import re
import sys

# Below this a 23 ms frame interval is not being met, which is the whole reason
# for capturing the log.
SLOW_FPS = 40.0

ANSI = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]")
RESULT = re.compile(r"\[tour\]\s+RESULT\s+(.*)$")
# key=value, key="value with spaces", key=1/2/3
FIELD = re.compile(r'(\w+)=("([^"]*)"|\S+)')


def read_text(path: str) -> str:
    """Reads a log whatever encoding it was saved in.

    PowerShell 5.1 writes UTF-16 LE with a byte order mark when you redirect
    with `>` or `*>`, so a log captured the obvious way is not UTF-8 and would
    otherwise come back as a wall of NUL bytes.
    """
    with open(path, "rb") as handle:
        raw = handle.read()
    for bom, encoding in (
        (b"\xff\xfe\x00\x00", "utf-32-le"),
        (b"\x00\x00\xfe\xff", "utf-32-be"),
        (b"\xff\xfe", "utf-16-le"),
        (b"\xfe\xff", "utf-16-be"),
        (b"\xef\xbb\xbf", "utf-8-sig"),
    ):
        if raw.startswith(bom):
            return raw.decode(encoding, errors="replace")
    # No mark. A UTF-16 file without one still gives itself away: every other
    # byte of ASCII text is a NUL.
    if raw[:400].count(b"\x00") > len(raw[:400]) // 4:
        return raw.decode("utf-16-le", errors="replace")
    return raw.decode("utf-8", errors="replace")


def triple(value: str) -> tuple[int, int, int]:
    """`avg/min/max` as three integers, zeros when the field is malformed."""
    parts = value.split("/")
    if len(parts) != 3:
        return (0, 0, 0)
    out = []
    for part in parts:
        try:
            out.append(int(part))
        except ValueError:
            out.append(0)
    return (out[0], out[1], out[2])


def parse(text: str) -> list[dict]:
    rows = []
    for line in text.splitlines():
        match = RESULT.search(ANSI.sub("", line))
        if match is None:
            continue
        fields = {}
        for key, quoted, inner in FIELD.findall(match.group(1)):
            fields[key] = inner if quoted.startswith('"') else quoted
        if "name" not in fields:
            continue
        render = triple(fields.get("render_us", ""))
        output = triple(fields.get("out_us", ""))

        def number(key: str, default: float = 0.0) -> float:
            try:
                return float(fields.get(key, default))
            except ValueError:
                return default

        rows.append(
            {
                "name": fields["name"],
                "group": fields.get("group", ""),
                "frames": int(number("frames")),
                "fps": number("fps"),
                "render_avg": render[0],
                "render_min": render[1],
                "render_max": render[2],
                "out_avg": output[0],
                "out_min": output[1],
                "out_max": output[2],
                "heap": int(number("heap")),
                "largest": int(number("largest")),
                "psram": int(number("psram")),
                "data_bytes": int(number("data_bytes")),
            }
        )
    return rows


COLUMNS = [
    ("name", "Effect"),
    ("group", "Group"),
    ("fps", "fps"),
    ("render_avg", "Render avg us"),
    ("render_max", "Render max us"),
    ("out_avg", "Output avg us"),
    ("out_max", "Output max us"),
    ("frames", "Frames"),
    ("data_bytes", "Data bytes"),
]


def markdown(rows: list[dict]) -> str:
    header = [label for _, label in COLUMNS] + ["Slow"]
    lines = [
        "| " + " | ".join(header) + " |",
        "| " + " | ".join("---" for _ in header) + " |",
    ]
    for row in rows:
        cells = []
        for key, _ in COLUMNS:
            value = row[key]
            cells.append(f"{value:.1f}" if key == "fps" else str(value))
        cells.append("yes" if row["fps"] < SLOW_FPS else "")
        lines.append("| " + " | ".join(cells) + " |")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", help="the captured esphome log")
    parser.add_argument("--csv", help="also write the table here")
    parser.add_argument(
        "--slow-only",
        action="store_true",
        help=f"only the effects under {SLOW_FPS:.0f} fps",
    )
    args = parser.parse_args()

    rows = parse(read_text(args.log))
    if not rows:
        print(
            f"No [tour] RESULT lines in {args.log}. "
            "Press the Profile run button and capture the log while it runs.",
            file=sys.stderr,
        )
        return 1

    # Slowest first, which is the order anybody reading this wants.
    rows.sort(key=lambda row: (row["fps"], -row["render_avg"]))
    slow = [row for row in rows if row["fps"] < SLOW_FPS]
    shown = slow if args.slow_only else rows

    print(f"{len(rows)} effect(s), {len(slow)} under {SLOW_FPS:.0f} fps\n")
    print(markdown(shown))

    if args.csv:
        with open(args.csv, "w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
            writer.writeheader()
            writer.writerows(shown)
        print(f"\nwrote {args.csv}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
