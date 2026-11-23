import datetime as dt
import json

import substack_check as sc

SAT = dt.date(2026, 11, 28)
TUE = dt.date(2026, 11, 24)


def snap(views, subs=7, followers=4, likes=0, comments=0, posts=None):
    """One post ("The Way Home", Nov 22) with `views`, plus any extra posts."""
    p = {"1": {"title": "The Way Home", "date": "2026-11-22", "views": views, "comments": comments,
               "restacks": 0, "likes": likes}}
    p.update(posts or {})
    return {"subs": subs, "followers": followers, "posts": p}


def history(day, *views_by_day_back, **kw):
    """views_by_day_back[0] is `day`, [1] the day before, ..."""
    return {(day - dt.timedelta(days=i)).isoformat(): snap(v, **kw) for i, v in enumerate(views_by_day_back)}


def test_quiet_day_is_silent():
    assert sc.daily_lines(TUE, history(TUE, 12, 11, 11, 10, 10, 10, 10, 10, 10, 10)) == []


def test_real_history_fires_on_post_days_only():
    """Oct 17-Nov 23 daily views, from the dashboard's traffic chart."""
    daily = {"2026-10-18": 9, "2026-10-19": 1, "2026-10-20": 49, "2026-10-21": 4, "2026-10-22": 4,
             "2026-10-30": 1, "2026-11-02": 1, "2026-11-13": 1, "2026-11-21": 7, "2026-11-22": 8}
    h, total, day = {}, 0, dt.date(2026, 10, 17)
    fired = []
    while day <= dt.date(2026, 11, 23):
        total += daily.get(day.isoformat(), 0)
        h[day.isoformat()] = snap(total, posts={})
        if sc.daily_lines(day, h):
            fired.append(day.isoformat())
        day += dt.timedelta(days=1)
    assert fired == ["2026-10-18", "2026-10-20", "2026-11-21", "2026-11-22"]


def test_post_day_line():
    lines = sc.daily_lines(TUE, history(TUE, 18, 10, 10))
    assert lines == ["📈 Substack: 8 views since yesterday; mostly “The Way Home” (+8)."]


def test_four_views_is_silent():
    assert sc.daily_lines(TUE, history(TUE, 14, 10)) == []


def test_busy_account_needs_three_times_usual():
    h = history(TUE, 210, 200, *[200 - 10 * i for i in range(1, 9)])  # usual ~10/day
    assert sc.daily_lines(TUE, h) == []  # 10 views: normal for it now
    h[TUE.isoformat()] = snap(240)
    assert "40 views since yesterday (usual ~10/day)" in sc.daily_lines(TUE, h)[0]


def test_one_new_subscriber_or_follower():
    h = history(TUE, 10, 10)
    h[TUE.isoformat()]["subs"] = 8
    assert "+1 subscribers (now 8)" in sc.daily_lines(TUE, h)[0]
    h = history(TUE, 10, 10)
    h[TUE.isoformat()]["followers"] = 5
    assert "+1 followers (now 5)" in sc.daily_lines(TUE, h)[0]


def test_any_comment_but_not_a_lone_like():
    assert "1 new comments" in sc.daily_lines(TUE, {**history(TUE, 10, 10), TUE.isoformat(): snap(10, comments=1)})[0]
    assert sc.daily_lines(TUE, {**history(TUE, 10, 10), TUE.isoformat(): snap(10, likes=1)}) == []


def test_old_post_dug_up():
    old = lambda v: {"9": {"title": "The Other House", "date": "2026-10-20", "views": v, "comments": 0, "restacks": 0, "likes": 0}}
    h = {"2026-11-23": snap(10, posts=old(41)), TUE.isoformat(): snap(10, posts=old(44))}
    assert "“The Other House” (+3)" in sc.daily_lines(TUE, h)[0]
    h[TUE.isoformat()] = snap(10, posts=old(43))  # +2: a stray view or two
    assert sc.daily_lines(TUE, h) == []


def test_no_previous_snapshot_is_silent():
    assert sc.daily_lines(TUE, history(TUE, 50)) == []


def test_weekly_line():
    h = history(SAT, *[50 - 3 * i for i in range(8)])
    new = {"2": {"title": "New One", "date": "2026-11-26", "views": 5, "comments": 1, "restacks": 0, "likes": 2}}
    h[SAT.isoformat()] = snap(50, subs=9, posts=new)
    line = sc.weekly_line(SAT, h)
    assert "last 7 days: 26 views" in line and "+2 subscribers (now 9" in line
    assert "published “The Way Home”, “New One”" in line and "3 new comments/restacks/likes" in line


def test_weekly_first_day():
    assert "weekly numbers start next Saturday" in sc.weekly_line(SAT, history(SAT, 10))


def test_run_saves_snapshot_and_adds_weekly_on_saturday_only(tmp_path):
    (tmp_path / "substack.json").write_text(json.dumps({"2026-01-01": snap(1)}))  # older than KEEP_DAYS
    r = sc.run(tmp_path, SAT, snap(12))
    saved = json.loads((tmp_path / "substack.json").read_text())
    assert list(saved) == [SAT.isoformat()]
    assert r["problem"] == "" and len(r["lines"]) == 1 and "weekly numbers" in r["lines"][0]
    assert sc.run(tmp_path, SAT + dt.timedelta(days=1), snap(13))["lines"] == []  # Sunday, quiet
    assert sc.run(tmp_path, SAT + dt.timedelta(days=7), snap(15))["lines"][0].startswith("Substack, last 7 days: 3 views, +0 subscribers")


def test_not_configured_is_silent(tmp_path, monkeypatch):
    monkeypatch.delenv("SUBSTACK_SID", raising=False)
    assert sc.brief_text(tmp_path) == ""


def test_expired_cookie_is_a_connection_problem(tmp_path, monkeypatch):
    monkeypatch.setenv("SUBSTACK_SID", "old")

    def boom(sid, pub):
        raise sc.AuthError("HTTP 401")
    monkeypatch.setattr(sc, "fetch", boom)
    text = sc.brief_text(tmp_path)
    assert "Substack connection problem" in text and "substack.sid" in text


def test_network_error_is_silent(tmp_path, monkeypatch):
    monkeypatch.setenv("SUBSTACK_SID", "x")

    def boom(sid, pub):
        raise OSError("dns")
    monkeypatch.setattr(sc, "fetch", boom)
    assert sc.brief_text(tmp_path) == ""
