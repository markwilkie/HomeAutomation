import datetime

import todays_events as te

THU, FRI, SAT, SUN, WED = (datetime.date(2026, 11, d) for d in (19, 20, 21, 22, 25))
NEXT_THU = datetime.date(2026, 11, 26)

THURSDAY_EMAIL = """## 🗓️ This weekend
**Fri Nov 20**
- **7pm** Stickshift Annie, Third Place Commons (LFP) [📅](https://calendar.google.com/x)
  - Dinner nearby: Pizzeria, casual
**Sat Nov 21**
- **9am** Edmonds Holiday Market [📅](https://calendar.google.com/y)
**Sun Nov 22**
- **10am** Holiday Market, second day

## 📆 Next week
**Wed Nov 25**
- **6pm** Tree lighting, Kenmore
**Thu Nov 26**
- **9am** Turkey Trot, Shoreline

## 🎟️ Plan ahead
- **Dec 2** Something, Seattle
"""

LAST_WEEK = """## 📆 Next week
**Thu Nov 19**
- **7pm** Trivia night, Third Place Commons
"""

SATURDAY_EMAIL = """## 🆕 New since Thursday
- New: Cookie walk, Sun
## 🗓️ This weekend
**Today, Sat Nov 21**
- **9am** Edmonds Holiday Market
**Sun Nov 22**
- **10am** Holiday Market, second day
- 🆕 **1pm** Cookie walk, LFP
"""

BRIEF = """Sober: 47 days

## 🎯 Needs you
- thing

## 📅 Today: Fri Nov 20
- 10am Breakfast

## 💵 Money
- BECU
"""


def save(state, day, text):
    (state / f"events-{day.isoformat()}.md").write_text(text, encoding="utf-8")


def test_weekend_and_next_week_days_come_from_thursdays_email(tmp_path):
    save(tmp_path, THU, THURSDAY_EMAIL)
    fri = te.section(tmp_path, FRI)
    assert fri.startswith(te.HEADING) and "Stickshift Annie" in fri
    assert "Dinner nearby" not in fri and "Edmonds" not in fri
    assert "Tree lighting" in te.section(tmp_path, WED)


def test_saturday_with_todays_email_is_just_the_pointer(tmp_path):
    save(tmp_path, THU, THURSDAY_EMAIL)
    save(tmp_path, SAT, SATURDAY_EMAIL)
    assert te.section(tmp_path, SAT) == f"{te.HEADING}\n- {te.POINTER[5]}"


def test_saturday_without_todays_email_uses_thursdays(tmp_path):
    save(tmp_path, THU, THURSDAY_EMAIL)
    assert "Edmonds Holiday Market" in te.section(tmp_path, SAT)


def test_sunday_uses_saturdays_refresh_including_new_events(tmp_path):
    save(tmp_path, THU, THURSDAY_EMAIL)
    save(tmp_path, SAT, SATURDAY_EMAIL)
    assert "Cookie walk" in te.section(tmp_path, SUN)


def test_thursday_shows_last_weeks_picks_then_pointer(tmp_path):
    save(tmp_path, datetime.date(2026, 11, 12), LAST_WEEK)
    save(tmp_path, THU, THURSDAY_EMAIL)
    lines = te.section(tmp_path, THU).splitlines()
    assert "Trivia night" in lines[1] and lines[-1] == f"- {te.POINTER[3]}"
    # next Thursday: the Thanksgiving pick from this week's Next week
    save(tmp_path, NEXT_THU, "## 🗓️ This weekend\n**Fri Nov 27**\n- x\n")
    assert "Turkey Trot" in te.section(tmp_path, NEXT_THU)


def test_nothing_saved_or_nothing_today(tmp_path):
    assert "No Weekend & Ahead email" in te.section(tmp_path, FRI)
    save(tmp_path, THU, "## 🗓️ This weekend\n**Sat Nov 21**\n- x\n")
    assert "Nothing picked for today." in te.section(tmp_path, FRI)
    save(tmp_path, datetime.date(2026, 11, 1), THURSDAY_EMAIL)  # too old to count
    assert "Nothing picked" in te.section(tmp_path, FRI)


def test_nov_2_does_not_match_nov_20():
    assert te.day_items("**Fri Nov 20**\n- x\n", datetime.date(2026, 11, 2)) == []


def test_insert_at_the_end():
    out = te.insert(BRIEF, f"{te.HEADING}\n- a")
    assert out.endswith(f"- BECU\n\n{te.HEADING}\n- a\n")
    assert te.insert(out, "again") == out
