#!/usr/bin/env python3
"""Sends brief text (Markdown, read from stdin) via Microsoft Graph
(me/sendMail), using this project's own independent OAuth grant against the
same Azure App Registration the Microsoft To Do MCP gateway and the
API-key-based daily-brief project use (see graph_login.py). Kept separate
from Claude Code CLI's own subscription auth -- mail delivery isn't
something to delegate to the model, it's a deterministic last step.
"""

import argparse
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path

import markdown
import msal
from dotenv import load_dotenv

SCRIPT_DIR = Path(__file__).resolve().parent
CONFIG_DIR = SCRIPT_DIR.parent / "config"
load_dotenv(CONFIG_DIR / ".env")

GRAPH_CLIENT_ID = os.environ.get("MS_CLIENT_ID", "d0f1f47d-77ee-494d-a63b-ee3afeb2fad1")
GRAPH_TENANT_ID = os.environ.get("MS_TENANT_ID", "859244b7-6976-450f-92d4-352c7abd6601")
GRAPH_SCOPES = ["Mail.Send"]
GRAPH_TOKEN_CACHE_PATH = Path(os.environ.get("MS_TOKEN_CACHE", str(CONFIG_DIR / "graph_token_cache.json")))

EMAIL_HTML_TEMPLATE = """\
<html><head><style>
  body {{ font-family: -apple-system, Segoe UI, Roboto, Helvetica, Arial, sans-serif;
          font-size: 15px; line-height: 1.5; color: #1a1a1a; max-width: 680px; }}
  h1 {{ font-size: 20px; border-bottom: 2px solid #ddd; padding-bottom: 8px; }}
  h2 {{ font-size: 16px; margin-top: 28px; color: #2c3e50; }}
  ul {{ padding-left: 22px; }}
  li {{ margin-bottom: 4px; }}
  a {{ color: #1a5fb4; }}
</style></head><body>
{body}
</body></html>
"""


def get_graph_access_token() -> str:
    if not GRAPH_TOKEN_CACHE_PATH.is_file():
        print(f"ERROR: {GRAPH_TOKEN_CACHE_PATH} not found -- run graph_login.py once first.", file=sys.stderr)
        sys.exit(1)

    cache = msal.SerializableTokenCache()
    cache.deserialize(GRAPH_TOKEN_CACHE_PATH.read_text())

    app = msal.PublicClientApplication(
        GRAPH_CLIENT_ID,
        authority=f"https://login.microsoftonline.com/{GRAPH_TENANT_ID}",
        token_cache=cache,
    )
    accounts = app.get_accounts()
    if not accounts:
        print("ERROR: no cached Graph account -- run graph_login.py again.", file=sys.stderr)
        sys.exit(1)

    result = app.acquire_token_silent(GRAPH_SCOPES, account=accounts[0])
    if cache.has_state_changed:
        GRAPH_TOKEN_CACHE_PATH.write_text(cache.serialize())

    if not result or "access_token" not in result:
        error = result.get("error_description") if result else "no result"
        print(f"ERROR: Graph token refresh failed: {error}", file=sys.stderr)
        sys.exit(1)
    return result["access_token"]


def render_brief_html(body_markdown: str) -> str:
    body_html = markdown.markdown(body_markdown, extensions=["extra", "sane_lists"])
    return EMAIL_HTML_TEMPLATE.format(body=body_html)


def send_email(subject: str, body_markdown: str) -> None:
    access_token = get_graph_access_token()
    payload = {
        "message": {
            "subject": subject,
            "body": {"contentType": "HTML", "content": render_brief_html(body_markdown)},
            "toRecipients": [{"emailAddress": {"address": os.environ["EMAIL_TO"]}}],
        }
    }
    import json

    request = urllib.request.Request(
        "https://graph.microsoft.com/v1.0/me/sendMail",
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {access_token}",
            "Content-Type": "application/json",
        },
        method="POST",
    )
    try:
        urllib.request.urlopen(request)
    except urllib.error.HTTPError as exc:
        print(f"ERROR: Graph sendMail failed ({exc.code}): {exc.read().decode('utf-8', 'replace')}", file=sys.stderr)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--subject", default="Daily Brief")
    args = parser.parse_args()

    body_markdown = sys.stdin.read()
    if not body_markdown.strip():
        print("ERROR: no brief text on stdin", file=sys.stderr)
        sys.exit(1)

    send_email(args.subject, body_markdown)
    print("Emailed brief successfully.")


if __name__ == "__main__":
    main()
