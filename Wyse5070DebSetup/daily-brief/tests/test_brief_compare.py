import datetime

import brief_compare as bc

DAY = datetime.date(2026, 11, 26)

PREV = """**Sober: 58 days**

## 🎯 Needs you
- Heather — asked about Sunday (text)
- Todd — waiver due Dec 1 (WhatsApp)
- Google refund (email)
- Dentist form (email)

## ⏳ Waiting on others
- Todd Weed — you asked what time

## 📅 Today: Wed Nov 25
- 9 AM standup
- Rain, 52/44

## 💵 Money
- BECU: Checking $1 · Savings $2 · LOC $0 owed

## 🌎 News & Markets
- Headline
""" + "filler line\n" * 150


def test_sections_parsed_without_emoji():
    s = bc.sections(PREV)
    assert len(s["Needs you"]) == 4 and s["Today: Wed Nov 25"] == ["9 AM standup", "Rain, 52/44"]


def test_code_checks_flag_missing_section_and_shrinking_list():
    today = PREV.replace("## 💵 Money\n- BECU: Checking $1 · Savings $2 · LOC $0 owed\n", "")
    today = today.replace("- Todd — waiver due Dec 1 (WhatsApp)\n- Google refund (email)\n- Dentist form (email)\n", "")
    flags = bc.code_checks(today, PREV, {"turns": 5, "input": 250_000}, {})
    assert any("Money" in f for f in flags) and any("Needs you went from 4 items to 1" in f for f in flags)


def test_quiet_day_has_no_flags():
    assert bc.code_checks(PREV, PREV, {"turns": 5, "input": 250_000}, {}) == []
    assert any("rounds" in f for f in bc.code_checks(PREV, PREV, {"turns": 12}, {}))


def test_report_uses_previous_day_and_test_runs_never_become_baseline(tmp_path, monkeypatch):
    monkeypatch.setattr(bc, "model_check", lambda t, p, d, same_day=False: {"concerns": ["Heather item vanished"], "summary": "ok"})
    body, n = bc.report(tmp_path, DAY, PREV, {}, use_model=True)
    assert n == 0 and "comparisons start tomorrow" in body

    bc.save(tmp_path, DAY - datetime.timedelta(days=1), PREV, {"input": 735_000, "cached": 450_000, "turns": 9})
    body, n = bc.report(tmp_path, DAY, PREV, {"input": 250_000, "cached": 200_000, "turns": 5})
    assert n == 1 and "Heather item vanished" in body and "2026-11-25" in body
    assert "| Input tokens | 735,000 | 250,000 |" in body and "| Uncached | 285,000 | 50,000 |" in body

    bc.save(tmp_path, DAY, PREV, {}, kind="test")
    assert bc._previous(tmp_path / "briefs", DAY + datetime.timedelta(days=1))[0] == "2026-11-25"


def test_model_failure_still_reports(tmp_path, monkeypatch):
    def boom(*a):
        raise RuntimeError("429")
    monkeypatch.setattr(bc, "model_check", boom)
    bc.save(tmp_path, DAY - datetime.timedelta(days=1), PREV, {})
    body, n = bc.report(tmp_path, DAY, PREV, {})
    assert n == 0 and "model check failed" in body and "Nothing unexpected" in body


def test_trial_compares_with_same_mornings_original(tmp_path, monkeypatch):
    seen = {}

    def fake(t, p, d, same_day=False):
        seen["same_day"] = same_day
        return {"concerns": ["Seven Pillars homework missing"], "extra": ["Zoe vet visit"], "summary": "one miss"}
    monkeypatch.setattr(bc, "model_check", fake)
    bc.save(tmp_path, DAY, PREV, {"input": 735_000, "cached": 450_000})
    body, n = bc.report(tmp_path, DAY, PREV, {"input": 588_000, "cached": 408_000, "check_added": "homework"}, kind="trial")
    assert seen["same_day"] and n == 1
    assert "Digest trial vs this morning's original" in body and "Seven Pillars homework missing" in body
    assert "## Only in the digest version" in body and "Zoe vet visit" in body
    assert "| Input tokens | 735,000 | 588,000 |" in body and "Completeness check added:** homework" in body
    bc.save(tmp_path, DAY, PREV, {}, kind="trial")
    assert bc._previous(tmp_path / "briefs", DAY + datetime.timedelta(days=1))[1].name == "2026-11-26.md"
