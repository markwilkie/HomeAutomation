"""Substack stats for the daily brief (user, 2026-11-23: weekly update, plus a
daily check that only speaks up when something is blowing up).

Reads Mark's own publication through Substack's internal web API (there is no
public one), signed in with the `substack.sid` cookie from a browser session:
  SUBSTACK_SID  the cookie's value     (daily-brief-oai config/.env)
  SUBSTACK_PUB  subdomain, default "markwilkie"

Each brief run saves one snapshot per day in state/substack.json (cumulative
views per post, subscribers, followers) and compares against earlier days:
  daily     a "📈" line only when something moved (see the thresholds); nothing otherwise
  Saturday  one summary line for the past 7 days, always
  expired cookie -> a connection problem with the fix steps

Two small HTTP GETs, no model calls: costs nothing on a quiet day.

  python3 substack_check.py [--state DIR]   -> print today's lines (and save the snapshot)
"""
from __future__ import annotations

import datetime as dt
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path

# Thresholds from the account's history (user, 2026-11-23): Oct 17-Nov 23 had
# 26 of 37 days at zero views, and only Oct 20 (49) reached the first bar of
# 25 views / 3x usual / 2 subs a day. These fire on ~6 of those 37 days,
# mostly the days after a post goes out.
SPIKE_MIN_VIEWS = 5     # a day's views must be at least this...
SPIKE_FACTOR = 3        # ...and this many times the usual day (avg of the last 7), once there is a usual
OLD_POST_DAYS = 7       # a post older than this...
OLD_POST_VIEWS = 3      # ...getting this many views in a day means someone dug it up
WEEKLY_WEEKDAY = 5      # Saturday: the weekly summary, always
KEEP_DAYS = 60
UA = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0 Safari/537.36"

FIX_STEPS = ("1. In Chrome, signed in to substack.com: DevTools (F12) → Application → Cookies → "
             "https://substack.com → copy the value of `substack.sid`. "
             "2. `ssh mwilkie@192.168.15.30` and set `SUBSTACK_SID=<value>` in "
             "`/mnt/data/appdata/daily-brief-oai/config/.env`.")


class AuthError(Exception):
    pass


