#!/usr/bin/env python3
"""Apply Mark's and Jennie's reply commands to the brief and the events email.

Runs first thing in run.sh (daily brief) and run_events.sh (events email), so
a reply sent since the last run shapes this one. A reply to a "Daily Brief"
or "Weekend & Ahead" email may hold commands, one per line:

    skip <thing>    (also: no, less)    add to the skip / leave-out list
    more <thing>    (also: add, like)   add to the likes / include list
    undo                                remove your last change
    show                                reply with the current list

Replies to the events email edit the Trilium note "Events Taste Profile";
replies to the brief edit "Daily Brief Preferences". Only these commands
work: no model reads the reply (user, 2026-11-14 -- predictable, free, and
anything bigger is made directly). Anything else gets a help email; every
change gets a confirmation. Only mail from mark@ / jennie@ that Exchange
marks as sent inside this tenant (X-MS-Exchange-Organization-AuthAs:
Internal) counts, so a spoofed From can't edit anything. Trilium keeps the
previous version of a note as a revision.

State (processed ids, change history for undo): ../state/replies.json.
Never blocks the email that follows: the run scripts ignore its exit code.

Usage: replies.py [--dry-run]   # show what it would do, change nothing
"""

from __future__ import annotations

import argparse
import asyncio
import datetime
import json
import re
import sys
import urllib.error
import urllib.request
from contextlib import AsyncExitStack

import msal
from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client

import brief  # Graph settings, MCP URLs, log()

STATE = brief.SCRIPT_DIR.parent / "state" / "replies.json"
ALLOWED_SENDERS = {"mark@wilkiefamily.net", "jennie@wilkiefamily.net"}
LOOKBACK_DAYS = 8
MAX_COMMANDS = 10
MAX_THING_CHARS = 80
NOTES = {"events": "Events Taste Profile", "brief": "Daily Brief Preferences"}
# Section headings each command writes under, per note (first match wins).
SECTIONS = {
    "events": {"skip": ["SKIP"], "more": ["LIKE"]},
    "brief": {"skip": ["LEAVE OUT"], "more": ["INCLUDE"]},
}
FEEDBACK_HEADING = "NOTES / FEEDBACK"
VERBS = {"skip": "skip", "no": "skip", "less": "skip", "more": "more", "add": "more", "like": "more"}
# Ends every reply we send. A reply to Mark lands in his own Inbox as
# "RE: Daily Brief", so without this it would be read as a command next run.
MARKER = "(Automated reply from the brief.)"
HELP = """To adjust {what}, reply with one command per line:
  skip <thing>   (or: no, less)  e.g. "skip ice skating"
  more <thing>   (or: add, like) e.g. "more breweries"
  undo           remove your last change
  show           see the current list
Bigger changes are made directly, not by reply."""


# --- Graph -------------------------------------------------------------------

def graph_token() -> str:
    cache = msal.SerializableTokenCache()
    cache.deserialize(brief.GRAPH_TOKEN_CACHE_PATH.read_text())
    app = msal.PublicClientApplication(
        brief.GRAPH_CLIENT_ID, authority=f"https://login.microsoftonline.com/{brief.GRAPH_TENANT_ID}", token_cache=cache
    )
    accounts = app.get_accounts()
    result = app.acquire_token_silent(["Mail.Read", "Mail.Send"], account=accounts[0]) if accounts else None
    if cache.has_state_changed:
        brief.GRAPH_TOKEN_CACHE_PATH.write_text(cache.serialize())
    if not result or "access_token" not in result:
        raise RuntimeError("Graph token unavailable -- re-run graph_login.py")
    return result["access_token"]


