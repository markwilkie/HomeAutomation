import asyncio
import datetime
import json
from zoneinfo import ZoneInfo

import digests

TZ = ZoneInfo("America/Los_Angeles")
NOW = datetime.datetime.now(datetime.timezone.utc)
SENT = "sent-folder"


def stamp(days_ago: float) -> str:
    return (NOW - datetime.timedelta(days=days_ago)).strftime("%Y-%m-%dT%H:%M:%SZ")


def msg(mid, subject, frm, days_ago, preview="p" * 255, conv=None, folder="inbox", read=True, to=()):
    return {"id": mid, "conversationId": conv or mid, "subject": subject, "receivedDateTime": stamp(days_ago),
            "from": {"emailAddress": {"address": frm}}, "isRead": read, "bodyPreview": preview,
            "parentFolderId": folder, "toRecipients": [{"emailAddress": {"address": a}} for a in to]}


def setup_function():
    digests.MAIL_REFS.clear()


def test_full_previews_short_refs_and_own_emails_left_out():
    inbox = [msg("A" * 150, "Your bill", "bank@x.com", 1, preview="x" * 255),
             msg("B" * 150, "Daily Brief (OpenAI)", "me@home.net", 0.5),
             msg("C" * 150, "RE: Daily Brief (OpenAI)", "jennie@home.net", 0.4)]
    d = digests.build_mail(inbox, [], [], {}, SENT, TZ)
    assert [r["subject"] for r in d["inbox"]] == ["Your bill"]
    row = d["inbox"][0]
    assert row["preview"] == "x" * 255 and row["n"] == "m1" and "trimmed" not in d
    assert digests.MAIL_REFS["m1"] == "A" * 150


def test_reply_found_in_any_folder_after_my_latest_ask():
    ask = msg("s1", "Passport appointment", "me@home.net", 5, conv="c1", folder=SENT, to=["todd@leidos.com"])
    follow = msg("s2", "RE: Passport appointment", "me@home.net", 3, conv="c1", folder=SENT)
    early = msg("r0", "RE: Passport appointment", "todd@leidos.com", 4, conv="c1", folder="archive")
    holding = msg("r1", "RE: Passport appointment", "todd@leidos.com", 2, conv="c1", folder="deleted",
                  preview="We'll update you")
    d = digests.build_mail([], [], [follow, ask], {"c1": [ask, early, follow, holding]}, SENT, TZ)
    t = d["my_threads"][0]
    assert t["last_word"] == "them" and len(t["my_messages"]) == 2
    assert [r["preview"] for r in t["replies_after_my_latest"]] == ["We'll update you"]
    assert t["they_said_before"][0]["from"] == "todd@leidos.com"


def test_unanswered_ask_and_broken_thread_matched_by_subject():
    ask = msg("s1", "Dinner Friday?", "me@home.net", 3, conv="c1", folder=SENT)
    lonely = msg("s2", "Quote for deck", "me@home.net", 2, conv="c2", folder=SENT)
    rethreaded = msg("i1", "Re: Dinner Friday?", "friend@x.com", 1, conv="other")
    d = digests.build_mail([rethreaded], [], [lonely, ask], {"c1": [ask], "c2": [lonely]}, SENT, TZ)
    words = {t["subject"]: t["last_word"] for t in d["my_threads"]}
    assert words == {"Quote for deck": "me", "Dinner Friday?": "them"}


def test_over_budget_trims_old_read_mail_first():
    inbox = [msg(f"o{i}", f"old {i}", "a@x.com", 5 + i * 0.1) for i in range(40)]
    inbox.insert(0, msg("new", "fresh", "b@x.com", 0.5, read=False, preview="n" * 255))
    d = digests.build_mail(inbox, [], [], {}, SENT, TZ, budget=8000)
    assert d["inbox"][0]["preview"] == "n" * 255
    assert all(len(r["preview"]) <= 120 for r in d["inbox"][1:]) and "trimmed" in d


WA_RECENT = """[2026-11-25 04:10:45] Chat: Avenida Ficoa From: 15551230000: Lunch Sunday?
still on?
[2026-11-25 03:00:00] Chat: 15559870000 From: 15559870000: Can you send the photos
[2026-11-25 02:00:00] Chat: MAYHEM Crew 2027 From: 15554440000: Dock at 0800 Sat"""


