
## Notes for Azure runs

(Added to prompt.md on Azure OpenAI runs only, not the Claude fallback. On
the same prompt, the first side-by-side, 2026-10-08, came back much thinner
than Claude's: it stops once a section has something.)

- Calendar: list_calendars returns every calendar that's available. Check
  those and only those -- calendars named above that it doesn't return are
  intentionally not connected (the Google connector also sees Birthdays and
  Vitality, which have no iCal feed here), so never mention them as missing.

- Thoroughness: this brief is judged on coverage, not brevity of research.
  Don't stop searching once a section has something in it. Aim for 15+ web
  searches overall (events and concerts are in a separate weekly email). In
  particular:
  - Email: search both Inbox and Clutter every run. Judge each message
    from its subject, sender and preview -- don't open messages with
    read_resource. If a subject looks important or relevant, include it,
    read or unread, whichever folder. Include anything actionable or
    time-sensitive -- alerts (water, security, data breach), bills and due
    dates, interview or appointment changes. Only drop true
    newsletters/promotions.
  - News: at least 5 distinct national/world stories, from 2+ searches.
  - Market snapshot: use the most recent official closing prices (the
    previous trading day's close if the market hasn't closed today), not
    intraday quotes, and cite a source that actually shows each figure.
  - To Do: the todo get-task-lists/get-tasks tools named above aren't
    available here. Call todo_due_tasks once instead -- it already sweeps
    every list. The To Do section is its overdue/today tasks plus the
    Important list (tasks with important=true).
  - Cross-reference what you gathered -- e.g. balances vs bills coming
    due, an alert vs an open to-do, a cancelled plan still on the
    calendar, account connections that need attention in Monarch -- and
    put each finding in the one section where it belongs (there is no
    "anything else" section).
