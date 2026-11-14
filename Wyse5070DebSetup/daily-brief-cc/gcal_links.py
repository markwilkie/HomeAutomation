"""Turn the events email's `(gcal:TITLE|START|END|LOCATION)` link targets
into Google Calendar "add event" URLs.

The model writes the short form (events.md, "Calendar links") because a
hand-built, URL-encoded calendar link is easy to get wrong; this builds it
in code. Used by daily-brief-oai/brief.py and, as a filter on stdin, by
daily-brief-cc/run.sh's Claude fallback.

START/END: Pacific local time, YYYY-MM-DDTHH:MM, or YYYY-MM-DD for all day.
A missing END means two hours after START (or one day for all-day).
A target that doesn't parse is left as the event's search link instead of
breaking the email.
"""

from __future__ import annotations

import datetime
import re
import sys
import urllib.parse

TZ_NAME = "America/Los_Angeles"
_TARGET = re.compile(r"\(gcal:([^)]*)\)")


def _url(fields: list[str], event_page: str = "") -> str:
    title, start, end, location = (fields + ["", "", "", ""])[:4]
    title, start, end, location = (f.strip() for f in (title, start, end, location))
    if len(start) == 10:  # all day
        s = datetime.date.fromisoformat(start)
        e = datetime.date.fromisoformat(end[:10]) if end else s + datetime.timedelta(days=1)
        if e <= s:
            e = s + datetime.timedelta(days=1)
        dates = f"{s:%Y%m%d}/{e:%Y%m%d}"
    else:
        s = datetime.datetime.fromisoformat(start)
        e = datetime.datetime.fromisoformat(end) if len(end) > 10 else s + datetime.timedelta(hours=2)
        if e <= s:
            e = s + datetime.timedelta(hours=2)
        dates = f"{s:%Y%m%dT%H%M%S}/{e:%Y%m%dT%H%M%S}"
    params = {"action": "TEMPLATE", "text": title, "dates": dates, "ctz": TZ_NAME}
    if location:
        params["location"] = location
    if event_page:
        params["details"] = f"Event page: {event_page}"
    return "https://calendar.google.com/calendar/render?" + urllib.parse.urlencode(params)


_LINK = re.compile(r"\]\((https?://[^)\s]+)\)")


def expand(text: str) -> str:
    """Line by line, so each calendar entry can carry the event's own page: the
    last ordinary link on the same bullet (its source link) goes in the
    entry's description (user, 2026-11-14)."""
    def expand_line(line: str) -> str:
        def sub(m: re.Match) -> str:
            fields = m.group(1).split("|")
            sources = _LINK.findall(line[:m.start()])
            try:
                return f"({_url(fields, sources[-1] if sources else '')})"
            except ValueError:
                query = urllib.parse.quote_plus(" ".join(f for f in (fields[0], fields[-1]) if f))
                return f"(https://www.google.com/search?q={query})"
        return _TARGET.sub(sub, line)
    return "\n".join(expand_line(line) for line in text.split("\n"))


if __name__ == "__main__":
    sys.stdout.write(expand(sys.stdin.read()))
