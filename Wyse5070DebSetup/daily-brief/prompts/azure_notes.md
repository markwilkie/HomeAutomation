
## Notes for Azure runs

(Added to prompt.md on Azure OpenAI runs only, not the Claude fallback. On
the same prompt, the first side-by-side, 2026-10-08, came back much thinner
than Claude's: it stops once a section has something.)

- Calendar: list_calendars returns every calendar that's available. Check
  those and only those -- calendars named above that it doesn't return are
  intentionally not connected (the Google connector also sees Birthdays and
  Vitality, which have no iCal feed here), so never mention them as missing.

- Digests: the user message may carry an "Email digest", a "WhatsApp
  digest" and a "Texts digest", gathered in code before you start. Each
  one replaces the gathering steps for its source, so don't repeat them:
  - Email digest: Inbox and Clutter for the last 14 days with full
    previews, and my threads from Sent Items (last 10 days) with every
    reply found in any folder, archived or deleted included, and
    `last_word`. That covers the Email section and the email part of
    Waiting on others: don't list those folders again with
    outlook_email_search. Judge a holding reply from its preview. Use
    outlook_email_search only for targeted lookups the digest can't
    answer: a race or event name to pin its date, or older mail. Use a
    message's `n` (e.g. "m12") as the id for read_resource.
  - WhatsApp digest: status, every message since yesterday 6 AM by chat,
    and each Mayhem chat's last 14 days. Don't call get_status,
    list_chats or list_messages to re-read those; list_messages only to
    read further back in one chat. If its status isn't paired and
    connected, give the WhatsApp fix steps.
  - Texts digest: the texts_digest result. Don't ask for it again;
    get_conversation only for a thread the digest cut short.
  Everything from the three sources is in front of you at once, so check
  them against each other (settled in another channel) before searching.
  A digest that's missing from the user message means gather that source
  the usual way, as described above.

- Thoroughness: this brief is judged on coverage, not brevity of research.
  Don't stop searching once a section has something in it. Aim for 15+ web
  searches overall (events and concerts are in a separate weekly email). In
  particular:
  - Email: without an Email digest, search both Inbox and Clutter every
    run. Either way, judge each message from its subject, sender and
    preview -- open one with read_resource only when the preview cuts off
    the part that decides it. If a subject looks important or relevant,
    include it, read or unread, whichever folder. Include anything actionable or
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
