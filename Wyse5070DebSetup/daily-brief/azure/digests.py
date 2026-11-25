"""Email and WhatsApp gathered in code for the brief's first message
(user, 2026-11-25: cut tokens without losing coverage).

Before this, the model found mail and chats itself: 18 Outlook searches and
9 WhatsApp calls spread over six rounds, every result re-sent on every later
round (~735k input tokens a day). These put the same material in the first
message once, where it's cached for every round after.

  mail_digest()          Inbox + Clutter for MAIL_DAYS with full previews,
                         and every thread I wrote in SENT_DAYS with its
                         replies from any folder (archived/deleted included).
  whatsapp_digest(s)     connection status, every message since yesterday
                         6 AM by chat, every Mayhem chat's last MAYHEM_DAYS.

Rollback: BRIEF_DIGESTS=off in config/.env and the next run gathers the old
way (brief.generate_brief skips these; azure_notes.md has both paths).
Each digest fits its own character budget by trimming the least important
text first, never by cutting the JSON off.
"""

from __future__ import annotations

import datetime
import json
import re
import urllib.parse

import mail

MAIL_DAYS = 14        # replies owed stay 14 days (prompt.md "How long items stay")
SENT_DAYS = 10        # waiting-on-others stays 10 days
MAYHEM_DAYS = 14      # race instructions are posted days ahead
MAIL_BUDGET = 40_000  # chars; measured 2026-11-25: ~32k for 14 days at full previews
WHATSAPP_BUDGET = 24_000
PAGE_LIMIT = 100

# This project's own emails (and replies to them): never news, and the brief
# reading yesterday's brief back would only confuse it.
OWN_SUBJECTS = ("daily brief", "weekend & ahead", "weekly wyse health report", "brief check")

_SELECT = "id,conversationId,subject,from,toRecipients,receivedDateTime,isRead,bodyPreview,parentFolderId,isDraft"

# Short references for messages in the digest: Graph ids are ~150 chars each,
# more than all the older previews together. read_resource maps them back.
MAIL_REFS: dict[str, str] = {}


def _norm_subject(s: str | None) -> str:
    s = (s or "").strip()
    while True:
        t = re.sub(r"^(\[test\]|re|fw|fwd|aw)\s*:?\s*", "", s, flags=re.I).strip()
        if t == s:
            return s.lower()
        s = t


def own_email(subject: str | None) -> bool:
    return _norm_subject(subject).startswith(OWN_SUBJECTS)


def _addr(m: dict) -> str:
    return ((m.get("from") or {}).get("emailAddress") or {}).get("address") or ""


def _when(stamp: str, tz) -> str:
    t = datetime.datetime.fromisoformat(stamp.replace("Z", "+00:00")).astimezone(tz)
    return t.strftime("%a %-m/%-d %-I:%M %p")


def _ref(m: dict) -> str:
    for ref, mid in MAIL_REFS.items():
        if mid == m["id"]:
            return ref
    ref = f"m{len(MAIL_REFS) + 1}"
    MAIL_REFS[ref] = m["id"]
    return ref


def _folder_messages(folder: str, since: datetime.datetime) -> list[dict]:
    rows, skip = [], 0
    stamp = since.strftime("%Y-%m-%dT%H:%M:%SZ")
    while True:
        data = mail.graph_get(f"/me/mailFolders/{urllib.parse.quote(folder)}/messages", {
            "$top": 50, "$skip": skip, "$select": _SELECT,
            "$filter": f"receivedDateTime ge {stamp}", "$orderby": "receivedDateTime desc"})
        page = data.get("value", [])
        rows += page
        if len(page) < 50 or skip >= 500:
            return rows
        skip += 50


def _conversation(cid: str) -> list[dict]:
    """Every message in a thread, whatever folder it's in now."""
    cid = cid.replace("'", "''")
    data = mail.graph_get("/me/messages", {"$filter": f"conversationId eq '{cid}'", "$select": _SELECT, "$top": 50})
    return sorted((m for m in data.get("value", []) if not m.get("isDraft")), key=lambda m: m["receivedDateTime"])


def _row(m: dict, tz, preview: int | None = None) -> dict:
    text = " ".join((m.get("bodyPreview") or "").split())
    return {"n": _ref(m), "from": _addr(m), "subject": m.get("subject"), "at": _when(m["receivedDateTime"], tz),
            "read": bool(m.get("isRead")), "preview": text if preview is None else text[:preview]}


