#!/usr/bin/env python3
"""The daily brief's "🎉 Events today" section, copied from the saved
Weekend & Ahead emails: no model, no searches (user, 2026-11-14).

The events email lists its picks under bold day labels ("**Fri Nov 20**",
"**Today, Sat Nov 21**") in This weekend and Next week; this takes today's
bullets from the newest saved email (state/events-<date>.md, last 8 days)
that has today's label, and puts the section right after the brief's
📅 Today section. On an events-email morning it points to that email
instead: Saturday's email opens with today, so just the pointer; Thursday's
starts on Friday, so today's picks from last week's email, then the pointer.

    python3 todays_events.py --state DIR [--date YYYY-MM-DD] < brief.md > out.md
"""

from __future__ import annotations

import argparse
import datetime
import re
import sys
from pathlib import Path

HEADING = "## 🎉 Events today"
LOOKBACK_DAYS = 8
POINTER = {
    3: "This morning's Weekend & Ahead email has this weekend and beyond.",
    5: "This morning's updated Weekend & Ahead email has today's picks.",
}
OTHER_POINTER = "This morning's Weekend & Ahead email has more."


def _label(line: str) -> str | None:
    """The text of a bold-only line ("**Sat Nov 21**"), else None."""
    s = line.strip()
    if len(s) > 4 and s.startswith("**") and s.endswith("**") and "**" not in s[2:-2]:
        return s[2:-2]
    return None


def day_items(text: str, day: datetime.date) -> list[str]:
    """Top-level bullets under the first label naming `day` (month + date)."""
    want = re.compile(rf"\b{day:%b}\w*\.? +{day.day}\b")
    out: list[str] = []
    inside = False
    for line in text.splitlines():
        label = _label(line)
        if label is not None or line.startswith("#"):
            if inside:
                break
            inside = label is not None and bool(want.search(label))
        elif inside and re.match(r"[-*] ", line):
            out.append("- " + line[2:].strip())
    return out


def _date(p: Path) -> str:
    return p.stem.removeprefix("events-")


def section(state: Path, day: datetime.date) -> str:
    cutoff = (day - datetime.timedelta(days=LOOKBACK_DAYS)).isoformat()
    saved = sorted((p for p in state.glob("events-*.md") if cutoff <= _date(p) <= day.isoformat()),
                   key=_date, reverse=True)
    sent_today = any(_date(p) == day.isoformat() for p in saved)
    pointer = POINTER.get(day.weekday(), OTHER_POINTER)
    if sent_today and day.weekday() == 5:
        lines = [pointer]
    else:
        items = next((i for p in saved if (i := day_items(p.read_text(encoding="utf-8"), day))), [])
        lines = [i[2:] for i in items]
        if sent_today:
            lines.append(pointer)
        elif not lines:
            lines = ["Nothing picked for today." if saved else "No Weekend & Ahead email in the last week."]
    return HEADING + "\n" + "\n".join(f"- {l}" for l in lines)


def insert(brief: str, block: str) -> str:
    """Put `block` after the brief's 📅 Today section (or at the end)."""
    if HEADING in brief:
        return brief
    lines = brief.split("\n")
    start = next((i for i, l in enumerate(lines) if re.match(r"#+ *📅", l)), None)
    if start is None:
        return brief.rstrip("\n") + "\n\n" + block + "\n"
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("#")), len(lines))
    while end > start + 1 and not lines[end - 1].strip():
        end -= 1
    return "\n".join(lines[:end] + ["", block] + lines[end:])


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--state", required=True, type=Path, help="folder with events-<date>.md")
    p.add_argument("--date", type=datetime.date.fromisoformat, default=datetime.date.today())
    a = p.parse_args()
    sys.stdout.write(insert(sys.stdin.read(), section(a.state, a.date)))


if __name__ == "__main__":
    main()
