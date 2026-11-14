import replies

EVENTS_NOTE = """Events Taste Profile.

LIKE
- Live music
- Breweries

SKIP
- Kids' events

NOTES / FEEDBACK (add lines as we go)
-
"""

BRIEF_NOTE = """Daily Brief Preferences.

INCLUDE / MORE OF
-

LEAVE OUT / LESS OF
-

NOTES / FEEDBACK (dated, newest last)
-
"""


def test_parse_commands_synonyms_and_politeness():
    cmds, unknown = replies.parse('Please skip ice skating\nmore breweries.\nLess: wine bars\nadd "jazz"\nshow\nundo')
    assert cmds == [("skip", "ice skating"), ("more", "breweries"), ("skip", "wine bars"),
                    ("more", "jazz"), ("show", ""), ("undo", "")]
    assert unknown == []


def test_parse_stops_at_signature_and_quotes():
    cmds, unknown = replies.parse("skip trivia\n\nSent from my iPhone\nskip everything")
    assert cmds == [("skip", "trivia")] and unknown == []
    cmds, _ = replies.parse("more jazz\n> skip quoted")
    assert cmds == [("more", "jazz")]


def test_parse_reports_free_text_and_limits():
    cmds, unknown = replies.parse("we hate kid stuff\nskip " + "x" * 200)
    assert cmds == [] and unknown == ["we hate kid stuff", "skip " + "x" * 200]
    many = "\n".join(f"skip thing {i}" for i in range(20))
    assert len(replies.parse(many)[0]) == replies.MAX_COMMANDS


def test_apply_adds_under_the_right_sections_and_logs_feedback():
    note, said, added, show = replies.apply("events", "Jennie", EVENTS_NOTE,
                                            [("skip", "ice skating"), ("more", "jazz")], [])
    lines = note.split("\n")
    assert lines.index("- ice skating") == lines.index("- Kids' events") + 1
    assert lines.index("- jazz") == lines.index("- Breweries") + 1
    assert any(l.endswith("Jennie: skip ice skating") for l in lines)
    assert "-" not in [l.strip() for l in lines]  # the empty feedback placeholder was used
    assert said == ["Added to skips: ice skating", "Added to likes: jazz"] and not show


def test_apply_fills_placeholders_in_the_brief_note():
    note, _, _, _ = replies.apply("brief", "Mark", BRIEF_NOTE, [("skip", "Monarch reconnects")], [])
    lines = note.split("\n")
    assert lines[lines.index("LEAVE OUT / LESS OF") + 1] == "- Monarch reconnects"
    assert lines[lines.index("INCLUDE / MORE OF") + 1].strip() == "-"  # placeholder untouched


def test_apply_already_there_and_undo():
    history = []
    note, said, added, _ = replies.apply("events", "Mark", EVENTS_NOTE, [("more", "breweries")], history)
    assert said == ["Already there: breweries"] and not added and note == EVENTS_NOTE
    note, _, added, _ = replies.apply("events", "Mark", EVENTS_NOTE, [("skip", "trivia")], history)
    history.append({"who": "Mark", "note": "events", "summary": [a["said"] for a in added],
                    "lines": [x for a in added for x in (a["rule"], a["feedback"])]})
    undone, said, _, _ = replies.apply("events", "Mark", note, [("undo", "")], history)
    assert "- trivia" not in undone and said[0].startswith("Undone:")
    _, said, _, _ = replies.apply("events", "Jennie", undone, [("undo", "")], history)
    assert said == ["Nothing of yours to undo."]


def test_kind_of_routes_by_subject():
    assert replies.kind_of("RE: Daily Brief (OpenAI)") == "brief"
    assert replies.kind_of("Re: Weekend & Ahead: Thu Nov 19") == "events"
    assert replies.kind_of("Daily Brief (OpenAI)") is None  # not a reply
    assert replies.kind_of("Re: dinner") is None
