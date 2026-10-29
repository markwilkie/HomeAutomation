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
import msal
import recurring_ical_events
from dotenv import load_dotenv
from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client
from openai import OpenAI, RateLimitError

SCRIPT_DIR = Path(__file__).resolve().parent
CONFIG_DIR = SCRIPT_DIR.parent / "config"
load_dotenv(CONFIG_DIR / ".env")

TZ = ZoneInfo(os.environ.get("BRIEF_TZ", "America/Los_Angeles"))
MODEL = os.environ.get("AZURE_OPENAI_DEPLOYMENT", "gpt-6.1-sol")
MAX_TURNS = int(os.environ.get("MAX_TURNS", "40"))
MAX_OUTPUT_TOKENS = int(os.environ.get("MAX_OUTPUT_TOKENS", "16000"))
RATE_LIMIT_RETRIES = int(os.environ.get("RATE_LIMIT_RETRIES", "5"))
REASONING_EFFORT = os.environ.get("REASONING_EFFORT", "high")
CALENDARS_PATH = Path(os.environ.get("CALENDARS_FILE", str(CONFIG_DIR / "calendars.json")))

GRAPH_CLIENT_ID = os.environ.get("MS_CLIENT_ID", "d0f1f47d-77ee-494d-a63b-ee3afeb2fad1")
GRAPH_TENANT_ID = os.environ.get("MS_TENANT_ID", "859244b7-6976-450f-92d4-352c7abd6601")
GRAPH_TOKEN_CACHE_PATH = Path(os.environ.get("MS_TOKEN_CACHE", str(CONFIG_DIR / "graph_token_cache.json")))

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
    "restaurants": {"trip"},
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


TEXTS_OPEN_DAYS = 7  # how far back an unanswered text still counts as open
TEXTS_BUSY = 12  # messages since yesterday morning that make a thread "busy"
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
        if messages[-1][1] and not recent:
            continue  # I had the last word and it's gone quiet: nothing to report
        shown = recent if len(recent) > TEXTS_BUSY else messages[-6:]
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
            limit = keep * 5 if conv["messages_since_yesterday"] > TEXTS_BUSY else keep
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


def graph_token() -> str:
    cache = msal.SerializableTokenCache()
    cache.deserialize(GRAPH_TOKEN_CACHE_PATH.read_text())
    app = msal.PublicClientApplication(
        GRAPH_CLIENT_ID, authority=f"https://login.microsoftonline.com/{GRAPH_TENANT_ID}", token_cache=cache
    )
    accounts = app.get_accounts()
    result = app.acquire_token_silent(["Mail.Read"], account=accounts[0]) if accounts else None
    if cache.has_state_changed:
        GRAPH_TOKEN_CACHE_PATH.write_text(cache.serialize())
    if not result or "access_token" not in result:
        raise RuntimeError("Graph Mail.Read token unavailable -- re-run graph_login.py")
    return result["access_token"]


def graph_get(path: str, params: dict | None = None) -> dict:
    url = "https://graph.microsoft.com/v1.0" + path
    if params:
        url += "?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(
        url,
        headers={"Authorization": f"Bearer {graph_token()}", "Prefer": 'outlook.body-content-type="text"'},
    )
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            return json.load(resp)
    except urllib.error.HTTPError as exc:
        raise RuntimeError(f"Graph {exc.code}: {exc.read().decode('utf-8', 'replace')[:500]}") from exc


