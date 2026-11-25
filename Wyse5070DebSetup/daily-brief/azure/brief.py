#!/usr/bin/env python3
"""Daily brief on Azure OpenAI (Responses API), so it runs on the Azure
monthly credit instead of drawing down the Claude subscription's usage
limits the way daily-brief-cc does.

Same prompt.md as daily-brief-cc (copied in by setup-daily-brief-oai.sh).
The tools it names are provided here under the same names:
  - todo / trilium / monarch: this host's local MCP gateways, proxied as
    function tools (same allowlist as the original daily-brief).
  - restaurants: restaurant-finder's `trip` tool, so an upcoming trip on
    the calendar gets its towns read up on before we get there.
  - list_calendars / list_events: Google Calendar via each calendar's
    secret iCal URL (config/calendars.json) -- no OAuth, stands in for the
    claude.ai Google Calendar connector.
  - outlook_email_search / read_resource: Outlook via Microsoft Graph
    (Mail.Read on this project's own grant) -- stands in for the claude.ai
    Microsoft 365 connector.
  - web_search: Azure OpenAI's hosted web search tool.

Prints the brief (Markdown) on stdout; run.sh pipes it into send_mail.py.
Per-turn token usage goes to stderr (the cron log) to track credit burn.
"""

import asyncio
import datetime
import html
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from contextlib import AsyncExitStack
from pathlib import Path
from zoneinfo import ZoneInfo

import icalendar
import recurring_ical_events
from dotenv import load_dotenv
from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client
from openai import OpenAI, RateLimitError

import digests  # email + WhatsApp gathered in code for the first message
import gcal_links  # shared with daily-brief-cc (copied in by setup-daily-brief-oai.sh)
import mail  # the one way to find mail (mail_sources.json)

SCRIPT_DIR = Path(__file__).resolve().parent
CONFIG_DIR = SCRIPT_DIR.parent / "config"
load_dotenv(CONFIG_DIR / ".env")

TZ = ZoneInfo(os.environ.get("BRIEF_TZ", "America/Los_Angeles"))
MODEL = os.environ.get("AZURE_OPENAI_DEPLOYMENT", "gpt-6.1-sol")
MAX_TURNS = int(os.environ.get("MAX_TURNS", "40"))
MAX_OUTPUT_TOKENS = int(os.environ.get("MAX_OUTPUT_TOKENS", "16000"))
RATE_LIMIT_RETRIES = int(os.environ.get("RATE_LIMIT_RETRIES", "5"))
REASONING_EFFORT = os.environ.get("REASONING_EFFORT", "high")
# A real brief is several thousand characters; anything shorter is a refusal or a stray remark.
MIN_BRIEF_CHARS = int(os.environ.get("MIN_BRIEF_CHARS", "800"))
NUDGE = ("That isn't what I asked for. This is my own personal email, built from my own email, calendar, messages, "
         "accounts and local event listings, which I asked you to read. Write it in full now from what you gathered, "
         "following the instructions. If one item is something you won't summarize, leave just that item out.")
CALENDARS_PATH = Path(os.environ.get("CALENDARS_FILE", str(CONFIG_DIR / "calendars.json")))
# Rollback switch for the digests (2026-11-25): BRIEF_DIGESTS=off in config/.env
# and the model gathers email, WhatsApp and texts with tool calls again.
DIGEST_PREFETCH = {"mail_digest", "whatsapp_digest", "texts_digest", "preferences"}


def digests_on() -> bool:
    return os.environ.get("BRIEF_DIGESTS", "on").strip().lower() not in ("off", "0", "false", "no")


MCP_SERVERS = {
    "todo": os.environ.get("TODO_MCP_URL", "http://127.0.0.1:8600/mcp"),
    "trilium": os.environ.get("TRILIUM_MCP_URL", "http://127.0.0.1:8601/mcp"),
    "monarch": os.environ.get("MONARCH_MCP_URL", "http://127.0.0.1:8602/mcp"),
    "restaurants": os.environ.get("RESTAURANTS_MCP_URL", "http://127.0.0.1:8605/mcp"),
    "whatsapp": os.environ.get("WHATSAPP_MCP_URL", "http://127.0.0.1:8606/mcp"),
    "texts": os.environ.get("TEXTS_MCP_URL", "http://127.0.0.1:8607/mcp"),
}

