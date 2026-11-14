import asyncio
import datetime
import json

import brief

NOW = datetime.datetime.now(datetime.timezone.utc)


def stamp(days_ago: float) -> str:
    return (NOW - datetime.timedelta(days=days_ago)).strftime("%Y-%m-%dT%H:%M:%SZ")


CONVERSATIONS = {
    # my unanswered ask, quiet for days: must reach the model
    "2177": ("Isaac Wellborn", [(5, "←", "Isaac Wellborn", "half an hour*"),
                                (4, "→", "Me", "Dinner Friday the night of the 20th?")]),
    # they asked me something: open on me
    "15": ("Karl Abel", [(3, "→", "Me", "How is he?"), (2, "←", "Karl Abel", "Home tomorrow?")]),
    # older than the window: left out
    "9": ("Old Friend", [(20, "→", "Me", "Hey")]),
}


async def fake_call(session, name, args):
    if name == "get_status":
        return "ok"
    if name == "list_conversations":
        rows = sorted(CONVERSATIONS.items(), key=lambda kv: kv[1][1][-1][0])
        return "\n".join(f"- {n} (ID: {cid}, last: {stamp(msgs[-1][0])})" for cid, (n, msgs) in rows)
    msgs = CONVERSATIONS[args["conversation_id"]][1]
    return "\n".join(f"[{stamp(d)}] {a} {s}: «{b}»" for d, a, s, b in reversed(msgs))


def test_quiet_thread_where_i_asked_last_is_kept(monkeypatch):
    monkeypatch.setattr(brief, "call_mcp", fake_call)
    out = json.loads(asyncio.run(brief.texts_digest(None)))
    names = {c["name"]: c for c in out["conversations"]}
    assert set(names) == {"Isaac Wellborn", "Karl Abel"}
    isaac = names["Isaac Wellborn"]
    assert isaac["last_from_me"] and isaac["messages"][-1]["text"] == "Dinner Friday the night of the 20th?"
