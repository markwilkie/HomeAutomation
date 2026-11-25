"""Brief comparison (user, 2026-11-25: catch unexpected changes after the
email/WhatsApp digests went in; completeness is a must).

Each brief is saved in state/briefs/<date>[-kind].md with its run stats.
  real brief   compared with the previous day's (up to 3 days back)
  trial/test   compared with the SAME morning's real brief when there is one:
               the digest trial (run.py --trial) against the original, item
               by item -- the completeness test before digests go to everyone.
Each comparison is code checks (a core section missing, Needs you / Waiting
on others shrinking sharply, much shorter, many more rounds or tokens) plus
one small low-effort model call. Reports go to Mark only, never also_to.
A trial or test file never becomes the next day's baseline.
"""

from __future__ import annotations

import datetime
import json
import os
import re
from pathlib import Path

KEEP_DAYS = 45
CORE_SECTIONS = ("Today", "Money", "To Do", "News", "Around town")
WATCHED = ("Needs you", "Waiting on others")

DAY_TO_DAY_PROMPT = """You check a personal daily brief email against the previous one, to catch a
regression in how it was generated (its data gathering just changed). Items in
"Needs you" and "Waiting on others" are supposed to stay until done, with these
rules: a dated item stays until its date passes; a reply owed or a request with
no date stays 14 days; a risk alert 3 days; waiting-on-others 10 days from the
latest ask; connection problems until fixed; anything settled in any channel is
dropped. Catch-up only lists what's new since yesterday, so it changes daily.

List only real concerns: an action item that vanished with no sign it was
settled, expired or answered; a section that went missing or empty when it
shouldn't; facts that contradict yesterday's (dates, amounts, names); an item
now listed twice. Normal day-to-day churn is not a concern. Answer JSON only:
{"concerns": ["<one line each, naming the item>"], "summary": "<one line>", "extra": []}"""

SAME_DAY_PROMPT = """Two versions of the same morning's personal daily brief were generated from the
same sources: ORIGINAL (the trusted way) and NEW (a cheaper way being trialled).
Completeness is what matters. Go through ORIGINAL item by item -- every action
item, waiting-on item, connection problem, calendar item, money note and FYI --
and list each one NEW leaves out or gets materially wrong (a different date,
amount or name, or a detail that changes what I'd do). An item that moved to a
different section, or is worded differently, is fine. News headlines and market
figures may differ slightly; only flag a missing section. Then list action items
NEW has that ORIGINAL lacks. Answer JSON only:
{"concerns": ["<each item ORIGINAL has that NEW misses or gets wrong, one line naming it>"],
 "extra": ["<each action item only NEW has, one line>"], "summary": "<one line>"}"""


def sections(text: str) -> dict[str, list[str]]:
    """{heading text without emoji: [bullet lines]}; "" holds lines before the first heading."""
    out: dict[str, list[str]] = {"": []}
    current = ""
    for line in text.splitlines():
        if line.startswith("## "):
            current = re.sub(r"^[^\w]+", "", line[3:]).strip()
            out[current] = []
        elif re.match(r"^\s*[-*] ", line):
            out[current].append(line.strip()[2:].strip())
    return out


def _find(secs: dict, name: str) -> list[str] | None:
    for heading, bullets in secs.items():
        if heading.startswith(name):
            return bullets
    return None


def code_checks(today: str, prev: str, stats: dict, prev_stats: dict) -> list[str]:
    flags = []
    t, p = sections(today), sections(prev)
    for name in CORE_SECTIONS:
        if _find(p, name) is not None and _find(t, name) is None:
            flags.append(f"Section missing: {name}")
    for name in WATCHED:
        before, now = len(_find(p, name) or []), len(_find(t, name) or [])
        if before >= 4 and now <= before // 2:
            flags.append(f"{name} went from {before} items to {now}")
    if len(prev) > 2000 and len(today) < 0.6 * len(prev):
        flags.append(f"Brief is much shorter: {len(today):,} chars vs {len(prev):,}")
    if stats.get("turns", 0) >= 10:
        flags.append(f"Took {stats['turns']} rounds with the model")
    return flags


def model_check(today: str, prev: str, prev_day: str, same_day: bool = False) -> dict:
    from openai import OpenAI  # deployed venv; tests stub this function

    client = OpenAI(base_url=os.environ["AZURE_OPENAI_BASE_URL"], api_key=os.environ["AZURE_OPENAI_API_KEY"])
    if same_day:
        prompt, body = SAME_DAY_PROMPT, f"ORIGINAL:\n\n{prev}\n\nNEW:\n\n{today}"
    else:
        prompt, body = DAY_TO_DAY_PROMPT, f"PREVIOUS BRIEF ({prev_day}):\n\n{prev}\n\nTODAY'S BRIEF:\n\n{today}"
    r = client.responses.create(
        model=os.environ.get("AZURE_OPENAI_DEPLOYMENT", "gpt-6.1-sol"),
        reasoning={"effort": "medium" if same_day else "low"},
        max_output_tokens=4000, instructions=prompt, input=body)
    text = (r.output_text or "").strip().removeprefix("```json").removeprefix("```").removesuffix("```").strip()
    out = json.loads(text)
    out["usage"] = r.usage and (r.usage.input_tokens, r.usage.output_tokens)
    return out