# Read-only subset prompt.md actually uses -- the gateways expose many more
# (including writes), and every schema sent is billed input on every turn.
# To Do is deliberately absent: given get-tasks, the model re-swept all 19
# lists across several turns (~50 calls, 2026-10-08), and since every turn
# re-sends the whole conversation, that pushed a single request past the
# per-minute token limit. todo_due_tasks below does the sweep in code.
TOOL_ALLOWLIST = {
    "trilium": {"search_notes", "list_children_notes", "get_note", "resolve_note_id"},
    "monarch": {"get_accounts", "get_transactions", "search_transactions"},
    # The one write: queues a background read of an upcoming trip's towns
    # (idempotent -- a town read this week just answers "ready").
    # options (read-only) is for the weekly events email's dinner pairings.
    "restaurants": {"trip", "options"},
    # Read-only: the server also has send/react/group-admin tools, and
    # message text is untrusted input to the model.
    "whatsapp": {"get_status", "list_chats", "list_messages"},
    # SMS + RCS (read-only proxy). texts_digest below does the sweep; these
    # are for reading further into one thread the digest flagged.
    "texts": {"get_conversation", "search_messages"},
}

# Long tool outputs (a whole note, a month of transactions) are cut so one
# call can't blow the per-minute token limit on its own.
MAX_TOOL_OUTPUT_CHARS = int(os.environ.get("MAX_TOOL_OUTPUT_CHARS", "15000"))