def outlook_email_search(args: dict) -> str:
    folder = (args.get("folderName") or "Inbox").strip().lower()
    top = min(int(args.get("top") or 25), 50)
    params = {"$top": top, "$select": "id,subject,from,receivedDateTime,isRead,bodyPreview"}
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
    message_id = args["id"]
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
        "List Outlook messages in a folder, newest first (or by relevance when query is given). Each result has isRead.",
        {
            "type": "object",
            "properties": {
                "folderName": {"type": "string", "description": "e.g. Inbox, Clutter, JunkEmail"},
                "query": {"type": "string", "description": "Optional search text, e.g. a subject"},
                "top": {"type": "integer", "description": "Max messages, default 25, max 50"},
            },
            "required": ["folderName"],
        },
    ),
    "read_resource": (
        read_resource,
        "Read the full text of one Outlook message by its id from outlook_email_search.",
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


async def generate_brief(prompt_text: str) -> str:
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
        if "texts" in sessions:
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

        next_input = f"Generate today's brief. Today is {today.isoformat()}, a {today.strftime('%A')}."
        if "trilium" in sessions:
            try:
                sober = await sober_line(sessions["trilium"])
                if sober:
                    next_input += f" Sober count: {sober}."
            except Exception as exc:
                log(f"WARN: could not read the Sober note: {exc}")
        previous_id = None
        totals = {"input": 0, "cached": 0, "output": 0}
        for turn in range(1, MAX_TURNS + 1):
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
                log(f"  total tokens: {totals}")
                return response.output_text

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


# prompt.md is shared with daily-brief-cc. Two things this model needs
# spelled out that Claude doesn't:
#  - The Google connector sees more calendars (Birthdays, Vitality) than
#    have iCal feeds here -- deliberately skipped (2026-10-08), so don't let
#    the brief flag them as missing daily.
#  - On the same prompt, the first side-by-side (2026-10-08) came back much
#    thinner than Claude's: ~18 searches vs several dozen, 6 local events vs
#    ~25, 4 concerts vs 11, 3 emails vs 8, intraday market quotes, and no
#    cross-referencing in "Anything else". It stops once a section has something.
PROMPT_ADDENDUM = """

Notes for this run:

- Calendar: list_calendars returns every calendar that's available. Check
  those and only those -- calendars named above that it doesn't return are
  intentionally not connected, so never mention them as missing.

- Thoroughness: this brief is judged on coverage, not brevity of research.
  Don't stop searching once a section has something in it. Aim for 25+ web
  searches overall. In particular:
  - Email: search both Inbox and Clutter every run. Judge each message
    from its subject, sender and preview -- don't open messages with
    read_resource except the Thursday Nextdoor digest. If a subject looks
    important or relevant, include it, read or unread, whichever folder.
    Include anything actionable or time-sensitive -- alerts (water,
    security, data breach), bills and due dates, interview or appointment
    changes. Only drop true newsletters/promotions.
  - News: at least 5 distinct national/world stories, from 2+ searches.
  - Market snapshot: use the most recent official closing prices (the
    previous trading day's close if the market hasn't closed today), not
    intraday quotes, and cite a source that actually shows each figure.
  - Local events: run every required search, then a second search for any
    town that returned fewer than 2 events in the window. Expect 15-25
    events in total on a Thursday.
  - Concerts (Thursdays): at least 4 separate searches (two broad
    Seattle-area scans plus each required venue); list every plausible
    match in the next ~6 weeks, typically 8-12 shows.
  - To Do: the todo get-task-lists/get-tasks tools named above aren't
    available here. Call todo_due_tasks once instead -- it already sweeps
    every list. Section 7 is its overdue/today tasks plus the Important
    list (tasks with important=true); upcoming ones can feed section 8.
  - Anything else interesting: actively cross-reference what you gathered
    -- e.g. balances vs bills coming due, an alert vs an open to-do, account
    connections that need attention in Monarch.
"""


def main():
    prompt_path = SCRIPT_DIR / "prompt.md"
    try:
        brief = asyncio.run(generate_brief(prompt_path.read_text() + PROMPT_ADDENDUM))
    except BaseException as exc:
        cause = root_cause(exc)
        log(f"ERROR: {type(cause).__name__}: {cause}")
        sys.exit(1)
    if not brief.strip():
        raise RuntimeError("model returned an empty brief")
    print(brief)


if __name__ == "__main__":
    main()
