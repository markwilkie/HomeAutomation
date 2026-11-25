"""Day-to-day brief comparison (user, 2026-11-25: catch unexpected changes
after the email/WhatsApp digests went in).

After each brief is sent, it's saved in state/briefs/<date>.md with its run
stats, and compared with the previous saved brief (up to 3 days back):
  - code checks: a core section missing, Needs you / Waiting on others
    shrinking sharply, the brief much shorter, many more rounds or tokens;
  - one small model call (low effort, ~10k tokens): which items vanished
    with no sign they were settled or aged out under prompt.md's rules,
    and anything else that looks off.
The report goes to Mark only (never also_to): every day until
jobs.json "compare_email_daily_until", then only when something's flagged.
A test run (run.py --only-me) is saved as <date>-test.md and compared with
the latest real brief, without becoming tomorrow's baseline.
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

COMPARE_PROMPT = """You check a personal daily brief email against the previous one, to catch a
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
{"concerns": ["<one line each, naming the item>"], "summary": "<one line>"}"""


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
            flags.append(f"Section missing: {name} (it was there yesterday)")
    for name in WATCHED:
        before, now = len(_find(p, name) or []), len(_find(t, name) or [])
        if before >= 4 and now <= before // 2:
            flags.append(f"{name} went from {before} items to {now}")
    if len(prev) > 2000 and len(today) < 0.6 * len(prev):
        flags.append(f"Brief is much shorter: {len(today):,} chars vs {len(prev):,}")
    if stats.get("turns", 0) >= 10:
        flags.append(f"Took {stats['turns']} rounds with the model (expected 4-6)")
    if stats.get("input", 0) > 600_000:
        flags.append(f"Used {stats['input']:,} input tokens (expected under ~350k with the digests)")
    return flags


def model_check(today: str, prev: str, prev_day: str) -> dict:
    from openai import OpenAI  # deployed venv; tests stub this function

    client = OpenAI(base_url=os.environ["AZURE_OPENAI_BASE_URL"], api_key=os.environ["AZURE_OPENAI_API_KEY"])
    r = client.responses.create(
        model=os.environ.get("AZURE_OPENAI_DEPLOYMENT", "gpt-6.1-sol"), reasoning={"effort": "low"},
        max_output_tokens=3000, instructions=COMPARE_PROMPT,
        input=f"PREVIOUS BRIEF ({prev_day}):\n\n{prev}\n\nTODAY'S BRIEF:\n\n{today}")
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


def save(state: Path, day: datetime.date, text: str, stats: dict, test: bool = False) -> Path:
    briefs = state / "briefs"
    briefs.mkdir(parents=True, exist_ok=True)
    name = f"{day.isoformat()}{'-test' if test else ''}"
    (briefs / f"{name}.md").write_text(text + "\n", encoding="utf-8")
    (briefs / f"{name}.json").write_text(json.dumps(stats), encoding="utf-8")
    cutoff = (day - datetime.timedelta(days=KEEP_DAYS)).isoformat()
    for p in briefs.iterdir():
        if p.name[:10] < cutoff:
            p.unlink()
    return briefs / f"{name}.md"


def report(state: Path, day: datetime.date, text: str, stats: dict, test: bool = False,
           use_model: bool = True) -> tuple[str, int]:
    """(Markdown report, number of flags). Compares with the latest real brief before `day`
    (a test run compares with today's real one if there is one)."""
    briefs = state / "briefs"
    prev_day, prev_path = None, None
    if test and (briefs / f"{day.isoformat()}.md").exists():
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
    concerns, summary = [], ""
    if use_model:
        try:
            m = model_check(text, prev, prev_day)
            concerns, summary = [c for c in m.get("concerns") or [] if c], m.get("summary") or ""
        except Exception as exc:  # the code checks still go out
            summary = f"(model check failed: {type(exc).__name__}: {exc})"

    t, p = sections(text), sections(prev)
    rows = []
    for heading in dict.fromkeys([h for h in p if h] + [h for h in t if h]):
        a, b = _find(p, heading), _find(t, heading)
        rows.append(f"| {heading} | {'—' if a is None else len(a)} | {'—' if b is None else len(b)} |")

    def fmt(n) -> str:
        return f"{n:,}" if isinstance(n, int) else "—"

    lines = [f"**Compared with the {prev_day} brief.** " + (summary or ""), ""]
    if stats.get("check_added"):
        lines += [f"**Completeness check added:** {stats['check_added']}", ""]
    if flags or concerns:
        lines += ["## Look at these", ""] + [f"- {f}" for f in flags + concerns] + [""]
    else:
        lines += ["Nothing unexpected.", ""]
    lines += ["## Sections (bullets)", "", f"| Section | {prev_day} | {day.isoformat()}{' test' if test else ''} |",
              "|---|---|---|"] + rows + [""]
    lines += ["## Run", "", "| | Before | Now |", "|---|---|---|",
              f"| Input tokens | {fmt(prev_stats.get('input'))} | {fmt(stats.get('input'))} |",
              f"| Uncached | {fmt(prev_stats.get('input', 0) - prev_stats.get('cached', 0) if prev_stats else None)} | "
              f"{fmt(stats.get('input', 0) - stats.get('cached', 0))} |",
              f"| Output tokens | {fmt(prev_stats.get('output'))} | {fmt(stats.get('output'))} |",
              f"| Rounds | {fmt(prev_stats.get('turns'))} | {fmt(stats.get('turns'))} |",
              f"| Web searches | {fmt(prev_stats.get('searches'))} | {fmt(stats.get('searches'))} |",
              f"| Length (chars) | {len(prev):,} | {len(text):,} |", ""]
    lines += ["_Rollback if needed: add `BRIEF_DIGESTS=off` to `/mnt/data/appdata/daily-brief-oai/config/.env`; "
              "the next brief gathers the old way._"]
    return "\n".join(lines), len(flags) + len(concerns)
