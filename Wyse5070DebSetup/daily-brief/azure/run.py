#!/usr/bin/env python3
"""Run one of this project's emails end to end (user, 2026-11-14: one runner).

    run.py brief [--dry-run] [--only-me]        the daily brief (cron 6:30am via run.sh)
    run.py events [full|refresh] [--dry-run]    Weekend & Ahead (cron Thu / Sat 5:00am via run_events.sh)

Every job does the same steps, configured in shared/jobs.json:
  1. replies.py: apply reply commands sent since the last run (never blocks).
  2. Build the prompt from the job's prompt files, plus its Azure-only notes.
  3. Generate on Azure OpenAI (brief.generate_brief), with the job's prefetch.
  4. On failure, hand the same job to the Claude fallback (claude/run.sh,
     deployed in daily-brief-cc) and stop.
  5. Expand calendar links; for the brief, add "🎉 Events today" from the
     saved events emails (todays_events.py); append the reply footer, send
     to Mark + also_to.
  6. Log the run's tokens and searches to ../state/usage.csv.
  7. Brief only: save it in ../state/briefs/ and compare with the previous
     one (brief_compare.py); the report goes to Mark only.

--only-me: a test send to Mark alone, subject marked [test]; logged as
"brief-test", saved as briefs/<date>-test.md (never tomorrow's baseline).

Events state: each events email is kept in ../state/events-<date>.md; a
refresh builds on this week's Thursday file (or runs full without one), a
full run uses the newest file from the last 8 days for "Still running".
"""

from __future__ import annotations

import argparse
import asyncio
import csv
import datetime
import json
import os
import subprocess
import sys
from pathlib import Path

import brief
import brief_compare
import gcal_links
import mail_guide
import replies
import todays_events

APP = Path(__file__).resolve().parent
STATE = APP.parent / "state"
CLAUDE_RUN = Path(os.environ.get("CLAUDE_FALLBACK", "/mnt/data/appdata/daily-brief-cc/app/run.sh"))


def project_file(name: str) -> Path:
    """Deployed, everything sits next to run.py; in the repo (tests) shared
    files and prompts are in ../shared and ../prompts."""
    for d in (APP, APP.parent / "shared", APP.parent / "prompts"):
        if (d / name).exists():
            return d / name
    raise FileNotFoundError(name)


CONFIG = json.loads(project_file("jobs.json").read_text(encoding="utf-8"))


def log(msg: str) -> None:
    print(f"[{datetime.datetime.now().astimezone().isoformat(timespec='seconds')}] {msg}", file=sys.stderr, flush=True)


def events_plan(mode: str | None, today: datetime.date) -> tuple[str, Path | None]:
    """(mode, previous email file) for an events run."""
    mode = mode or {3: "full", 5: "refresh"}.get(today.weekday())
    if not mode:
        sys.exit("Not Thursday or Saturday; pass full or refresh")
    if mode == "refresh":
        thursday = today - datetime.timedelta(days=(today.weekday() - 3) % 7)
        previous = STATE / f"events-{thursday.isoformat()}.md"
        if previous.exists():
            return "refresh", previous
        log("No Thursday events email found; doing a full run instead")
        mode = "full"
    cutoff = today - datetime.timedelta(days=8)
    earlier = sorted(p for p in STATE.glob("events-*.md")
                     if cutoff.isoformat() <= p.stem.removeprefix("events-") < today.isoformat())
    return mode, (earlier[-1] if earlier else None)


def prompt_text(job: dict, azure: bool) -> str:
    files = job["prompts"] + (job.get("azure_prompts", []) if azure else [])
    # mail_guide.md is generated at deploy (for the Claude fallback); render it if absent.
    return "\n".join(mail_guide.render() if f == "mail_guide.md" and not (APP / f).exists()
                     else project_file(f).read_text(encoding="utf-8") for f in files)


def footer(job: dict) -> str:
    return CONFIG["footer"].format(what=job["footer_what"])


def send(subject: str, body: str, only_me: bool = False) -> None:
    args = [sys.executable, str(project_file("send_mail.py")), "--subject", subject]
    for addr in ([] if only_me else CONFIG.get("also_to", [])):
        args += ["--also-to", addr]
    subprocess.run(args, input=body, text=True, check=True)


def record_usage(job_name: str, chars: int) -> None:
    u = brief.LAST_USAGE
    path = STATE / "usage.csv"
    new = not path.exists()
    with path.open("a", newline="") as f:
        w = csv.writer(f)
        if new:
            w.writerow(["date", "job", "input", "cached", "output", "searches", "chars"])
        w.writerow([datetime.date.today().isoformat(), job_name, u.get("input", 0), u.get("cached", 0),
                    u.get("output", 0), u.get("searches", 0), chars])


