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
import re
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


# Despite prompt.md repeatedly asking for real Markdown bullets, the model
# intermittently emits a multi-item section as one line like
# "**Calendar** - item - item - item" instead -- seen across several
# different sections on different days (Calendar & Email on 9/29 and 9/30,
# then Market Snapshot too on 10/1, after the 9/30 prompt.md fix targeted
# at Calendar & Email specifically did nothing for either). Prompt wording
# alone isn't reliably fixing this, so this is a deterministic backstop:
# normalize that pattern into real bullets before rendering, regardless of
# what the model outputs. Only the literal ASCII " - " triggers it -- the
# model consistently uses em/en dashes ("—"/"–") and a true minus sign
# ("−") within normal prose (times, stat deltas), never this exact
# sequence, so real sentences are untouched. Requires 2+ occurrences (3+
# items) to avoid misfiring on a single incidental hyphenated phrase --
# this doesn't fully eliminate false positives (a genuine sentence with
# two incidental " - " uses would also get split) but prompt.md already
# asks for every section to be bullets, never prose, so a real multi-dash
# sentence essentially shouldn't occur in this pipeline's actual output.
_LIST_MARKER_RE = re.compile(r"^(-|\*|\+)\s")  # a real bullet marker, NOT "**bold**"
_RUN_ON_LABEL_RE = re.compile(r"^(\*{1,2}[^*]+\*{1,2}|[A-Za-z][A-Za-z0-9 /]{0,40}:)$")


def _fix_run_on_bullets(line: str) -> list[str]:
    parts = line.split(" - ")
    if len(parts) < 3:
        return [line]
    rest = parts
    out = []
    if _RUN_ON_LABEL_RE.match(parts[0].strip()):
        out.append(parts[0].strip())
        # Markdown only recognizes the following lines as a list if a blank
        # line separates them from this label -- without it, these "- "
        # lines are lazy continuation of the same paragraph, and get
        # rejoined with spaces at render time, recreating the exact run-on
        # text this function exists to fix.
        out.append("")
        rest = parts[1:]
    out.extend(f"- {p.strip()}" for p in rest)
    return out


def _ensure_blank_before_lists(lines: list[str]) -> list[str]:
    # Markdown (this project uses classic python-markdown, not CommonMark)
    # only starts a new list where a blank line precedes it; a "- " line
    # immediately after non-list prose is lazy continuation of that same
    # paragraph instead, and gets rejoined into one run-on block at render
    # time regardless of how well-formed the list itself is. This has bitten
    # us even when the model emits genuinely correct multi-line bullets --
    # e.g. a one-line intro ("Thursday's closes:") directly followed by real
    # "- **S&P 500:** ..." lines with no blank line between them -- which
    # _fix_run_on_bullets can't catch because no single line of that input
    # ever contains the " - " join pattern it looks for. This pass is a
    # second, independent backstop that looks at line transitions instead.
    out = []
    prev_stripped = ""
    for line in lines:
        stripped = line.strip()
        if _LIST_MARKER_RE.match(stripped) and prev_stripped and not _LIST_MARKER_RE.match(prev_stripped):
            out.append("")
        out.append(line)
        prev_stripped = stripped
    return out


def normalize_markdown(body_markdown: str) -> str:
    out_lines = []
    for line in body_markdown.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or _LIST_MARKER_RE.match(stripped):
            out_lines.append(line)
            continue
        out_lines.extend(_fix_run_on_bullets(line))
    return "\n".join(_ensure_blank_before_lists(out_lines))


def render_brief_html(body_markdown: str) -> str:
    body_html = markdown.markdown(normalize_markdown(body_markdown), extensions=["extra", "sane_lists"])
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
