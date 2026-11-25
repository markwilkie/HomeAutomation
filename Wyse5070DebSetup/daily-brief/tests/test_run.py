import datetime
import os

import pytest

import run

THU = datetime.date(2026, 11, 19)
SAT = datetime.date(2026, 11, 21)


@pytest.fixture
def state(tmp_path, monkeypatch):
    monkeypatch.setattr(run, "STATE", tmp_path)
    return tmp_path


def test_jobs_config_is_complete():
    assert run.CONFIG["also_to"]
    assert "{what}" in run.CONFIG["footer"]
    for name, job in run.CONFIG["jobs"].items():
        for key in ("prompts", "subject", "fail_subject", "footer_what"):
            assert job.get(key), (name, key)
        for f in job["prompts"] + job.get("azure_prompts", []):
            if f != "mail_guide.md":  # generated
                assert run.project_file(f).exists(), f


def test_prompt_text_adds_azure_notes_and_mail_guide():
    job = run.CONFIG["jobs"]["brief"]
    azure, claude = run.prompt_text(job, azure=True), run.prompt_text(job, azure=False)
    assert "## Where to find mail" in azure and "## Where to find mail" in claude
    assert "## Notes for Azure runs" in azure and "## Notes for Azure runs" not in claude
    events = run.prompt_text(run.CONFIG["jobs"]["events"], azure=True)
    assert events.startswith("# Weekend & Ahead") and "## Notes for Azure runs" not in events


def test_footers_name_the_job():
    assert "To adjust the brief" in run.footer(run.CONFIG["jobs"]["brief"])
    assert "To adjust these picks" in run.footer(run.CONFIG["jobs"]["events"])


def test_events_plan_by_weekday(state):
    assert run.events_plan(None, THU) == ("full", None)
    (state / "events-2026-11-19.md").write_text("thursday")
    assert run.events_plan(None, SAT) == ("refresh", state / "events-2026-11-19.md")
    with pytest.raises(SystemExit):
        run.events_plan(None, datetime.date(2026, 11, 20))  # Friday


def test_refresh_without_thursday_runs_full_on_last_saturday(state):
    (state / "events-2026-11-14.md").write_text("last saturday")
    assert run.events_plan("refresh", SAT) == ("full", state / "events-2026-11-14.md")


def test_full_run_ignores_old_and_todays_files(state):
    (state / "events-2026-11-07.md").write_text("too old")
    (state / "events-2026-11-19.md").write_text("today")
    assert run.events_plan("full", THU) == ("full", None)


def test_fallback_passes_the_job_to_claude(monkeypatch, tmp_path):
    seen = {}
    monkeypatch.setattr(run.os, "execve", lambda path, argv, env: seen.update(path=path, argv=argv, env=env))
    job = run.CONFIG["jobs"]["events"]
    run.fallback("events", job, "REQUEST", "Weekend & Ahead: Thu Nov 19", tmp_path / "e.md", dry=True)
    env = seen["env"]
    assert seen["argv"][-1] == "--dry-run"
    assert env["BRIEF_PROMPT_FILES"] == "events.md mail_guide.md"
    assert env["BRIEF_SUBJECT"] == "Weekend & Ahead: Thu Nov 19"
    assert env["BRIEF_FAIL_SUBJECT"].startswith("Weekend & Ahead FAILED")
    assert env["BRIEF_REQUEST"] == "REQUEST" and env["BRIEF_SAVE_TO"].endswith("e.md")
    assert "jennie@" in env["BRIEF_ALSO_TO"]
    assert env["BRIEF_EVENTS_STATE"] == ""  # only the brief gets "Events today"
    run.fallback("brief", run.CONFIG["jobs"]["brief"], None, "Daily Brief", None, dry=True)
    assert seen["env"]["BRIEF_EVENTS_STATE"] == str(run.STATE)


def test_events_request_includes_previous_email(tmp_path):
    import brief
    prev = tmp_path / "events-2026-11-19.md"
    prev.write_text("## 🗓️ This weekend\n- thing")
    msg = brief.events_request("refresh", prev)
    assert "REFRESH run" in msg and "Thursday's email (2026-11-19)" in msg and "- thing" in msg
    assert "Last Saturday's email" in brief.events_request("full", prev)


def test_only_me_never_reaches_also_to(monkeypatch):
    seen = {}
    monkeypatch.setattr(run.os, "execve", lambda path, argv, env: seen.update(env=env))
    run.fallback("brief", run.CONFIG["jobs"]["brief"], None, "[test] Daily Brief", None, dry=False, only_me=True)
    assert seen["env"]["BRIEF_ALSO_TO"] == "" and seen["env"]["BRIEF_SUBJECT"].startswith("[test] ")
    sent = {}
    monkeypatch.setattr(run.subprocess, "run", lambda args, **kw: sent.update(args=args))
    run.send("[test] Daily Brief", "body", only_me=True)
    assert "--also-to" not in sent["args"]
    run.send("Daily Brief", "body")
    assert "--also-to" in sent["args"]
