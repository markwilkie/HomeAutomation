import json
import urllib.parse

import gcal_links
import mail_guide


def params(url: str) -> dict:
    return dict(urllib.parse.parse_qsl(urllib.parse.urlparse(url).query))


def target(text: str) -> str:
    return text.split("](", 1)[1].rsplit(")", 1)[0]


def test_timed_event_defaults_to_two_hours():
    p = params(target(gcal_links.expand("x [📅](gcal:Stickshift Annie|2026-11-21T19:00||Third Place Commons, LFP)")))
    assert p["text"] == "Stickshift Annie"
    assert p["dates"] == "20261121T190000/20261121T210000"
    assert p["ctz"] == "America/Los_Angeles"
    assert p["location"] == "Third Place Commons, LFP"


def test_all_day_event_with_end():
    p = params(target(gcal_links.expand("[📅](gcal:Market|2026-11-21|2026-11-23|Edmonds)")))
    assert p["dates"] == "20261121/20261123"


def test_event_page_is_last_source_link_on_the_same_line():
    line = "- 7pm Show ([a](https://a.example/1)) ([b](https://b.example/2)) [📅](gcal:Show|2026-11-21T19:00||Place)"
    out = gcal_links.expand(line + "\n- other [📅](gcal:Other|2026-11-22||)")
    first, second = out.split("\n")
    assert params(first.rsplit("](", 1)[1].rstrip(")"))["details"] == "Event page: https://b.example/2"
    assert "details" not in params(second.rsplit("](", 1)[1].rstrip(")"))


def test_unparseable_date_falls_back_to_search():
    out = gcal_links.expand("[📅](gcal:Oops|soon||Somewhere)")
    assert target(out).startswith("https://www.google.com/search?q=Oops")


def test_text_without_targets_is_unchanged():
    text = "## Heading\n- plain [link](https://example.com)\n"
    assert gcal_links.expand(text) == text


def test_mail_guide_lists_every_folder_and_sender():
    src = mail_guide.load()
    out = mail_guide.render()
    for f in src["folders"]:
        assert f["name"] in out
    for s in src["senders"]:
        assert s["address"] in out
    assert out.count("## Where to find mail") == 1


def test_mail_sources_is_well_formed():
    src = mail_guide.load()
    keys = [s["key"] for s in src["senders"]]
    assert len(keys) == len(set(keys))
    assert {"inbox", "deleteditems", "archive", "clutter"} <= {f["graph"] for f in src["folders"]}
    for s in src["senders"]:
        assert {"key", "what", "address", "usually", "when", "subject"} <= set(s)
        assert "@" in s["address"]
    json.dumps(src)  # serializable