def fallback(job_name: str, job: dict, request: str | None, subject: str, save_to: Path | None, dry: bool,
             only_me: bool = False) -> None:
    """Hand the job to Claude Code; replaces this process."""
    log(f"Azure run failed; falling back to Claude Code ({CLAUDE_RUN})")
    env = dict(os.environ,
               BRIEF_PROMPT_FILES=" ".join(job["prompts"]),
               BRIEF_SUBJECT=("[test] " if only_me else "") + job.get("fallback_subject", subject),
               BRIEF_FAIL_SUBJECT=job["fail_subject"],
               BRIEF_FOOTER=footer(job),
               BRIEF_ALSO_TO="" if only_me else " ".join(CONFIG.get("also_to", [])),
               BRIEF_EVENTS_STATE=str(STATE) if job.get("todays_events") else "")
    if request:
        env["BRIEF_REQUEST"] = request
    if save_to:
        env["BRIEF_SAVE_TO"] = str(save_to)
    os.execve(str(CLAUDE_RUN), [str(CLAUDE_RUN)] + (["--dry-run"] if dry else []), env)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("job", choices=sorted(CONFIG["jobs"]))
    p.add_argument("mode", nargs="?", choices=["full", "refresh"], help="events only")
    p.add_argument("--dry-run", action="store_true", help="print instead of emailing; nothing saved or changed")
    p.add_argument("--only-me", action="store_true", help="test send to Mark only, subject marked [test]")
    args = p.parse_args()
    job = CONFIG["jobs"][args.job]
    today = datetime.datetime.now(brief.TZ).date()
    STATE.mkdir(exist_ok=True)

    try:
        asyncio.run(replies.main(args.dry_run))
    except Exception as exc:  # never blocks the email
        log(f"replies failed; continuing: {type(exc).__name__}: {exc}")

    request, save_to, subject = None, None, job["subject"]
    if args.only_me:
        subject = "[test] " + subject
    if args.job == "events":
        mode, previous = events_plan(args.mode, today)
        request = brief.events_request(mode, previous)
        subject = (job["refresh_subject"] if mode == "refresh" else job["subject"]).format(date=today.strftime("%a %b %-d"))
        save_to = None if args.dry_run else STATE / f"events-{today.isoformat()}.md"
        log(f"Events email ({mode}) via Azure OpenAI; previous: {previous or 'none'}")
    else:
        log("Generating brief via Azure OpenAI...")

    try:
        text = asyncio.run(brief.generate_brief(prompt_text(job, azure=True), request, tuple(job.get("prefetch", []))))
        if not text.strip():
            raise RuntimeError("model returned an empty email")
    except BaseException as exc:
        cause = brief.root_cause(exc)
        log(f"ERROR: {type(cause).__name__}: {cause}")
        fallback(args.job, job, request, subject, save_to, args.dry_run, args.only_me)
        return

    text = gcal_links.expand(text)
    if job.get("todays_events"):
        text = todays_events.insert(text, todays_events.section(STATE, today))
    log(f"Generated {args.job}: {len(text)} chars")
    if args.dry_run:
        print(f"\n----- {subject} (dry run, not emailed or saved) -----\n\n{text}")
        return
    record_usage(args.job + ("-test" if args.only_me else ""), len(text))
    if save_to:  # without the footer: the next run builds on this
        save_to.write_text(text + "\n", encoding="utf-8")
    send(subject, f"{text}\n\n---\n\n{footer(job)}\n", args.only_me)
    if job.get("compare"):
        compare(text, today, args.only_me)


def compare(text: str, today: datetime.date, test: bool) -> None:
    """Save the brief and send the day-to-day comparison to Mark (never blocks)."""
    try:
        stats = dict(brief.LAST_USAGE)
        body, flags = brief_compare.report(STATE, today, text, stats, test)
        brief_compare.save(STATE, today, text, stats, test)
        log(f"Compare: {flags} flag(s)")
        if test or flags or today.isoformat() <= CONFIG.get("compare_email_daily_until", ""):
            what = f"{flags} thing{'s' if flags != 1 else ''} to look at" if flags else "nothing unexpected"
            send(f"{'[test] ' if test else ''}Brief check {today.strftime('%a %b %-d')}: {what}", body, only_me=True)
    except Exception as exc:
        log(f"compare failed: {type(exc).__name__}: {exc}")


if __name__ == "__main__":
    main()
