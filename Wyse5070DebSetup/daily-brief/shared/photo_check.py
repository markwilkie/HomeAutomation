"""Photo-workflow health for the daily brief and the weekly health check.

Reads the status files written on wyse by setup-photo-sync.sh (nightly backup + Lightroom edit saver):
  /mnt/data/appdata/photo-sync/backup-status.json
  /mnt/data/appdata/photo-sync/edit-saver-status.json

  python3 photo_check.py --brief   -> 0+ "📷 ..." lines for 🎯 Needs you (nothing when all is well)
  python3 photo_check.py --weekly  -> one summary line, always

Pure file reads: never touches the NAS, OneDrive or Adobe, so it can't fail because they're down.
"""
from __future__ import annotations

import datetime as dt
import json
import os
import sys

STATUS_DIR = os.environ.get("PHOTO_SYNC_DIR", "/mnt/data/appdata/photo-sync")
STALE_H = 48          # a job that hasn't succeeded in this long is reported
MISSING_GRACE_H = 36  # an edited photo still unsaved after this long (2 nightly runs) is reported
EXPIRY_WARN_D = 3     # warn this many days before the Lightroom sign-in expires


def _load(name: str):
    try:
        with open(os.path.join(STATUS_DIR, name), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def _when(stamp):
    try:
        t = dt.datetime.fromisoformat(stamp)
        return t if t.tzinfo else t.replace(tzinfo=dt.timezone.utc)
    except (TypeError, ValueError):
        return None


def _day(t: dt.datetime) -> str:
    return t.astimezone().strftime("%a %b %-d") if os.name != "nt" else t.astimezone().strftime("%a %b %d")


def brief_lines(now: dt.datetime | None = None, backup=None, edits=None) -> list[str]:
    now = now or dt.datetime.now(dt.timezone.utc)
    backup = _load("backup-status.json") if backup is None else backup
    edits = _load("edit-saver-status.json") if edits is None else edits
    out = []

    if backup is not None:
        last_ok = _when(backup.get("last_success"))
        if backup.get("ok") is False and backup.get("error"):
            out.append(f"📷 Photo backup to OneDrive failed last night ({backup['error']}): check the wyse dashboard")
        elif last_ok is None or now - last_ok > dt.timedelta(hours=STALE_H):
            since = f"since {_day(last_ok)}" if last_ok else "yet"
            out.append(f"📷 Photo backup to OneDrive hasn't succeeded {since}: check the wyse dashboard")
        if backup.get("ok") and backup.get("not_in_onedrive"):
            out.append(f"📷 {backup['not_in_onedrive']} photos on the NAS aren't in OneDrive yet")

    if edits is not None:
        last_run = _when(edits.get("last_run"))
        expires = _when(edits.get("sign_in_expires"))
        if expires is not None and expires <= now:
            out.append("📷 Lightroom sign-in has expired, so edits aren't being saved to the NAS: "
                       "run Renew Lightroom sign-in (30 s)")
        elif expires is not None and expires - now <= dt.timedelta(days=EXPIRY_WARN_D):
            out.append(f"📷 Lightroom sign-in expires {_day(expires)}: run Renew Lightroom sign-in (30 s)")
        if last_run is None or now - last_run > dt.timedelta(hours=STALE_H):
            why = f": {edits['error']}" if edits.get("error") else ""
            since = f"since {_day(last_run)}" if last_run else "yet"
            if not (expires is not None and expires <= now):  # expiry line already explains it
                out.append(f"📷 Lightroom edit saver hasn't run {since}{why}")
        late = [m["name"] for m in edits.get("missing", [])
                if (_when(m.get("since")) or now) <= now - dt.timedelta(hours=MISSING_GRACE_H)]
        if late:
            shown = ", ".join(late[:5]) + (f" and {len(late) - 5} more" if len(late) > 5 else "")
            out.append(f"📷 {len(late)} edited photo{'s' if len(late) != 1 else ''} not saved to the NAS: {shown}")
    return out


def weekly_line(now: dt.datetime | None = None, backup=None, edits=None) -> str:
    now = now or dt.datetime.now(dt.timezone.utc)
    backup = _load("backup-status.json") if backup is None else backup
    edits = _load("edit-saver-status.json") if edits is None else edits
    parts = []
    if backup is None:
        parts.append("backup not set up")
    else:
        last_ok = _when(backup.get("last_success"))
        good = backup.get("ok") and last_ok and now - last_ok <= dt.timedelta(hours=STALE_H)
        size = f", {backup.get('files', 0)} files / {backup.get('bytes', 0) / 1e9:.1f} GB in OneDrive"
        parts.append(("backed up nightly ✔" if good else "backup NOT OK ✘") + size)
    if edits is None:
        parts.append("edit saver not set up")
    else:
        ok = edits.get("ok") and not brief_lines(now, {"ok": True, "last_success": now.isoformat()}, edits)
        parts.append(f"edits {edits.get('saved_total', 0)} of {edits.get('edited_total', 0)} " + ("✔" if ok else "✘"))
        exp = _when(edits.get("sign_in_expires"))
        if exp:
            parts.append(f"Lightroom sign-in good until {_day(exp)}" if exp > now else "Lightroom sign-in EXPIRED")
    return "Photos: " + "; ".join(parts)


if __name__ == "__main__":
    if "--weekly" in sys.argv:
        print(weekly_line())
    else:
        print("\n".join(brief_lines()))