def build_mail(inbox: list[dict], clutter: list[dict], sent: list[dict], conversations: dict[str, list[dict]],
               sent_folder_id: str, tz, budget: int = MAIL_BUDGET) -> dict:
    """The digest from already-fetched messages (pure: tested without Graph)."""
    folders = {"Inbox": [m for m in inbox if not own_email(m.get("subject"))],
               "Clutter": [m for m in clutter if not own_email(m.get("subject"))]}
    threads = []
    seen = set()
    for s in sent:  # newest first
        cid = s.get("conversationId")
        if not cid or cid in seen or own_email(s.get("subject")):
            continue
        seen.add(cid)
        msgs = conversations.get(cid) or [s]
        mine = [m for m in msgs if m.get("parentFolderId") == sent_folder_id or m["id"] == s["id"]]
        last_mine = max(m["receivedDateTime"] for m in mine)
        after = [m for m in msgs if m not in mine and m["receivedDateTime"] > last_mine]
        before = [m for m in msgs if m not in mine and m["receivedDateTime"] <= last_mine]
        # A reply that broke the thread (other account, new message): same subject, after my latest.
        if not after:
            subject = _norm_subject(s.get("subject"))
            after = [m for m in folders["Inbox"] + folders["Clutter"]
                     if m.get("conversationId") != cid and _norm_subject(m.get("subject")) == subject
                     and m["receivedDateTime"] > last_mine]
        threads.append({
            "subject": s.get("subject"),
            "to": [((r.get("emailAddress") or {}).get("address")) for r in s.get("toRecipients") or []],
            "my_messages": [{"at": _when(m["receivedDateTime"], tz), "preview": " ".join((m.get("bodyPreview") or "").split())}
                            for m in sorted(mine, key=lambda m: m["receivedDateTime"])],
            "they_said_before": ([_row(before[-1], tz)] if before else []),
            "replies_after_my_latest": [_row(m, tz) for m in after],
            "last_word": "them" if after else "me",
        })

    digest = {
        "about": (f"Inbox and Clutter for the last {MAIL_DAYS} days (this project's own emails left out), newest "
                  f"first; my threads from Sent Items in the last {SENT_DAYS} days, with replies found in any folder. "
                  "`n` works as the id for read_resource."),
        "inbox": [_row(m, tz) for m in folders["Inbox"]],
        "clutter": [_row(m, tz) for m in folders["Clutter"]],
        "my_threads": threads,
    }

    def size() -> int:
        return len(json.dumps(digest, ensure_ascii=False, separators=(",", ":")))

    # Over budget: shorten read mail older than 2 days first, then all older mail, then everything.
    cutoff = datetime.datetime.now(tz) - datetime.timedelta(days=2)
    for preview, only_old_read in ((120, True), (0, True), (120, False), (0, False)):
        if size() <= budget:
            break
        for name in ("inbox", "clutter"):
            src = folders["Inbox" if name == "inbox" else "Clutter"]
            digest[name] = [
                _row(m, tz, preview)
                if (not only_old_read or (m.get("isRead") and
                    datetime.datetime.fromisoformat(m["receivedDateTime"].replace("Z", "+00:00")) < cutoff))
                else r
                for m, r in zip(src, digest[name])]
        digest["trimmed"] = "older previews shortened to fit; open any message with read_resource"
    return digest


def mail_digest(tz) -> str:
    MAIL_REFS.clear()
    now = datetime.datetime.now(datetime.timezone.utc)
    inbox = _folder_messages("inbox", now - datetime.timedelta(days=MAIL_DAYS))
    try:
        clutter = _folder_messages("clutter", now - datetime.timedelta(days=MAIL_DAYS))
    except RuntimeError:  # no Clutter folder on this mailbox
        clutter = []
    sent = _folder_messages("sentitems", now - datetime.timedelta(days=SENT_DAYS))
    sent_folder_id = mail.graph_get("/me/mailFolders/sentitems", {"$select": "id"})["id"]
    conversations = {}
    for s in sent:
        cid = s.get("conversationId")
        if cid and cid not in conversations and not own_email(s.get("subject")):
            conversations[cid] = _conversation(cid)
    return json.dumps(build_mail(inbox, clutter, sent, conversations, sent_folder_id, tz),
                      ensure_ascii=False, separators=(",", ":"))


# --- WhatsApp -----------------------------------------------------------------

# list_messages lines: "[2026-11-25 04:10:45] Chat: <name or number> From: <name or number>: text"
# (UTC; a body can run onto following lines).
_WA_LINE = re.compile(r"^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)\] Chat: (.*?) From: (.*?): (.*)$")


def parse_messages(text: str) -> list[dict]:
    out = []
    for line in text.splitlines():
        m = _WA_LINE.match(line)
        if m:
            at = datetime.datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S").replace(tzinfo=datetime.timezone.utc)
            out.append({"at": at, "chat": m.group(2).strip(), "from": m.group(3).strip(), "text": m.group(4)})
        elif out and line.strip():
            out[-1]["text"] += " " + line.strip()
    return out


async def _paged(call, session, args: dict) -> list[dict]:
    msgs = []
    for page in range(10):
        got = parse_messages(await call(session, "list_messages", {**args, "page": page}))
        msgs += got
        if len(got) < args["limit"]:
            break
    return msgs