def _previous(briefs: Path, day: datetime.date) -> tuple[str | None, Path | None]:
    for back in range(1, 4):
        d = (day - datetime.timedelta(days=back)).isoformat()
        if (briefs / f"{d}.md").exists():
            return d, briefs / f"{d}.md"
    return None, None


def save(state: Path, day: datetime.date, text: str, stats: dict, kind: str = "") -> Path:
    briefs = state / "briefs"
    briefs.mkdir(parents=True, exist_ok=True)
    name = day.isoformat() + (f"-{kind}" if kind else "")
    (briefs / f"{name}.md").write_text(text + "\n", encoding="utf-8")
    (briefs / f"{name}.json").write_text(json.dumps(stats), encoding="utf-8")
    cutoff = (day - datetime.timedelta(days=KEEP_DAYS)).isoformat()
    for p in briefs.iterdir():
        if p.name[:10] < cutoff:
            p.unlink()
    return briefs / f"{name}.md"


def report(state: Path, day: datetime.date, text: str, stats: dict, kind: str = "",
           use_model: bool = True) -> tuple[str, int]:
    """(Markdown report, number of flags)."""
    briefs = state / "briefs"
    same_day = bool(kind) and (briefs / f"{day.isoformat()}.md").exists()
    if same_day:
        prev_day, prev_path = day.isoformat(), briefs / f"{day.isoformat()}.md"
    else:
        prev_day, prev_path = _previous(briefs, day)
    if not prev_path:
        return "No earlier brief saved to compare with yet; comparisons start tomorrow.", 0
    prev = prev_path.read_text(encoding="utf-8")
    try:
        prev_stats = json.loads(prev_path.with_suffix(".json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        prev_stats = {}

    flags = code_checks(text, prev, stats, prev_stats)
    concerns, extra, summary = [], [], ""
    if use_model:
        try:
            m = model_check(text, prev, prev_day, same_day)
            concerns = [c for c in m.get("concerns") or [] if c]
            extra = [c for c in m.get("extra") or [] if c]
            summary = m.get("summary") or ""
        except Exception as exc:  # the code checks still go out
            summary = f"(model check failed: {type(exc).__name__}: {exc})"

    t, p = sections(text), sections(prev)
    rows = []
    for heading in dict.fromkeys([h for h in p if h] + [h for h in t if h]):
        a, b = _find(p, heading), _find(t, heading)
        rows.append(f"| {heading} | {'—' if a is None else len(a)} | {'—' if b is None else len(b)} |")

    def fmt(n) -> str:
        return f"{n:,}" if isinstance(n, int) else "—"

    old_label, new_label = ("Original", "Digest trial" if kind == "trial" else "Test") if same_day else (prev_day, "Today")
    head = (f"**Digest trial vs this morning's original brief.** " if same_day
            else f"**Compared with the {prev_day} brief.** ")
    lines = [head + summary, ""]
    if stats.get("check_added"):
        lines += [f"**Completeness check added:** {stats['check_added']}", ""]
    if flags or concerns:
        title = "## Missing or wrong in the digest version" if same_day else "## Look at these"
        lines += [title, ""] + [f"- {f}" for f in flags + concerns] + [""]
    else:
        lines += ["Nothing missing." if same_day else "Nothing unexpected.", ""]
    if extra:
        lines += ["## Only in the digest version" if same_day else "## New today", ""] + [f"- {e}" for e in extra] + [""]
    lines += ["## Sections (bullets)", "", f"| Section | {old_label} | {new_label} |", "|---|---|---|"] + rows + [""]
    lines += ["## Run", "", f"| | {old_label} | {new_label} |", "|---|---|---|",
              f"| Input tokens | {fmt(prev_stats.get('input'))} | {fmt(stats.get('input'))} |",
              f"| Uncached | {fmt(prev_stats.get('input', 0) - prev_stats.get('cached', 0) if prev_stats else None)} | "
              f"{fmt(stats.get('input', 0) - stats.get('cached', 0))} |",
              f"| Output tokens | {fmt(prev_stats.get('output'))} | {fmt(stats.get('output'))} |",
              f"| Rounds | {fmt(prev_stats.get('turns'))} | {fmt(stats.get('turns'))} |",
              f"| Web searches | {fmt(prev_stats.get('searches'))} | {fmt(stats.get('searches'))} |",
              f"| Length (chars) | {len(prev):,} | {len(text):,} |", ""]
    return "\n".join(lines), len(flags) + len(concerns)