def log(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


# --- MCP gateways ----------------------------------------------------------


async def connect_mcp_servers(stack: AsyncExitStack) -> dict[str, ClientSession]:
    """A gateway that's down is skipped, so the brief still goes out with
    what's available (the model reports that section as unavailable)."""
    sessions = {}
    for key, url in MCP_SERVERS.items():
        try:
            read, write = await stack.enter_async_context(streamable_http_client(url))
            session = await stack.enter_async_context(ClientSession(read, write))
            await session.initialize()
            sessions[key] = session
        except Exception as exc:
            log(f"WARN: could not reach {key} MCP gateway at {url}: {exc}")
    return sessions


async def mcp_tools(sessions: dict[str, ClientSession]):
    tools, dispatch = [], {}
    for key, session in sessions.items():
        for tool in (await session.list_tools()).tools:
            if tool.name not in TOOL_ALLOWLIST.get(key, set()):
                continue
            name = f"{key}__{tool.name}"
            tools.append(
                {
                    "type": "function",
                    "name": name,
                    "description": f"[{key}] {tool.description or ''}",
                    "parameters": tool.input_schema or {"type": "object", "properties": {}},
                    "strict": False,
                }
            )
            dispatch[name] = (session, tool.name)
    return tools, dispatch


async def call_mcp(session: ClientSession, name: str, args: dict) -> str:
    result = await session.call_tool(name, args)
    text = "\n".join(b.text if getattr(b, "text", None) is not None else str(b) for b in result.content)
    text = text or "(no output)"
    return f"Tool error: {text}" if result.is_error else text


TODO_LOOKAHEAD_DAYS = 14
_TODO_LIST_RE = re.compile(r"ID: (\S+)\nName: (.+)")
_TODO_TASK_RE = re.compile(r"^(○|✓) ID: \S+\n(.*?)(?=^---|\Z)", re.M | re.S)


async def todo_due_tasks(session: ClientSession) -> str:
    """Every open task across every list that's overdue, due within
    TODO_LOOKAHEAD_DAYS, or marked important, in one compact result."""
    today = datetime.datetime.now(TZ).date()
    horizon = today + datetime.timedelta(days=TODO_LOOKAHEAD_DAYS)
    lists = _TODO_LIST_RE.findall(await call_mcp(session, "get-task-lists", {}))
    if not lists:
        return "Tool error: could not read To Do lists"
    tasks, errors = [], []
    for list_id, list_name in lists:
        out = await call_mcp(session, "get-tasks", {"listId": list_id, "filter": "status ne 'completed'"})
        if out.startswith(("Tool error", "Failed")):
            errors.append(f"{list_name}: {out[:200]}")
            continue
        for mark, body in _TODO_TASK_RE.findall(out):
            fields = dict(re.findall(r"^(\w+): (.*)$", body, re.M))
            if mark != "○":
                continue
            due = datetime.datetime.strptime(fields["Due"].strip(), "%m/%d/%Y").date() if "Due" in fields else None
            # To Do's "Important" view is high importance (starred) across
            # all lists -- wanted in full, due date or not.
            important = fields.get("Importance", "").strip() == "high"
            if not important and (due is None or due > horizon):
                continue
            if due is None:
                status = "important (no due date)"
            else:
                status = "overdue" if due < today else "today" if due == today else "upcoming"
            tasks.append(
                {
                    "list": list_name.strip(),
                    "title": fields.get("Title", "").strip(),
                    "due": due.isoformat() if due else None,
                    "status": status,
                    "important": important,
                }
            )
    tasks.sort(key=lambda t: t["due"] or "9999")
    return json.dumps({"today": today.isoformat(), "lists_checked": len(lists), "tasks": tasks, "errors": errors})


TEXTS_OPEN_DAYS = 14  # how far back an unanswered text still counts as open (prompt.md: How long items stay)
TEXTS_BUSY = 3  # messages since yesterday morning that make a thread "busy" (this many or more)
TEXTS_BODY_CHARS = 280
TEXTS_BUDGET_CHARS = MAX_TOOL_OUTPUT_CHARS - 500
_TEXTS_CONV_RE = re.compile(r"^- (.+?)(?: \((\d+) unread\))? \(ID: (\S+), last: (\S+)\)$", re.M)
# get_conversation lines: "[2026-10-28T23:06:36Z] → Me: «text»" (→ = sent by
# me, ← = received), newest first; a body can span lines.
_TEXTS_MSG_RE = re.compile(r"^\[(\S+)\] (←|→) (.+?): «(.*?)»", re.M | re.S)


def _utc(stamp: str) -> datetime.datetime:
    return datetime.datetime.fromisoformat(stamp.replace("Z", "+00:00"))


async def texts_digest(session: ClientSession) -> str:
    """SMS + RCS for the brief in one result: every conversation active in
    the last TEXTS_OPEN_DAYS, oldest-first messages, who had the last word,
    and how busy it's been since yesterday morning. Busy threads get
    everything since then; the rest their last few messages. Deciding
    what's open, interesting or urgent is the model's job."""
    status = await call_mcp(session, "get_status", {})
    # The proxy leads with the problem and its exact fix steps when the
    # texts connection is broken; pass that through verbatim.
    problem = status.split("\n\n(Results below")[0] if "TEXTS ACCESS PROBLEM" in status else None

    now = datetime.datetime.now(datetime.timezone.utc)
    open_since = now - datetime.timedelta(days=TEXTS_OPEN_DAYS)
    yesterday_6am = datetime.datetime.combine(
        datetime.datetime.now(TZ).date() - datetime.timedelta(days=1), datetime.time(6), TZ
    )
    listing = await call_mcp(session, "list_conversations", {"limit": 100})

    conversations = []
    for name, unread, conv_id, last in _TEXTS_CONV_RE.findall(listing):
        if _utc(last) < open_since:
            break  # sorted newest first
        out = await call_mcp(session, "get_conversation", {"conversation_id": conv_id, "limit": 60})
        messages = sorted(
            (_utc(ts), arrow == "→", sender.strip(), " ".join(body.split()))
            for ts, arrow, sender, body in _TEXTS_MSG_RE.findall(out)
        )
        if not messages:
            continue
        recent = [m for m in messages if m[0] >= yesterday_6am]
        # Busy: everything since yesterday. Quiet with my last word: kept
        # (it may be an ask nobody answered -- Waiting on others; 2026-11-14
        # these were dropped and "Dinner Friday the 20th?" was missed), with
        # a little context. Otherwise the last few.
        if len(recent) >= TEXTS_BUSY:
            shown = recent
        elif messages[-1][1] and not recent:
            shown = messages[-3:]
        else:
            shown = messages[-6:]
        conversations.append(
            {
                "name": name,
                "id": conv_id,
                "group": name.endswith("[group]"),
                "unread": int(unread or 0),
                "last_at": messages[-1][0].astimezone(TZ).strftime("%a %-m/%-d %-I:%M %p"),
                "last_from_me": messages[-1][1],
                "messages_since_yesterday": len(recent),
                "messages": [
                    {
                        "at": at.astimezone(TZ).strftime("%a %-I:%M %p"),
                        "from": "me" if mine else sender,
                        "text": body[:TEXTS_BODY_CHARS],
                    }
                    for at, mine, sender, body in shown
                ],
            }
        )

    result = {"problem": problem, "open_window_days": TEXTS_OPEN_DAYS, "conversations": conversations}

    def size() -> int:
        return len(json.dumps(result, ensure_ascii=False, separators=(",", ":")))

    # Fit the tool-output cap by trimming, quiet threads before busy ones,
    # rather than having the tail (oldest activity) cut off mid-JSON.
    for keep, chars in ((4, TEXTS_BODY_CHARS), (2, 160), (1, 100)):
        if size() <= TEXTS_BUDGET_CHARS:
            break
        for conv in conversations:
            limit = keep * 5 if conv["messages_since_yesterday"] >= TEXTS_BUSY else keep
            conv["messages"] = [{**m, "text": m["text"][:chars]} for m in conv["messages"][-limit:]]
    return json.dumps(result, ensure_ascii=False, separators=(",", ":"))


async def sober_line(session: ClientSession) -> str | None:
    """Days/weeks since the latest date in the top-level Trilium note
    "Sober" (e.g. "9/28/2026 - last relapse"). Counted here, not by the
    model, so the number is never off by a day."""
    found = json.loads(
        await call_mcp(
            session,
            "search_notes",
            {
                "searchCriteria": [
                    {"type": "noteProperty", "property": "title", "op": "=", "value": "Sober", "logic": "AND"},
                    {"type": "noteProperty", "property": "parents.noteId", "op": "=", "value": "root", "logic": "AND"},
                ]
            },
        )
    )
    if not found:
        return None
    content = json.loads(await call_mcp(session, "get_note", {"noteId": found[0]["noteId"]})).get("content", "")
    dates = [
        datetime.date(int(y), int(m), int(d)) for m, d, y in re.findall(r"\b(\d{1,2})/(\d{1,2})/(\d{4})\b", content)
    ]
    if not dates:
        return None
    relapse = max(dates)
    days = (datetime.datetime.now(TZ).date() - relapse).days
    weeks, rem = divmod(days, 7)
    return (
        f"{days} days since last relapse ({relapse.month}/{relapse.day}/{relapse.year}) -- "
        f"{weeks} week{'s' if weeks != 1 else ''}, {rem} day{'s' if rem != 1 else ''}"
    )


# --- Google Calendar via secret iCal URLs -----------------------------------


def load_calendars() -> dict[str, str]:
    if not CALENDARS_PATH.is_file():
        return {}
    return json.loads(CALENDARS_PATH.read_text())


def list_calendars(_args: dict) -> str:
    names = list(load_calendars())
    return json.dumps({"calendars": names} if names else {"error": f"no calendars configured in {CALENDARS_PATH}"})


def _fmt_when(value) -> tuple[str, bool]:
    if isinstance(value, datetime.datetime):
        if value.tzinfo is None:
            value = value.replace(tzinfo=TZ)
        return value.astimezone(TZ).strftime("%Y-%m-%d %H:%M"), False
    return value.isoformat(), True


def list_events(args: dict) -> str:
    calendars = load_calendars()
    wanted = args.get("calendars") or list(calendars)
    start = datetime.date.fromisoformat(args["start_date"])
    end = datetime.date.fromisoformat(args.get("end_date") or args["start_date"])
    # Window is whole local days, end inclusive.
    window_start = datetime.datetime.combine(start, datetime.time.min, TZ)
    window_end = datetime.datetime.combine(end + datetime.timedelta(days=1), datetime.time.min, TZ)

    events, errors = [], []
    for name in wanted:
        url = calendars.get(name)
        if not url:
            errors.append(f"{name}: not a configured calendar")
            continue
        try:
            with urllib.request.urlopen(url, timeout=30) as resp:
                cal = icalendar.Calendar.from_ical(resp.read())
            for ev in recurring_ical_events.of(cal).between(window_start, window_end):
                begin, all_day = _fmt_when(ev.get("DTSTART").dt)
                finish = _fmt_when(ev.get("DTEND").dt)[0] if ev.get("DTEND") else None
                events.append(
                    {
                        "calendar": name,
                        "summary": str(ev.get("SUMMARY", "")),
                        "start": begin,
                        "end": finish,
                        "all_day": all_day,
                        "location": str(ev.get("LOCATION", "")) or None,
                    }
                )
        except Exception as exc:
            errors.append(f"{name}: {exc}")
    events.sort(key=lambda e: e["start"])
    return json.dumps({"events": events, "errors": errors})


# --- Outlook via Microsoft Graph --------------------------------------------


# Mail access (token, Graph reads, folder-by-folder finding) lives in mail.py,
# shared with replies.py and the email-lookup skill.
graph_get = mail.graph_get


# --- Mail backstop -----------------------------------------------------------
# USPS Informed Delivery scans, checked once a day as a backstop so important
# paper mail doesn't sit in the mailbox (user, 2026-11-14). The digest arrives
# ~7:15am, after the brief, so each morning looks at the latest digest from
# the last ~30 hours: yesterday's mail. A separate small call with low-detail
# images, not a tool in the brief's loop (images there would be re-sent every
# turn). Most days it finds nothing and the brief says nothing.

MAIL_BACKSTOP_PROMPT = """These are USPS scans of envelopes delivered to a household
(Mark, Jennie, and their grown kids, e.g. Hannah). List only pieces someone should
be sure to pick up and open soon:
- personal mail: handwritten address, a card or letter from a person;
- government or courts: IRS, Social Security, DMV, jury duty, tax forms, ballots;
- real account mail: a statement, notice, check, new card or legal letter from a
  bank, insurer, employer, doctor or utility (not an offer or ad from them).
Everything else is junk and must be left out: ads, offers, catalogs, "pre-approved"
or discount mailers, insurance or credit solicitations, donation requests.
Most days nothing qualifies. Answer with JSON only:
{"important": [{"to": "<addressee first name>", "what": "<few words, e.g. handwritten letter, postmarked Seattle>"}]}"""


def mail_backstop() -> str:
    """One line for the brief's user message, or "" when nothing matters."""
    digest = mail.latest("usps", days=1.25)  # ~30 hours: yesterday's delivery
    if not digest:
        return ""
    scans = mail.usps_scans(digest["id"])
    if not scans:
        return ""
    images = [{"type": "input_image", "detail": "low",
               "image_url": f"data:{a['contentType']};base64,{a['contentBytes']}"} for a in scans]
    client = OpenAI(base_url=os.environ["AZURE_OPENAI_BASE_URL"], api_key=os.environ["AZURE_OPENAI_API_KEY"])
    r = client.responses.create(
        model=MODEL, reasoning={"effort": "low"}, max_output_tokens=2000,
        input=[{"role": "user", "content": [{"type": "input_text", "text": MAIL_BACKSTOP_PROMPT}, *images]}])
    text = (r.output_text or "").strip().removeprefix("```json").removeprefix("```").removesuffix("```").strip()
    important = json.loads(text).get("important") or []
    day = digest["subject"].split("for ")[-1].split(" is")[0]  # "Sat, 11/14"
    log(f"  mail backstop: {len(scans)} scans from {day}, {len(important)} important, usage={r.usage and (r.usage.input_tokens, r.usage.output_tokens)}")
    if not important:
        return ""
    items = "; ".join(f"{i.get('what', 'mail')} for {i.get('to', '?')}" for i in important)
    return f" Mail to pick up (USPS scans, delivered {day}): {items}."


def _message_row(m: dict) -> dict:
    return {
        "id": m["id"],
        "subject": m.get("subject"),
        "from": (m.get("from") or {}).get("emailAddress", {}).get("address"),
        "received": m.get("receivedDateTime"),
        "isRead": m.get("isRead"),
        "preview": m.get("bodyPreview"),
        **({"folder": m["folder"]} if "folder" in m else {}),
    }


def outlook_email_search(args: dict) -> str:
    folder = (args.get("folderName") or "Inbox").strip().lower()
    sender = (args.get("sender") or "").strip()
    if folder == "all" or sender:
        # mail.find: every folder in mail_sources.json (or just this one), last
        # `days`, by sender and/or subject text -- finds mail already deleted or archived.
        hits = mail.find(sender or None, args.get("query") or None, float(args.get("days") or 8),
                         folders=None if folder == "all" else [folder.replace(" ", "")])
        return json.dumps({"folder": folder, "messages": [_message_row(m) for m in hits[:50]]})
    # Graph's well-known folder names have no spaces ("Sent Items" -> sentitems).
    folder = {"sent items": "sentitems", "sent": "sentitems", "junk email": "junkemail"}.get(folder, folder)
    top = min(int(args.get("top") or 25), 50)
    params = {"$top": top, "$select": "id,subject,from,toRecipients,receivedDateTime,isRead,bodyPreview"}
    if args.get("query"):
        # $search can't be combined with $orderby; results come back by relevance.
        params["$search"] = f'"{args["query"]}"'
    else:
        params["$orderby"] = "receivedDateTime desc"
    try:
        data = graph_get(f"/me/mailFolders/{urllib.parse.quote(folder)}/messages", params)
    except RuntimeError as exc:
        return f"Tool error: {exc}"
    messages = [
        {
            "id": m["id"],
            "subject": m.get("subject"),
            "from": (m.get("from") or {}).get("emailAddress", {}).get("address"),
            "to": [r.get("emailAddress", {}).get("address") for r in m.get("toRecipients") or []],
            "received": m.get("receivedDateTime"),
            "isRead": m.get("isRead"),
            # Preview only (owner's call 2026-10-09): full text of unread
            # mail added tokens for little gain -- the 10/08 emails it was
            # meant to catch had actually been deleted, not hidden.
            "preview": m.get("bodyPreview"),
        }
        for m in data.get("value", [])
    ]
    return json.dumps({"folder": folder, "messages": messages})


def read_resource(args: dict) -> str:
    message_id = str(args["id"]).strip()
    message_id = digests.MAIL_REFS.get(message_id, message_id)  # "m12" from the email digest
    try:
        m = graph_get(f"/me/messages/{urllib.parse.quote(message_id)}", {"$select": "subject,from,receivedDateTime,body"})
    except RuntimeError as exc:
        return f"Tool error: {exc}"
    body = html.unescape(re.sub(r"\n{3,}", "\n\n", m.get("body", {}).get("content", "")))
    return json.dumps(
        {
            "subject": m.get("subject"),
            "from": (m.get("from") or {}).get("emailAddress", {}).get("address"),
            "received": m.get("receivedDateTime"),
            "body": body,
        }
    )


LOCAL_TOOLS = {
    "list_calendars": (
        list_calendars,
        "List the Google calendars available to list_events.",
        {"type": "object", "properties": {}},
    ),
    "list_events": (
        list_events,
        "List events (recurring ones expanded) from Google calendars over whole local days (Pacific time), end date inclusive.",
        {
            "type": "object",
            "properties": {
                "start_date": {"type": "string", "description": "YYYY-MM-DD"},
                "end_date": {"type": "string", "description": "YYYY-MM-DD, inclusive; defaults to start_date"},
                "calendars": {
                    "type": "array",
                    "items": {"type": "string"},
                    "description": "Calendar names from list_calendars; defaults to all of them",
                },
            },
            "required": ["start_date"],
        },
    ),
    "outlook_email_search": (
        outlook_email_search,
        "List Outlook messages in a folder, newest first (or by relevance when query is given). Each result has isRead. "
        "To find a particular message that may already be deleted or archived, use folderName \"all\" (Inbox, "
        "Deleted Items, Archive and Clutter in one call) and/or sender; then query matches subject text.",
        {
            "type": "object",
            "properties": {
                "folderName": {"type": "string", "description": "e.g. Inbox, Clutter, Sent Items, JunkEmail, or all"},
                "query": {"type": "string", "description": "Optional search text, e.g. a subject"},
                "sender": {"type": "string", "description": "Optional sender address or domain, e.g. no-reply@rs.email.nextdoor.com"},
                "days": {"type": "number", "description": "With all or sender: how far back, default 8"},
                "top": {"type": "integer", "description": "Max messages, default 25, max 50"},
            },
            "required": ["folderName"],
        },
    ),
    "read_resource": (
        read_resource,
        "Read the full text of one Outlook message by its id from outlook_email_search, or its `n` (e.g. m12) from the email digest.",
        {"type": "object", "properties": {"id": {"type": "string"}}, "required": ["id"]},
    ),
}


# --- Model loop -------------------------------------------------------------


def web_search_tool() -> dict:
    return {
        "type": "web_search",
        "user_location": {
            "type": "approximate",
            "city": os.environ.get("WEB_SEARCH_CITY", "Seattle"),
            "region": os.environ.get("WEB_SEARCH_REGION", "Washington"),
            "country": os.environ.get("WEB_SEARCH_COUNTRY", "US"),
        },
    }


def create_with_rate_limit_retry(client: OpenAI, **kwargs):
    """The SDK's own retries back off for seconds; Azure's token limit is a
    per-minute window, so wait out whole windows before giving up."""
    for attempt in range(RATE_LIMIT_RETRIES + 1):
        try:
            return client.responses.create(**kwargs)
        except RateLimitError:
            if attempt == RATE_LIMIT_RETRIES:
                raise
            log(f"  429 rate limited; waiting 65s (retry {attempt + 1}/{RATE_LIMIT_RETRIES})")
            time.sleep(65)


async def trilium_note(session: ClientSession, title: str) -> str:
    """A Trilium note's content by exact title ("" if there's none)."""
    found = json.loads(await call_mcp(session, "search_notes", {"searchCriteria": [{"type": "noteProperty", "property": "title", "op": "=", "value": title, "logic": "AND"}], "limit": 10}))
    match = next((n for n in found if n.get("title") == title), None)
    if not match:
        return ""
    return json.loads(await call_mcp(session, "get_note", {"noteId": match["noteId"]})).get("content", "")


async def events_taste(session: ClientSession) -> str:
    """Both taste notes for the events email, read in code. The Music Taste
    Profile is ~50 KB, cut at MAX_TOOL_OUTPUT_CHARS per read, so the first
    events run (2026-11-14) spent four turns paging through it; artist names
    alone (~5 KB) are enough to match concerts against."""
    async def note(title: str) -> str:
        found = json.loads(await call_mcp(session, "search_notes", {"searchCriteria": [{"type": "noteProperty", "property": "title", "op": "=", "value": title, "logic": "AND"}], "limit": 10}))
        match = next((n for n in found if n.get("title") == title), None)
        if not match:
            return ""
        return json.loads(await call_mcp(session, "get_note", {"noteId": match["noteId"]})).get("content", "")

    events = await note("Events Taste Profile")
    music = await note("Music Taste Profile")
    artists = sorted({line.split(" (")[0].split(" [")[0].strip() for line in music.splitlines()
                      if " | genres:" in line})
    return ("\n\nEvents Taste Profile note (already read for you; don't fetch it):\n\n" + events
            + "\n\nArtists in the Music Taste Profile (names only; already read for you, don't fetch the note):\n"
            + " · ".join(artists))


async def _prefetch(name: str, sessions: dict) -> str:
    """Text added to the first message, gathered in code before the model runs
    (jobs.json "prefetch"): cheaper and steadier than a tool call."""
    if name == "mail_backstop":
        return mail_backstop()
    if name == "preferences" and "trilium" in sessions:
        text = await trilium_note(sessions["trilium"], "Daily Brief Preferences")
        return f" Daily Brief Preferences note (already read for you; don't fetch it):\n\n{text}\n\n" if text else ""
    if name == "mail_digest":
        return " Email digest (Outlook, gathered in code for you; JSON): " + digests.mail_digest(TZ)
    if name == "whatsapp_digest" and "whatsapp" in sessions:
        return " WhatsApp digest (gathered in code for you; JSON): " + await digests.whatsapp_digest(sessions["whatsapp"], call_mcp, TZ)
    if name == "texts_digest" and "texts" in sessions:
        return " Texts digest (the texts_digest result, already gathered for you; JSON): " + await texts_digest(sessions["texts"])
    if name == "sober" and "trilium" in sessions:
        sober = await sober_line(sessions["trilium"])
        return f" Sober count: {sober}." if sober else ""
    if name == "taste_notes" and "trilium" in sessions:
        return await events_taste(sessions["trilium"])
    if name == "photos":
        import photo_check  # shared/; reads the photo-sync status files on this host

        lines = photo_check.brief_lines()
        return (" Photo problems (put each in Needs you exactly as written): " + " | ".join(lines) + ".") if lines else ""
    if name == "substack":
        import substack_check  # shared/; saves a daily snapshot in state/substack.json

        return substack_check.brief_text(SCRIPT_DIR.parent / "state")
    if name == "weather_alerts":
        import weather_alerts  # shared/; National Weather Service alerts for home

        return weather_alerts.brief_text()
    return ""


DIGEST_MARKERS = (" Email digest (", " WhatsApp digest (", " Texts digest (")
COMPLETENESS_CHECK = """Before this goes out, check it for completeness. Go through the Email digest
(inbox, clutter and my_threads), the WhatsApp digest and the Texts digest one message or thread
at a time. For each one that asks me for something, gives me a task (homework, a form, a
document, something to bring), has a date or deadline, is an RSVP, a bill or a question to me,
or is my own ask nobody answered: confirm it's in the brief, or that the rules drop it (settled
in any channel, its date passed, older than its window, automated or marketing). Add anything
missing to the section where it belongs, following every rule above. Don't drop or shorten
anything already in the brief unless it breaks a rule.

Reply in exactly this form, nothing else:
ADDED: <each item you added, separated by "; ", or "nothing">
---BRIEF---
<the complete final brief>"""


def after_check(reply: str, draft: str) -> tuple[str, str]:
    """(final brief, what the check added). Keeps the draft if the reply isn't a usable brief."""
    head, sep, body = reply.partition("---BRIEF---")
    body = body.strip()
    if not sep or len(body) < MIN_BRIEF_CHARS:
        log("  WARN: completeness check reply wasn't a brief; sending the draft")
        return draft, ""
    added = head.strip().removeprefix("ADDED:").strip()
    return body, ("" if added.lower().rstrip(".") == "nothing" else added)


# Token totals of the last generate_brief call, for the runner's cost line.
LAST_USAGE: dict = {}


async def generate_brief(prompt_text: str, request: str | None = None, prefetch: tuple[str, ...] = ()) -> str:
    client = OpenAI(
        base_url=os.environ["AZURE_OPENAI_BASE_URL"],
        api_key=os.environ["AZURE_OPENAI_API_KEY"],
        max_retries=3,
        timeout=600,
    )
    today = datetime.datetime.now(TZ).date()

    async with AsyncExitStack() as stack:
        sessions = await connect_mcp_servers(stack)
        tools, dispatch = await mcp_tools(sessions)
        for name, (_, desc, schema) in LOCAL_TOOLS.items():
            tools.append({"type": "function", "name": name, "description": desc, "parameters": schema, "strict": False})
        if "todo" in sessions:
            tools.append(
                {
                    "type": "function",
                    "name": "todo_due_tasks",
                    "description": f"Microsoft To Do: every open task across ALL lists that is overdue, due today, due within {TODO_LOOKAHEAD_DAYS} days, or marked important (starred). One call covers everything.",
                    "parameters": {"type": "object", "properties": {}},
                    "strict": False,
                }
            )
        wanted = [n for n in prefetch if digests_on() or n not in DIGEST_PREFETCH]
        if "texts" in sessions and "texts_digest" not in wanted:
            tools.append(
                {
                    "type": "function",
                    "name": "texts_digest",
                    "description": f"My SMS + RCS texts: every conversation active in the last {TEXTS_OPEN_DAYS} days with its latest messages, who sent the last one, and how busy it's been since yesterday morning. One call covers everything; includes the fix steps if the texts connection is broken.",
                    "parameters": {"type": "object", "properties": {}},
                    "strict": False,
                }
            )
        tools.append(web_search_tool())

        next_input = request or f"Generate today's brief. Today is {today.isoformat()}, a {today.strftime('%A')}."
        for name in wanted:
            try:
                extra = await _prefetch(name, sessions)
                next_input += extra
                if name in DIGEST_PREFETCH:
                    log(f"  {name}: {len(extra):,} chars")
            except Exception as exc:  # a missing extra never stops the email
                log(f"WARN: prefetch {name} failed: {type(exc).__name__}: {exc}")
                if name == "texts_digest" and "texts" in sessions:  # let the model call it after all
                    tools.append({"type": "function", "name": "texts_digest", "strict": False,
                                  "description": "My SMS + RCS texts: every recent conversation in one call.",
                                  "parameters": {"type": "object", "properties": {}}})
        previous_id = None
        nudged = False
        digest_used = any(m in next_input for m in DIGEST_MARKERS)
        draft = None
        totals = {"input": 0, "cached": 0, "output": 0}
        for turn in range(1, MAX_TURNS + 1):
            totals["turns"] = turn
            response = create_with_rate_limit_retry(
                client,
                model=MODEL,
                instructions=prompt_text,
                input=next_input,
                tools=tools,
                previous_response_id=previous_id,
                reasoning={"effort": REASONING_EFFORT},
                # Azure charges each request's max output against the
                # per-minute token limit up front; the model's default
                # max is large enough that a few turns hit 429.
                max_output_tokens=MAX_OUTPUT_TOKENS,
            )
            previous_id = response.id
            usage = response.usage
            if usage:
                totals["input"] += usage.input_tokens
                totals["output"] += usage.output_tokens
                totals["cached"] += getattr(usage.input_tokens_details, "cached_tokens", 0) or 0
            calls = [item for item in response.output if item.type == "function_call"]
            searches = sum(1 for item in response.output if item.type == "web_search_call")
            totals["searches"] = totals.get("searches", 0) + searches
            log(f"  turn {turn}: {len(calls)} tool calls {[c.name for c in calls]}, {searches} web searches, usage={usage and (usage.input_tokens, usage.output_tokens)}")

            if not calls:
                text = response.output_text or ""
                if len(text.strip()) < MIN_BRIEF_CHARS:
                    # 2026-11-14: after gathering everything the model answered "I'm sorry, but I cannot
                    # assist with that request." (49 chars) and that was emailed as the brief. Ask once more
                    # in the same conversation (cheap: the gathered data is cached); if it still won't
                    # write one, fail so run.sh falls back to Claude Code.
                    if nudged:
                        raise RuntimeError(f"model gave no brief twice; last reply: {text.strip()[:200]!r}")
                    nudged = True
                    log(f"  WARN: reply too short to be a brief ({len(text.strip())} chars): {text.strip()[:200]!r}; asking again")
                    next_input = NUDGE
                    continue
                if digest_used and draft is None:
                    # Completeness check (user, 2026-11-25: completeness is a must). The first digest
                    # test left out a homework email that was right there in the digest.
                    draft = text
                    log("  completeness check")
                    next_input = COMPLETENESS_CHECK
                    continue
                added = ""
                if draft is not None:
                    text, added = after_check(text, draft)
                    log(f"  completeness check added: {added or 'nothing'}")
                log(f"  total tokens: {totals}")
                LAST_USAGE.clear()
                LAST_USAGE.update(totals)
                if draft is not None:
                    LAST_USAGE["check_added"] = added or "nothing"
                return text

            outputs = []
            for call in calls:
                try:
                    args = json.loads(call.arguments or "{}")
                    if call.name == "todo_due_tasks" and "todo" in sessions:
                        output = await todo_due_tasks(sessions["todo"])
                    elif call.name == "texts_digest" and "texts" in sessions:
                        output = await texts_digest(sessions["texts"])
                    elif call.name in dispatch:
                        session, original = dispatch[call.name]
                        output = await call_mcp(session, original, args)
                    elif call.name in LOCAL_TOOLS:
                        output = LOCAL_TOOLS[call.name][0](args)
                    else:
                        output = f"Tool error: unknown tool {call.name}"
                except Exception as exc:
                    output = f"Tool error: {type(exc).__name__}: {exc}"
                if len(output) > MAX_TOOL_OUTPUT_CHARS:
                    output = output[:MAX_TOOL_OUTPUT_CHARS] + "\n...(truncated)"
                outputs.append({"type": "function_call_output", "call_id": call.call_id, "output": output})
            next_input = outputs

        raise RuntimeError(f"no final answer after {MAX_TURNS} turns")


def root_cause(exc: BaseException) -> BaseException:
    # The MCP client's task groups wrap the real error several levels deep.
    while isinstance(exc, BaseExceptionGroup) and len(exc.exceptions) == 1:
        exc = exc.exceptions[0]
    return exc


def events_request(mode: str, previous: Path | None) -> str:
    """The user message for the weekly events email (events.md)."""
    today = datetime.datetime.now(TZ).date()
    msg = f"This is the {mode.upper()} run of the events email. Today is {today.isoformat()}, a {today.strftime('%A')}."
    if previous and previous.exists():
        label = "Thursday's email" if mode == "refresh" else "Last Saturday's email"
        msg += f"\n\n{label} ({previous.stem.removeprefix('events-')}):\n\n{previous.read_text()}"
    return msg


if __name__ == "__main__":
    sys.exit("brief.py is a library now: run run.py (or run.sh / run_events.sh) instead.")