def graph(method: str, path: str, body: dict | None = None) -> dict:
    req = urllib.request.Request(
        "https://graph.microsoft.com/v1.0" + path,
        method=method,
        data=json.dumps(body).encode() if body is not None else None,
        headers={
            "Authorization": f"Bearer {graph_token()}",
            "Content-Type": "application/json",
            "Prefer": 'outlook.body-content-type="text"',
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            raw = resp.read()
            return json.loads(raw) if raw else {}
    except urllib.error.HTTPError as exc:
        raise RuntimeError(f"Graph {exc.code}: {exc.read().decode('utf-8', 'replace')[:300]}") from exc


def reply(message_id: str, text: str) -> None:
    graph("POST", f"/me/messages/{message_id}/reply", {"comment": f"{text}\n\n{MARKER}"})


def kind_of(subject: str) -> str | None:
    s = (subject or "").lower()
    if not s.startswith(("re:", "re ")):
        return None
    if "weekend & ahead" in s or "weekend &amp; ahead" in s:
        return "events"
    if "daily brief" in s:
        return "brief"
    return None


def new_replies(done: set[str]) -> list[dict]:
    since = (datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(days=LOOKBACK_DAYS)).strftime("%Y-%m-%dT%H:%M:%SZ")
    found = []
    for folder in ("inbox", "clutter"):
        try:
            data = graph("GET", f"/me/mailFolders/{folder}/messages?$top=50&$orderby=receivedDateTime%20desc"
                                f"&$filter=receivedDateTime%20ge%20{since}&$select=id,subject,from,receivedDateTime")
        except RuntimeError as exc:
            brief.log(f"replies: can't list {folder}: {exc}")
            continue
        found += [m for m in data.get("value", []) if m["id"] not in done and kind_of(m.get("subject"))]
    return sorted(found, key=lambda m: m["receivedDateTime"])


def read_reply(message_id: str) -> tuple[bool, str, str]:
    """(trusted, sender, the reply's own new text)."""
    m = graph("GET", f"/me/messages/{message_id}?$select=from,uniqueBody,internetMessageHeaders")
    sender = ((m.get("from") or {}).get("emailAddress") or {}).get("address", "").lower()
    headers = {h["name"].lower(): h["value"] for h in m.get("internetMessageHeaders") or []}
    internal = headers.get("x-ms-exchange-organization-authas", "").lower() == "internal"
    text = ((m.get("uniqueBody") or {}).get("content") or "").strip()
    return sender in ALLOWED_SENDERS and internal, sender, text


# --- Commands ----------------------------------------------------------------

_SIGNATURE = re.compile(r"^(sent from|get outlook|-- ?$|__+$|from:|on .+ wrote:)", re.I)


def parse(text: str) -> tuple[list[tuple[str, str]], list[str]]:
    """([(command, thing)], lines not understood). Stops at a signature or quote."""
    commands, unknown = [], []
    for raw in text.splitlines():
        line = raw.strip().strip("•*-").strip()
        if not line:
            continue
        if _SIGNATURE.match(line) or line.startswith(">"):
            break
        words = re.sub(r"^please[, ]+", "", line, flags=re.I).rstrip(".!").split(None, 1)
        verb = words[0].lower().rstrip(":,") if words else ""
        if verb in ("undo", "show") and len(words) == 1:
            commands.append((verb, ""))
        elif verb in VERBS and len(words) == 2 and 0 < len(words[1].strip()) <= MAX_THING_CHARS:
            commands.append((VERBS[verb], words[1].strip().strip('"“”')))
        else:
            unknown.append(line)
    return commands[:MAX_COMMANDS], unknown


def insert_under(note: str, headings: list[str], line: str) -> str:
    """Add `line` as the last bullet of the first section whose heading starts
    with one of `headings`; an empty "- " placeholder bullet is replaced."""
    lines = note.split("\n")
    start = next((i for i, l in enumerate(lines) if any(l.strip().upper().startswith(h) for h in headings)), None)
    if start is None:
        return note.rstrip("\n") + f"\n\n{headings[0]}\n{line}\n"
    end = start + 1
    while end < len(lines) and lines[end].strip().startswith("-"):
        end += 1
    if end - 1 > start and lines[end - 1].strip() == "-":
        lines[end - 1] = line
    else:
        lines.insert(end, line)
    return "\n".join(lines)


def remove_lines(note: str, removed: list[str]) -> str:
    lines = note.split("\n")
    for r in removed:
        if r in lines:
            lines.remove(r)
    return "\n".join(lines)


async def trilium_note(session: ClientSession, title: str) -> tuple[str, str, str]:
    """(note id, content, blobId) of the note with this exact title."""
    found = json.loads(await brief.call_mcp(session, "search_notes", {"searchCriteria": [
        {"type": "noteProperty", "property": "title", "op": "=", "value": title, "logic": "AND"}], "limit": 10}))
    match = next((n for n in found if n.get("title") == title), None)
    if not match:
        raise RuntimeError(f"Trilium note {title!r} not found")
    note = json.loads(await brief.call_mcp(session, "get_note", {"noteId": match["noteId"]}))
    return match["noteId"], note.get("content", ""), note["blobId"]


def apply(which: str, who: str, note: str, commands: list[tuple[str, str]], history: list[dict]):
    """Returns (new note, lines for the reply, added lines for history, show requested)."""
    today = datetime.datetime.now(brief.TZ).date().isoformat()
    said, added, show = [], [], False
    for cmd, thing in commands:
        if cmd == "show":
            show = True
        elif cmd == "undo":
            last = next((h for h in reversed(history) if h["who"] == who and h["note"] == which and not h.get("undone")), None)
            if not last:
                said.append("Nothing of yours to undo.")
                continue
            note = remove_lines(note, last["lines"])
            last["undone"] = True
            said.append("Undone: " + "; ".join(last["summary"]))
        else:
            rule = f"- {thing}"
            if rule.lower() in (l.strip().lower() for l in note.split("\n")):
                said.append(f"Already there: {thing}")
                continue
            label = "skip" if cmd == "skip" else "more"
            note = insert_under(note, SECTIONS[which][cmd], rule)
            feedback = f"- {today} {who}: {label} {thing}"
            note = insert_under(note, [FEEDBACK_HEADING], feedback)
            added.append({"rule": rule, "feedback": feedback, "said": f"{'Added to skips' if cmd == 'skip' else 'Added to likes'}: {thing}"})
            said.append(added[-1]["said"])
    return note, said, added, show


async def main(dry_run: bool) -> None:
    state = json.loads(STATE.read_text()) if STATE.exists() else {"done": [], "history": []}
    done = set(state["done"])
    replies = new_replies(done)
    if not replies:
        brief.log("replies: none new")
        return
    async with AsyncExitStack() as stack:
        read, write = await stack.enter_async_context(streamable_http_client(brief.MCP_SERVERS["trilium"]))
        session = await stack.enter_async_context(ClientSession(read, write))
        await session.initialize()
        for m in replies:
            which = kind_of(m["subject"])
            what = "the events email" if which == "events" else "the daily brief"
            try:
                trusted, sender, text = read_reply(m["id"])
                if not trusted:
                    brief.log(f"replies: skipped {m['subject']!r} from {sender or '?'} (not Mark/Jennie from inside the tenant)")
                elif MARKER in text or not text:
                    pass  # our own reply, or empty
                else:
                    who = "Jennie" if sender.startswith("jennie") else "Mark"
                    commands, unknown = parse(text)
                    brief.log(f"replies: {who} on {which}: {commands or 'no commands'}; not understood: {unknown}")
                    if dry_run:
                        pass
                    elif not commands:
                        reply(m["id"], "I only understand a few commands, so nothing was changed.\n\n" + HELP.format(what=what))
                    else:
                        note_id, note, blob = await trilium_note(session, NOTES[which])
                        new_note, said, added, show = apply(which, who, note, commands, state["history"])
                        if new_note != note:
                            await brief.call_mcp(session, "update_note", {
                                "noteId": note_id, "content": new_note, "expectedHash": blob, "mode": "overwrite"})
                        if added:
                            state["history"].append({"who": who, "note": which, "at": m["receivedDateTime"],
                                                     "lines": [x for a in added for x in (a["rule"], a["feedback"])],
                                                     "summary": [a["said"] for a in added]})
                            state["history"] = state["history"][-50:]
                        body = "\n".join(said)
                        if show:
                            body += ("\n\n" if body else "") + f"Current list (Trilium note \"{NOTES[which]}\"):\n\n{new_note.strip()}"
                        if unknown:
                            body += "\n\nNot understood, so ignored: " + " / ".join(unknown[:5]) + "\n\n" + HELP.format(what=what)
                        reply(m["id"], body.strip())
            except Exception as exc:  # one bad reply mustn't stop the rest, or the email after
                brief.log(f"replies: error on {m['subject']!r}: {type(exc).__name__}: {exc}")
                continue
            if not dry_run:
                done.add(m["id"])
                state["done"] = sorted(done)
                STATE.parent.mkdir(parents=True, exist_ok=True)
                STATE.write_text(json.dumps(state, indent=1))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true")
    asyncio.run(main(parser.parse_args().dry_run))
    sys.exit(0)