def _get(url: str, sid: str):
    req = urllib.request.Request(url, headers={"Cookie": f"substack.sid={sid}", "User-Agent": UA,
                                               "Accept": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            body = r.read().decode("utf-8")
    except urllib.error.HTTPError as e:
        if e.code in (401, 403):
            raise AuthError(f"HTTP {e.code}") from e
        raise
    try:
        return json.loads(body)
    except ValueError as e:  # a login or challenge page instead of JSON
        raise AuthError("got a web page instead of data") from e


def fetch(sid: str, pub: str) -> dict:
    """Today's snapshot: {"subs", "followers", "posts": {id: {...}}}."""
    me = _get("https://substack.com/api/v1/user/profile/self", sid)
    if not me.get("id"):
        raise AuthError("not signed in")
    data = _get(f"https://{pub}.substack.com/api/v1/post_management/published"
                "?offset=0&limit=50&order_by=post_date&order_direction=desc", sid)
    posts = {}
    for p in data.get("posts") or []:
        s = p.get("stats") or {}
        posts[str(p["id"])] = {
            "title": p.get("title") or "(untitled)",
            "date": (p.get("post_date") or "")[:10],
            "views": int(s.get("views") or 0),
            "comments": int(s.get("comments") or p.get("comment_count") or 0),
            "restacks": int(s.get("restacks") or p.get("restacks") or 0),
            "likes": int(s.get("likes") or p.get("reaction_count") or 0),
        }
    return {"subs": int(me.get("subscriberCountNumber") or 0), "followers": int(me.get("followerCount") or 0),
            "posts": posts}


def _views(snap: dict) -> int:
    return sum(p["views"] for p in snap["posts"].values())


def _engagement(snap: dict) -> int:
    return sum(p["comments"] + p["restacks"] + p["likes"] for p in snap["posts"].values())


def _talk(snap: dict) -> int:
    """Comments + restacks: any new one is news (likes alone aren't)."""
    return sum(p["comments"] + p["restacks"] for p in snap["posts"].values())


def _post_gains(new: dict, old: dict, with_date: bool = False) -> list:
    """(views gained, title[, post date]) per post that gained, most first."""
    gains = [(p["views"] - old["posts"].get(pid, {}).get("views", 0), p["title"], p["date"])
             for pid, p in new["posts"].items()]
    gains = sorted((g for g in gains if g[0] > 0), reverse=True)
    return gains if with_date else [g[:2] for g in gains]


def _before(history: dict, day: dt.date, at_least: int, at_most: int):
    """(date, snapshot) of the newest snapshot between at_least and at_most days before `day`."""
    for back in range(at_least, at_most + 1):
        d = (day - dt.timedelta(days=back)).isoformat()
        if d in history:
            return d, history[d]
    return None, None


def daily_lines(day: dt.date, history: dict) -> list[str]:
    today = history.get(day.isoformat())
    prev_day, prev = _before(history, day, 1, 3)
    if not today or not prev:
        return []
    gap = (day - dt.date.fromisoformat(prev_day)).days
    views = _views(today) - _views(prev)
    subs = today["subs"] - prev["subs"]
    followers = today["followers"] - prev["followers"]
    talk = _talk(today) - _talk(prev)
    engagement = _engagement(today) - _engagement(prev)
    # Usual day: average daily views over the 7 days before the previous snapshot.
    _, week_ago = _before(history, dt.date.fromisoformat(prev_day), 7, 9)
    usual = (_views(prev) - _views(week_ago)) / 7 if week_ago else 0
    busy_views = views >= max(SPIKE_MIN_VIEWS, SPIKE_FACTOR * usual * gap)
    old_cutoff = (day - dt.timedelta(days=OLD_POST_DAYS)).isoformat()
    dug_up = [(g, t) for g, t, d in _post_gains(today, prev, with_date=True)
              if d and d < old_cutoff and g >= OLD_POST_VIEWS * gap]
    if not (busy_views or subs > 0 or followers > 0 or talk > 0 or dug_up):
        return []
    since = "since yesterday" if gap == 1 else f"in the last {gap} days"
    parts = [f"{views} views {since}" + (f" (usual ~{usual:.0f}/day)" if week_ago and usual >= 1 else "")]
    top = _post_gains(today, prev)
    if top:
        parts.append(f"mostly “{top[0][1]}” (+{top[0][0]})")
    for g, t in dug_up:
        if not top or t != top[0][1]:
            parts.append(f"older post “{t}” picked up +{g}")
    if subs:
        parts.append(f"{subs:+d} subscribers (now {today['subs']})")
    if followers:
        parts.append(f"{followers:+d} followers (now {today['followers']})")
    if engagement:
        parts.append(f"{engagement} new comments/restacks/likes")
    return ["📈 Substack: " + "; ".join(parts) + "."]


def weekly_line(day: dt.date, history: dict) -> str:
    today = history.get(day.isoformat())
    if not today:
        return ""
    base_day, base = _before(history, day, 7, 10)
    if not base:
        oldest = min(history)
        if oldest == day.isoformat():
            return (f"Substack: {today['subs']} subscribers, {today['followers']} followers; "
                    "weekly numbers start next Saturday.")
        base_day, base = oldest, history[oldest]
    days = (day - dt.date.fromisoformat(base_day)).days
    subs = today["subs"] - base["subs"]
    line = (f"Substack, last {days} days: {_views(today) - _views(base)} views, "
            f"{subs:+d} subscribers (now {today['subs']}, {today['followers']} followers)")
    top = _post_gains(today, base)
    if top:
        line += f"; top post “{top[0][1]}” (+{top[0][0]} views)"
    new_posts = [p["title"] for p in today["posts"].values() if p["date"] > base_day]
    if new_posts:
        line += "; published " + ", ".join(f"“{t}”" for t in new_posts)
    engagement = _engagement(today) - _engagement(base)
    if engagement:
        line += f"; {engagement} new comments/restacks/likes"
    return line + "."


def run(state_dir: Path, day: dt.date | None = None, snapshot: dict | None = None) -> dict:
    """{"lines": [...], "problem": str}. Saves today's snapshot. Silent (all
    empty) when SUBSTACK_SID isn't set, or on a network hiccup (logged)."""
    day = day or dt.date.today()
    sid = os.environ.get("SUBSTACK_SID", "").strip()
    if snapshot is None:
        if not sid:
            return {"lines": [], "problem": ""}
        try:
            snapshot = fetch(sid, os.environ.get("SUBSTACK_PUB", "markwilkie").strip() or "markwilkie")
        except AuthError as e:
            return {"lines": [], "problem": f"Substack sign-in has expired ({e}), so stats aren't being checked. {FIX_STEPS}"}
        except Exception as e:  # Substack down, DNS, etc.: try again tomorrow
            print(f"WARN: substack fetch failed: {type(e).__name__}: {e}", file=sys.stderr)
            return {"lines": [], "problem": ""}
    path = state_dir / "substack.json"
    try:
        history = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        history = {}
    history[day.isoformat()] = snapshot
    cutoff = (day - dt.timedelta(days=KEEP_DAYS)).isoformat()
    history = {d: s for d, s in sorted(history.items()) if d >= cutoff}
    state_dir.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(history, indent=1), encoding="utf-8")

    lines = daily_lines(day, history)
    if day.weekday() == WEEKLY_WEEKDAY:
        weekly = weekly_line(day, history)
        if weekly:
            lines.append(weekly)
    return {"lines": lines, "problem": ""}


def brief_text(state_dir: Path) -> str:
    """The text the brief's prefetch adds to the user message ("" when quiet)."""
    r = run(state_dir)
    out = ""
    if r["problem"]:
        out += (" Substack connection problem (a connection problem under Needs you, fix steps numbered "
                f"exactly as given, ending \"(Substack)\"): {r['problem']}")
    if r["lines"]:
        out += (" Substack stats (the 📈 Substack section, just before News, each line "
                "as its own bullet, word for word): " + " | ".join(r["lines"]))
    return out


if __name__ == "__main__":
    state = Path(sys.argv[sys.argv.index("--state") + 1]) if "--state" in sys.argv else Path(__file__).resolve().parent.parent / "state"
    r = run(state)
    print(r["problem"] or "\n".join(r["lines"]) or "(quiet)")
