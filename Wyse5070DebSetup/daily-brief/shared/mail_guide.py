#!/usr/bin/env python3
"""Render the "Where to find mail" prompt section from mail_sources.json.

The setup scripts write it to mail_guide.md next to the deployed prompts (jobs.json
lists it for each job), so the Azure brief and the Claude fallback follow the same folder rules as the code
in daily-brief/azure/mail.py. Standard library only (runs with system python3).

Usage: python3 mail_guide.py > mail_guide.md
"""

import json
from pathlib import Path

SOURCES = Path(__file__).resolve().parent / "mail_sources.json"


def load() -> dict:
    return json.loads(SOURCES.read_text(encoding="utf-8"))


def render() -> str:
    src = load()
    folders = ", ".join(f["name"] for f in src["folders"])
    rows = "\n".join(
        f"| {s['what']} | `{s['address']}` | {s['usually']} | {s['when']} |" for s in src["senders"]
    )
    return f"""

## Where to find mail

Mail is often read and then deleted or archived, and Outlook files some
senders in Clutter. To find a particular message, look in each of these
folders, in this order, and stop once found: {folders}. A plain search
without a folder misses Deleted Items and Archive. With
`outlook_email_search`, use `folderName` with `sender` for a known sender
(on Azure runs, `folderName` "all" checks every folder above in one call).
New mail for the brief still comes from Inbox and Clutter.

| What | Sender | Usually in | When |
|---|---|---|---|
{rows}
"""


if __name__ == "__main__":
    print(render(), end="")
