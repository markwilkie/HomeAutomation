import weather_alerts as wa


def alert(event, severity="Moderate", ends="2026-11-29T04:00:00-08:00", headline="gusts to 50 mph", **kw):
    p = {"status": "Actual", "messageType": "Alert", "severity": severity, "event": event, "ends": ends,
         "parameters": {"NWSheadline": [headline]} if headline else {}, "description": "Detail para.\n\nMore."}
    p.update(kw)
    return {"properties": p}


def test_moderate_alert_listed_with_end_time():
    lines = wa.alert_lines({"features": [alert("Wind Advisory")]})
    assert len(lines) == 1 and lines[0].startswith("Wind Advisory until Sun 4 AM: gusts to 50 mph")


def test_minor_test_and_cancelled_dropped():
    data = {"features": [alert("Frost Advisory", severity="Minor"), alert("Wind Advisory", status="Test"),
                         alert("Flood Watch", messageType="Cancel")]}
    assert wa.alert_lines(data) == []


def test_duplicate_zones_collapsed_and_description_fallback():
    data = {"features": [alert("Winter Storm Warning", severity="Severe", headline=""),
                         alert("Winter Storm Warning", severity="Severe", headline="")]}
    lines = wa.alert_lines(data)
    assert lines == ["Winter Storm Warning until Sun 4 AM: Detail para."]


def test_no_alerts_or_feed_down_is_silent(monkeypatch):
    monkeypatch.setattr(wa, "fetch", lambda: {"features": []})
    assert wa.brief_text() == ""

    def down():
        raise OSError("timeout")
    monkeypatch.setattr(wa, "fetch", down)
    assert wa.brief_text() == ""


def test_brief_text_points_at_top_section(monkeypatch):
    monkeypatch.setattr(wa, "fetch", lambda: {"features": [alert("Wind Advisory")]})
    assert "⚠️ Weather alert" in wa.brief_text()
