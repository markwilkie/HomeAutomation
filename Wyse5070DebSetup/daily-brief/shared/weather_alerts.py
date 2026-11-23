"""Active National Weather Service alerts for home, for the top of the daily
brief (user, 2026-11-23: "call attention to any unusual inclement weather
alerts at top").

api.weather.gov is free, keyless, and the source the forecast sites copy, so
this is read in code instead of hoping a web search turns alerts up. Only
Moderate or worse (Wind Advisory, Winter Storm Warning, Flood Watch, ...):
Minor ones (Frost Advisory and the like) aren't unusual here.

  python3 weather_alerts.py   -> print today's lines ("(none)" when clear)
"""
from __future__ import annotations

import datetime as dt
import json
import sys
import urllib.request
from zoneinfo import ZoneInfo

POINT = "47.76,-122.28"  # Lake Forest Park, town-level
URL = f"https://api.weather.gov/alerts/active?point={POINT}"
UA = "home-daily-brief (personal, non-commercial)"  # api.weather.gov requires a User-Agent
SEVERE = {"Moderate", "Severe", "Extreme"}
TZ = ZoneInfo("America/Los_Angeles")


def _when(stamp: str | None) -> str:
    try:
        t = dt.datetime.fromisoformat(stamp).astimezone(TZ)
    except (TypeError, ValueError):
        return ""
    return t.strftime("%a %-I %p") if sys.platform != "win32" else t.strftime("%a %#I %p")


def alert_lines(data: dict) -> list[str]:
    out, seen = [], set()
    for f in data.get("features") or []:
        p = f.get("properties") or {}
        if p.get("status") != "Actual" or p.get("messageType") == "Cancel" or p.get("severity") not in SEVERE:
            continue
        event = p.get("event") or "Weather alert"
        until = _when(p.get("ends") or p.get("expires"))
        key = (event, until)
        if key in seen:  # NWS often issues the same alert for overlapping zones
            continue
        seen.add(key)
        headline = ((p.get("parameters") or {}).get("NWSheadline") or [""])[0]
        detail = headline or (p.get("description") or "").split("\n\n")[0].replace("\n", " ")
        line = event + (f" until {until}" if until else "")
        if detail:
            line += f": {detail[:240].strip()}"
        out.append(line)
    return out


def fetch() -> dict:
    req = urllib.request.Request(URL, headers={"User-Agent": UA, "Accept": "application/geo+json"})
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.loads(r.read().decode("utf-8"))


def brief_text() -> str:
    """Prefetch text for the brief's user message ("" when there are no alerts
    or the feed can't be reached: the regular weather search still runs)."""
    try:
        lines = alert_lines(fetch())
    except Exception as e:
        print(f"WARN: weather alerts fetch failed: {type(e).__name__}: {e}", file=sys.stderr)
        return ""
    if not lines:
        return ""
    return (" Weather alerts from the National Weather Service (put them in the ⚠️ Weather alert section "
            "at the top, one bullet each, keeping the event name and end time): " + " | ".join(lines))


if __name__ == "__main__":
    try:
        print("\n".join(alert_lines(fetch())) or "(none)")
    except Exception as e:
        sys.exit(f"fetch failed: {e}")