def build_whatsapp(status: dict, recent: list[dict], mayhem: dict[str, list[dict]], names: dict[str, str],
                   own: str, tz, budget: int = WHATSAPP_BUDGET) -> dict:
    """The digest from already-fetched messages (pure: tested without the gateway)."""
    def who(x: str) -> str:
        return "me" if own and x == own else names.get(x, x)

    def rows(msgs: list[dict], chars: int) -> list[dict]:
        return [{"at": m["at"].astimezone(tz).strftime("%a %-m/%-d %-I:%M %p"), "from": who(m["from"]),
                 "text": " ".join(m["text"].split())[:chars]} for m in sorted(msgs, key=lambda m: m["at"])]

    mayhem_names = set(mayhem)
    by_chat: dict[str, list[dict]] = {}
    for m in recent:
        chat = who(m["chat"])
        if chat not in mayhem_names:  # Mayhem chats are listed in full below
            by_chat.setdefault(chat, []).append(m)

    def assemble(other_chars: int, mayhem_chars: int, mayhem_keep: int) -> dict:
        return {
            "about": ("Gathered in code: every message since yesterday 6 AM Pacific by chat, and each Mayhem "
                      f"chat's last {MAYHEM_DAYS} days in full. Times are Pacific; \"me\" is Mark."),
            "status": {"paired": status.get("paired"), "connected": status.get("connected")},
            "chats_since_yesterday": [{"chat": c, "messages": rows(ms, other_chars)}
                                      for c, ms in sorted(by_chat.items(), key=lambda kv: -len(kv[1]))],
            "mayhem_chats": [{"chat": c, "messages": rows(sorted(ms, key=lambda m: m["at"])[-mayhem_keep:], mayhem_chars)}
                             for c, ms in mayhem.items() if ms],
        }

    # Over budget: shorten other chats first; Mayhem (race times, what to bring) last.
    for other_chars, mayhem_chars, keep in ((600, 1200, 400), (200, 1200, 400), (100, 1200, 400), (60, 1200, 400),
                                            (60, 600, 400), (60, 300, 150), (60, 200, 80)):
        digest = assemble(other_chars, mayhem_chars, keep)
        if len(json.dumps(digest, ensure_ascii=False, separators=(",", ":"))) <= budget:
            break
        digest["trimmed"] = "messages shortened to fit; use whatsapp list_messages to read one chat in full"
    return digest


async def _names(session, call, args: dict) -> dict[str, str]:
    """{number: name} from list_chats. A channel (newsletter) has only its number for a name."""
    try:
        chats = json.loads(await call(session, "list_chats", args))
    except (ValueError, TypeError):
        return {}
    out = {}
    for c in chats if isinstance(chats, list) else []:
        number, _, kind = str(c.get("jid", "")).partition("@")
        name = c.get("name") or ""
        if kind == "newsletter" and (not name or name == number):
            name = "a WhatsApp channel (broadcast)"
        elif kind == "broadcast":  # Status updates; messages show the chat by this number-like name
            if name:
                out[name] = "WhatsApp Status updates"
            continue
        if name and name != number:
            out[number] = name
    return out


async def whatsapp_digest(session, call, tz) -> str:
    status = json.loads(await call(session, "get_status", {}))
    if not (status.get("paired") and status.get("connected")):
        return json.dumps({"status": {"paired": status.get("paired"), "connected": status.get("connected")},
                           "problem": "WhatsApp isn't paired and connected (use the WhatsApp fix steps)."})
    own = str(status.get("own_phone") or "")
    now = datetime.datetime.now(tz)
    since = datetime.datetime.combine(now.date() - datetime.timedelta(days=1), datetime.time(6), tz)
    recent = await _paged(call, session, {"after": since.astimezone(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
                                          "limit": PAGE_LIMIT, "include_context": False})
    names = await _names(session, call, {"limit": 200, "sort_by": "last_active"})
    mayhem_list = json.loads(await call(session, "list_chats", {"query": "mayhem", "limit": 50}) or "[]")
    cutoff = now - datetime.timedelta(days=MAYHEM_DAYS)
    mayhem = {}
    for c in mayhem_list:
        last = c.get("last_message_time")
        if last and datetime.datetime.fromisoformat(last.replace("Z", "+00:00")) < cutoff:
            continue
        mayhem[c.get("name") or c["jid"]] = await _paged(call, session, {
            "chat_jid": c["jid"], "after": cutoff.astimezone(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "limit": 50, "include_context": False})
    # Chats and senders still shown as a number: the default chat list doesn't
    # include everyone (2026-11-25: a DM was missing), but a lookup finds them.
    unknown = {x for m in recent + [m for ms in mayhem.values() for m in ms] for x in (m["chat"], m["from"])
               if x.isdigit() and x != own and x not in names}
    for number in sorted(unknown)[:20]:
        names.update(await _names(session, call, {"query": number, "limit": 5}))
    return json.dumps(build_whatsapp(status, recent, mayhem, names, own, tz), ensure_ascii=False, separators=(",", ":"))