async def fake_wa(session, name, args):
    if name == "get_status":
        return json.dumps({"paired": True, "connected": True, "own_phone": "15551230000"})
    if name == "list_chats" and args.get("query") == "mayhem":
        return json.dumps([{"jid": "999@g.us", "name": "MAYHEM Crew 2027", "last_message_time": stamp(1)},
                           {"jid": "888@g.us", "name": "MAYHEM Crew 2024", "last_message_time": stamp(400)}])
    if name == "list_chats":
        return json.dumps([{"jid": "15559870000@s.whatsapp.net", "name": "Todd"},
                           {"jid": "15554440000@s.whatsapp.net", "name": "Skipper"}])
    if name == "list_messages":
        if args.get("page"):
            return ""
        if args.get("chat_jid") == "999@g.us":
            return ("[2026-11-20 18:00:00] Chat: MAYHEM Crew 2027 From: 15554440000: Race is Dec 5\n"
                    "[2026-11-25 02:00:00] Chat: MAYHEM Crew 2027 From: 15554440000: Dock at 0800 Sat")
        assert args.get("chat_jid") != "888@g.us", "quiet Mayhem chat should be skipped"
        return WA_RECENT
    raise AssertionError(name)


def test_whatsapp_digest_groups_names_me_and_mayhem():
    d = json.loads(asyncio.run(digests.whatsapp_digest(None, fake_wa, TZ)))
    chats = {c["chat"]: c["messages"] for c in d["chats_since_yesterday"]}
    assert set(chats) == {"Avenida Ficoa", "Todd"}  # Mayhem listed separately, not twice
    assert chats["Avenida Ficoa"][0] == {"at": "Tue 11/24 8:10 PM", "from": "me", "text": "Lunch Sunday? still on?"}
    mayhem = d["mayhem_chats"]
    assert [c["chat"] for c in mayhem] == ["MAYHEM Crew 2027"]
    assert [m["text"] for m in mayhem[0]["messages"]] == ["Race is Dec 5", "Dock at 0800 Sat"]
    assert mayhem[0]["messages"][0]["from"] == "Skipper"


def test_whatsapp_not_connected_is_a_problem():
    async def down(session, name, args):
        return json.dumps({"paired": False, "connected": False})
    d = json.loads(asyncio.run(digests.whatsapp_digest(None, down, TZ)))
    assert "problem" in d and d["status"]["paired"] is False


def test_whatsapp_trims_other_chats_before_mayhem():
    long = [{"at": NOW, "chat": f"Chat{i}", "from": "x", "text": "y" * 600} for i in range(40)]
    mayhem = {"MAYHEM": [{"at": NOW, "chat": "MAYHEM", "from": "s", "text": "call time " + "z" * 900}]}
    d = digests.build_whatsapp({"paired": True, "connected": True}, long, mayhem, {}, "", TZ, budget=12000)
    assert len(d["chats_since_yesterday"][0]["messages"][0]["text"]) <= 200
    assert len(d["mayhem_chats"][0]["messages"][0]["text"]) == 910


def test_names_label_channels_and_look_up_missing_people():
    async def chats(session, name, args):
        if args.get("query") == "15550001111":
            return json.dumps([{"jid": "15550001111@s.whatsapp.net", "name": "Heather"}])
        return json.dumps([{"jid": "12036300@newsletter", "name": "12036300"}])
    names = asyncio.run(digests._names(None, chats, {"limit": 200}))
    assert names == {"12036300": "a WhatsApp channel (broadcast)"}
    assert asyncio.run(digests._names(None, chats, {"query": "15550001111"})) == {"15550001111": "Heather"}


def test_preferences_prefetched_and_rollback_switch(monkeypatch):
    import brief

    async def trilium(session, name, args):
        if name == "search_notes":
            return json.dumps([{"title": "Daily Brief Preferences", "noteId": "n1"}])
        return json.dumps({"content": "Less sports."})
    monkeypatch.setattr(brief, "call_mcp", trilium)
    out = asyncio.run(brief._prefetch("preferences", {"trilium": object()}))
    assert "Less sports." in out and "don't fetch it" in out
    monkeypatch.setenv("BRIEF_DIGESTS", "off")
    assert not brief.digests_on() and "preferences" in brief.DIGEST_PREFETCH


def test_completeness_check_reply_parsing():
    import brief
    draft = "draft " * 300
    final = "## 🎯 Needs you\n- Seven Pillars homework (email)\n" + "x" * 900
    assert brief.after_check(f"ADDED: Seven Pillars homework\n---BRIEF---\n{final}", draft) == (final, "Seven Pillars homework")
    assert brief.after_check(f"ADDED: nothing.\n---BRIEF---\n{final}", draft) == (final, "")
    assert brief.after_check("Looks complete to me.", draft) == (draft, "")  # not a brief: keep the draft
    assert any(m in " Email digest (Outlook...): {}" for m in brief.DIGEST_MARKERS)
