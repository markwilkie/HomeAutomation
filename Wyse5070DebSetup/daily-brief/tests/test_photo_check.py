import datetime as dt

import photo_check as pc

NOW = dt.datetime(2026, 11, 21, 14, 30, tzinfo=dt.timezone.utc)


def ago(**kw):
    return (NOW - dt.timedelta(**kw)).isoformat()


def ahead(**kw):
    return (NOW + dt.timedelta(**kw)).isoformat()


GOOD_BACKUP = {"ok": True, "last_success": ago(hours=12), "files": 1300, "bytes": 15_000_000_000, "not_in_onedrive": 0}
GOOD_EDITS = {"ok": True, "last_run": ago(hours=13), "edited_total": 70, "saved_total": 70, "missing": [],
              "sign_in_expires": ahead(days=10)}


def test_all_good_is_silent():
    assert pc.brief_lines(NOW, GOOD_BACKUP, GOOD_EDITS) == []


def test_backup_failed_last_night():
    b = dict(GOOD_BACKUP, ok=False, error="rclone sync failed", last_success=ago(hours=36))
    lines = pc.brief_lines(NOW, b, GOOD_EDITS)
    assert len(lines) == 1 and "backup to OneDrive failed" in lines[0]


def test_backup_stale():
    lines = pc.brief_lines(NOW, dict(GOOD_BACKUP, last_success=ago(hours=60)), GOOD_EDITS)
    assert "hasn't succeeded since" in lines[0]


def test_sign_in_expiring_and_expired():
    soon = pc.brief_lines(NOW, GOOD_BACKUP, dict(GOOD_EDITS, sign_in_expires=ahead(days=2)))
    assert len(soon) == 1 and "sign-in expires" in soon[0] and "Renew Lightroom sign-in" in soon[0]
    gone = pc.brief_lines(NOW, GOOD_BACKUP, dict(GOOD_EDITS, sign_in_expires=ago(hours=1), last_run=ago(hours=60)))
    assert len(gone) == 1 and "has expired" in gone[0]  # no separate "hasn't run" line


def test_missing_edits_only_after_grace():
    fresh = dict(GOOD_EDITS, ok=False, saved_total=69, missing=[{"name": "WJD01503", "since": ago(hours=12)}])
    assert pc.brief_lines(NOW, GOOD_BACKUP, fresh) == []
    old = dict(fresh, missing=[{"name": "WJD01503", "since": ago(hours=40)}, {"name": "WJD01510", "since": ago(hours=40)}])
    lines = pc.brief_lines(NOW, GOOD_BACKUP, old)
    assert lines == ["📷 2 edited photos not saved to the NAS: WJD01503, WJD01510"]


def test_edit_saver_not_run():
    lines = pc.brief_lines(NOW, GOOD_BACKUP, dict(GOOD_EDITS, last_run=ago(hours=50), error="NAS unreachable"))
    assert "edit saver hasn't run since" in lines[0] and "NAS unreachable" in lines[0]


def test_not_set_up_is_silent_in_brief_but_shown_weekly(tmp_path, monkeypatch):
    monkeypatch.setattr(pc, "STATUS_DIR", str(tmp_path))
    assert pc.brief_lines(NOW) == []
    assert pc.weekly_line(NOW) == "Photos: backup not set up; edit saver not set up"


def test_weekly_summary():
    line = pc.weekly_line(NOW, GOOD_BACKUP, GOOD_EDITS)
    assert line.startswith("Photos: backed up nightly ✔") and "edits 70 of 70 ✔" in line and "good until" in line
    bad = pc.weekly_line(NOW, dict(GOOD_BACKUP, ok=False), dict(GOOD_EDITS, sign_in_expires=ago(days=1)))
    assert "backup NOT OK ✘" in bad and "EXPIRED" in bad
