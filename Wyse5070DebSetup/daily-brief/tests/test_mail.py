import json

import pytest

import mail


def fake_graph(folders: dict):
    """graph_get stand-in: folder name -> list of messages; missing folder -> RuntimeError."""
    calls = []

    def get(path, params=None):
        calls.append(path)
        if path.startswith("/me/mailFolders/"):
            folder = path.split("/")[3]
            if folder not in folders:
                raise RuntimeError("Graph 404")
            return {"value": folders[folder]}
        if path.endswith("/attachments") and params:
            return {"value": [
                {"id": "1", "name": "1000997928-006.jpg", "contentType": "image/jpeg"},
                {"id": "2", "name": "mailer-2934.jpg", "contentType": "image/jpeg"},
                {"id": "3", "name": "content-2934.jpg", "contentType": "image/jpeg"},
                {"id": "4", "name": "notes.txt", "contentType": "text/plain"},
            ]}
        if "/attachments/" in path:
            return {"name": "scan", "contentType": "image/jpeg", "contentBytes": "QUJD"}
        raise AssertionError(path)

    return get, calls


def msg(i, sender, subject, when):
    return {"id": i, "subject": subject, "from": {"emailAddress": {"address": sender}}, "receivedDateTime": when}


@pytest.fixture
def usps_mailbox(monkeypatch):
    get, calls = fake_graph({
        "inbox": [msg("a", "friend@example.com", "Hi", "2026-11-14T10:00:00Z")],
        "deleteditems": [msg("b", "USPSInformeddelivery@email.informeddelivery.usps.com",
                             "Your Daily Digest for Sat, 11/14 is ready to view", "2026-11-14T15:17:00Z"),
                         msg("c", "USPSInformeddelivery@email.informeddelivery.usps.com",
                             "Your Daily Digest for Fri, 11/13 is ready to view", "2026-11-13T15:22:00Z")],
        "clutter": [msg("d", "no-reply@rs.email.nextdoor.com", "Weekend events you'll love", "2026-11-12T15:53:00Z")],
        # no "archive": that folder errors and must be skipped
    })
    monkeypatch.setattr(mail, "graph_get", get)
    return calls


def test_find_by_known_sender_key_checks_every_folder_newest_first(usps_mailbox):
    hits = mail.find("usps", "Daily Digest")
    assert [m["id"] for m in hits] == ["b", "c"]
    assert hits[0]["folder"] == "deleteditems"
    assert [c.split("/")[3] for c in usps_mailbox] == mail.FOLDERS


def test_find_by_domain_and_subject(usps_mailbox):
    assert [m["id"] for m in mail.find("rs.email.nextdoor.com", "weekend events")] == ["d"]
    assert mail.find("rs.email.nextdoor.com", "no such subject") == []


def test_find_stops_after_enough(usps_mailbox):
    mail.find("usps", stop_after=1)
    assert len(usps_mailbox) == 2  # inbox, deleteditems


def test_latest_uses_the_senders_subject(usps_mailbox):
    assert mail.latest("usps")["id"] == "b"
    assert mail.latest("nobody@example.com") is None


def test_usps_scans_skips_ads_and_non_images(usps_mailbox):
    scans = mail.usps_scans("b")
    assert len(scans) == 1 and scans[0]["contentBytes"] == "QUJD"


def test_sender_address_maps_keys():
    assert mail.sender_address("usps") == "uspsinformeddelivery@email.informeddelivery.usps.com"
    assert mail.sender_address("Someone@X.com") == "someone@x.com"


def test_brief_email_tool_all_folders_uses_mail_find(usps_mailbox):
    import brief
    out = json.loads(brief.outlook_email_search({"folderName": "all", "sender": "usps"}))
    assert [m["id"] for m in out["messages"]] == ["b", "c"]
    assert out["messages"][0]["folder"] == "deleteditems"
