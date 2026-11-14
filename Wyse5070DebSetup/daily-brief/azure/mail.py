#!/usr/bin/env python3
"""The one way everything here finds Mark's mail (user, 2026-11-14).

Used by brief.py (the brief's outlook_email_search tool and the mail
backstop), replies.py, and the email-lookup skill (this file's CLI, run on
wyse). Folders and known senders come from mail_sources.json, which also
feeds the "Where to find mail" prompt section (mail_guide.py) so the Claude
fallback follows the same rules. Read-only Graph access (Mail.Read) on this
project's own grant.

CLI (on wyse, from /mnt/data/appdata/daily-brief-oai/app):
  venv/bin/python mail.py find <sender key|address|domain> [--days N] [--subject TEXT]
  venv/bin/python mail.py scans [--days N]      # USPS envelope scans -> /tmp/usps
"""

from __future__ import annotations

import argparse
import base64
import datetime
import json
import os
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

import msal
from dotenv import load_dotenv

SCRIPT_DIR = Path(__file__).resolve().parent
CONFIG_DIR = SCRIPT_DIR.parent / "config"
load_dotenv(CONFIG_DIR / ".env")

GRAPH_CLIENT_ID = os.environ.get("MS_CLIENT_ID", "d0f1f47d-77ee-494d-a63b-ee3afeb2fad1")
GRAPH_TENANT_ID = os.environ.get("MS_TENANT_ID", "859244b7-6976-450f-92d4-352c7abd6601")
GRAPH_TOKEN_CACHE_PATH = Path(os.environ.get("MS_TOKEN_CACHE", str(CONFIG_DIR / "graph_token_cache.json")))

# Deployed next to this file; in the repo (tests) it's in ../shared.
_SOURCES_PATH = next(p for p in (SCRIPT_DIR / "mail_sources.json", SCRIPT_DIR.parent / "shared" / "mail_sources.json")
                     if p.exists())
SOURCES = json.loads(_SOURCES_PATH.read_text(encoding="utf-8"))
FOLDERS = [f["graph"] for f in SOURCES["folders"]]
SENDERS = {s["key"]: s for s in SOURCES["senders"]}


# --- Graph -------------------------------------------------------------------

def graph_token(scopes: tuple[str, ...] = ("Mail.Read",)) -> str:
    cache = msal.SerializableTokenCache()
    cache.deserialize(GRAPH_TOKEN_CACHE_PATH.read_text())
    app = msal.PublicClientApplication(
        GRAPH_CLIENT_ID, authority=f"https://login.microsoftonline.com/{GRAPH_TENANT_ID}", token_cache=cache
    )
    accounts = app.get_accounts()
    result = app.acquire_token_silent(list(scopes), account=accounts[0]) if accounts else None
    if cache.has_state_changed:
        GRAPH_TOKEN_CACHE_PATH.write_text(cache.serialize())
    if not result or "access_token" not in result:
        raise RuntimeError(f"Graph {'/'.join(scopes)} token unavailable -- re-run graph_login.py")
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


# --- Finding mail ------------------------------------------------------------

def sender_address(sender: str) -> str:
    """A known-sender key ("usps") or an address/domain, lowercased."""
    return (SENDERS[sender]["address"] if sender in SENDERS else sender).lower()


def _from(m: dict) -> str:
    return (((m.get("from") or {}).get("emailAddress") or {}).get("address") or "").lower()


def find(sender: str | None = None, subject: str | None = None, days: float = 8,
         folders: list[str] | None = None, stop_after: int | None = None,
         select: str = "id,subject,from,receivedDateTime,isRead,bodyPreview") -> list[dict]:
    """Messages from the last `days`, folder by folder in mail_sources.json's
    order, matching `sender` (key, address or domain; substring) and `subject`
    (case-insensitive substring), newest first; each has "folder" added.
    Stops once `stop_after` matches are found. One date-filtered list call per
    folder: a mailbox-wide from+date filter times out, and the search index
    misses Deleted Items and Archive."""
    since = (datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(days=days)).strftime("%Y-%m-%dT%H:%M:%SZ")
    want_from = sender_address(sender) if sender else None
    want_subject = (subject or "").lower()
    found = []
    for folder in folders or FOLDERS:
        try:
            data = graph_get(f"/me/mailFolders/{folder}/messages", {
                "$filter": f"receivedDateTime ge {since}", "$orderby": "receivedDateTime desc",
                "$select": select, "$top": "50"})
        except RuntimeError:
            continue  # e.g. no Archive folder
        for m in data.get("value", []):
            if want_from and want_from not in _from(m):
                continue
            if want_subject and want_subject not in (m.get("subject") or "").lower():
                continue
            found.append({**m, "folder": folder})
        if stop_after and len(found) >= stop_after:
            break
    return sorted(found, key=lambda m: m["receivedDateTime"], reverse=True)


def latest(sender: str, subject: str | None = None, days: float = 8) -> dict | None:
    s = SENDERS.get(sender, {})
    hits = find(sender, subject if subject is not None else s.get("subject") or None, days)
    return hits[0] if hits else None


def usps_scans(message_id: str) -> list[dict]:
    """Envelope scans in an Informed Delivery digest: [{name, contentType,
    contentBytes(base64)}]. Scans are named like 1000997928-006.jpg;
    mailer-*/content-* are USPS ride-along ads and are skipped."""
    atts = graph_get(f"/me/messages/{message_id}/attachments", {"$select": "id,name,contentType"}).get("value", [])
    scans = [a for a in atts if a.get("contentType", "").startswith("image/")
             and not a["name"].startswith(("mailer-", "content-"))]
    return [graph_get(f"/me/messages/{message_id}/attachments/{a['id']}") for a in scans[:10]]


# --- CLI (the email-lookup skill) --------------------------------------------

def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    f = sub.add_parser("find")
    f.add_argument("sender", help=f"known key ({', '.join(SENDERS)}) or address/domain")
    f.add_argument("--days", type=float, default=8)
    f.add_argument("--subject")
    s = sub.add_parser("scans")
    s.add_argument("--days", type=float, default=2)
    args = p.parse_args()
    if args.cmd == "find":
        known = SENDERS.get(args.sender, {})
        for m in find(args.sender, args.subject if args.subject is not None else known.get("subject") or None, args.days):
            print(f"{m['receivedDateTime']}  [{m['folder']}]  {_from(m)}  {m.get('subject')}\n    {(m.get('bodyPreview') or '')[:160]!r}  id={m['id']}")
    else:
        out = Path("/tmp/usps")
        out.mkdir(exist_ok=True)
        for m in find("usps", "Daily Digest", args.days, select="id,subject,from,receivedDateTime"):
            day = m["subject"].split("for ")[-1].split(" is")[0].replace(", ", "_").replace("/", "-")
            scans = usps_scans(m["id"])
            print(f"{m['subject']} [{m['folder']}]: {len(scans)} scans")
            for a in scans:
                path = out / f"{day}_{a['name']}"
                path.write_bytes(base64.b64decode(a["contentBytes"]))
                print("  ", path)


if __name__ == "__main__":
    main()
