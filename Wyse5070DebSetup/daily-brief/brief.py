#!/usr/bin/env python3
"""Daily brief: one Anthropic Messages API tool-use loop that pulls from
this host's local MCP gateways (Todo, Trilium, Monarch) plus Anthropic's
hosted web_search tool, then emails the result.

Run by cron directly on wyse -- see ../setup-daily-brief.sh. Local MCP
servers are reached over plain HTTP on 127.0.0.1, no auth needed, since
this script runs on the same host they're bound to.
"""

import argparse
import asyncio
import datetime
import json
import os
import sys
import urllib.error
import urllib.request
from contextlib import AsyncExitStack
from pathlib import Path

import anthropic
import markdown
import msal
from dotenv import load_dotenv
from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client

SCRIPT_DIR = Path(__file__).resolve().parent
CONFIG_DIR = SCRIPT_DIR.parent / "config"

# Same Azure App Registration already used for the Microsoft To Do MCP
# gateway (see setup-microsoft-todo-mcp.sh) -- this script gets its own,
# independent OAuth grant against it (own token cache file below), for the
# same reason the To Do gateway has its own: two long-running consumers
# refreshing the same token file would race and invalidate each other's.
GRAPH_CLIENT_ID = os.environ.get("MS_CLIENT_ID", "d0f1f47d-77ee-494d-a63b-ee3afeb2fad1")
GRAPH_TENANT_ID = os.environ.get("MS_TENANT_ID", "859244b7-6976-450f-92d4-352c7abd6601")
GRAPH_SCOPES = ["Mail.Send"]
GRAPH_TOKEN_CACHE_PATH = Path(os.environ.get("MS_TOKEN_CACHE", str(CONFIG_DIR / "graph_token_cache.json")))

# Local MCP gateways this host already runs (see Wyse5070DebSetup/README.md's
# port table). Prefixed tool names below keep them from colliding.
MCP_SERVERS = {
    "todo": os.environ.get("TODO_MCP_URL", "http://127.0.0.1:8600/mcp"),
    "trilium": os.environ.get("TRILIUM_MCP_URL", "http://127.0.0.1:8601/mcp"),
    "monarch": os.environ.get("MONARCH_MCP_URL", "http://127.0.0.1:8602/mcp"),
}

MODEL = os.environ.get("MODEL", "claude-sonnet-5")
MAX_TOOL_ITERATIONS = 15
WEB_SEARCH_MAX_USES = int(os.environ.get("WEB_SEARCH_MAX_USES", "5"))

# Each gateway exposes many more tools (budgets, transaction rules, note
# editing/deletion, task-list creation, ...) than this read-only brief ever
# calls -- see prompt.md for what's actually used. Their full schemas would
# otherwise add ~30k+ tokens of pure overhead, freshly cache-written on
# every single run (the cache doesn't persist across separate daily runs).
TOOL_ALLOWLIST = {
    "todo": {"get-task-lists", "get-task-lists-organized", "get-tasks"},
    "trilium": {"search_notes", "list_children_notes", "get_note", "resolve_note_id"},
    "monarch": {"get_accounts", "get_transactions", "search_transactions"},
}


async def connect_mcp_servers(stack: AsyncExitStack) -> dict[str, ClientSession]:
    """Connect to each local MCP gateway. A gateway that's down is skipped
    (logged, not fatal) so the brief still goes out with what's available."""
    sessions: dict[str, ClientSession] = {}
    for server_key, url in MCP_SERVERS.items():
        try:
            read, write = await stack.enter_async_context(streamable_http_client(url))
            session = await stack.enter_async_context(ClientSession(read, write))
            await session.initialize()
            sessions[server_key] = session
        except Exception as exc:
            print(f"WARN: could not reach {server_key} MCP gateway at {url}: {exc}", file=sys.stderr)
    return sessions


async def build_tool_catalog(sessions: dict[str, ClientSession]):
    """Returns (anthropic_tools, dispatch) where dispatch maps a prefixed
    tool name back to (session, original_name) for calling it."""
    anthropic_tools = []
    dispatch: dict[str, tuple[ClientSession, str]] = {}
    for server_key, session in sessions.items():
        result = await session.list_tools()
        allowed = TOOL_ALLOWLIST.get(server_key)
        for tool in result.tools:
            if allowed is not None and tool.name not in allowed:
                continue
            prefixed_name = f"{server_key}__{tool.name}"
            anthropic_tools.append(
                {
                    "name": prefixed_name,
                    "description": tool.description or "",
                    "input_schema": tool.input_schema,
                }
            )
            dispatch[prefixed_name] = (session, tool.name)
    return anthropic_tools, dispatch


def mcp_result_to_tool_result_content(result):
    """Flatten an MCP CallToolResult's content blocks into the plain text
    Anthropic's tool_result expects."""
    parts = []
    for block in result.content:
        if getattr(block, "text", None) is not None:
            parts.append(block.text)
        else:
            parts.append(str(block))
    text = "\n".join(parts) or "(no output)"
    if result.is_error:
        return [{"type": "text", "text": f"Tool error: {text}"}]
    return [{"type": "text", "text": text}]


def web_search_tool():
    tool = {"type": "web_search_20250305", "name": "web_search", "max_uses": WEB_SEARCH_MAX_USES}
    city = os.environ.get("WEB_SEARCH_CITY")
    if city:
        tool["user_location"] = {
            "type": "approximate",
            "city": city,
            "region": os.environ.get("WEB_SEARCH_REGION", ""),
            "country": os.environ.get("WEB_SEARCH_COUNTRY", "US"),
            "timezone": os.environ.get("WEB_SEARCH_TIMEZONE", ""),
        }
    return tool


class CacheBreakpoint:
    """Tracks the single dynamic cache breakpoint in the growing messages
    list and moves it each turn, clearing the previous one first -- the API
    allows at most 4 cache_control blocks per request, and system+tools
    already use 2, so a stray leftover marker on every past turn would blow
    past that limit within a few turns.

    Without this, every turn re-sends and re-bills the entire (growing)
    conversation as fresh input tokens -- a multi-turn tool loop like this
    one can otherwise balloon into hundreds of thousands of billed input
    tokens. A cache hit costs 10% of the normal input price."""

    def __init__(self):
        self._marked_block: dict | None = None

    def move_to_end(self, messages: list[dict]) -> None:
        if self._marked_block is not None:
            self._marked_block.pop("cache_control", None)
        last_content = messages[-1]["content"]
        if isinstance(last_content, list) and last_content:
            last_content[-1]["cache_control"] = {"type": "ephemeral"}
            self._marked_block = last_content[-1]


async def generate_brief(prompt_text: str) -> str:
    client = anthropic.Anthropic()  # reads ANTHROPIC_API_KEY from env

    async with AsyncExitStack() as stack:
        sessions = await connect_mcp_servers(stack)
        local_tools, dispatch = await build_tool_catalog(sessions)
        tools = local_tools + [web_search_tool()]
        if tools:
            tools[-1]["cache_control"] = {"type": "ephemeral"}

        today = datetime.date.today()
        user_message = f"Generate today's brief. Today is {today.isoformat()}, a {today.strftime('%A')}."
        messages = [{"role": "user", "content": [{"type": "text", "text": user_message}]}]
        cache_breakpoint = CacheBreakpoint()
        final_response = None
        for _ in range(MAX_TOOL_ITERATIONS):
            cache_breakpoint.move_to_end(messages)
            response = client.messages.create(
                model=MODEL,
                max_tokens=16000,
                system=[{"type": "text", "text": prompt_text, "cache_control": {"type": "ephemeral"}}],
                messages=messages,
                tools=tools,
            )
            response_content = [block.model_dump(exclude_none=True) for block in response.content]
            messages.append({"role": "assistant", "content": response_content})
            final_response = response
            print(
                f"  turn: stop_reason={response.stop_reason} "
                f"blocks={[b['type'] for b in response_content]} "
                f"usage={response.usage}",
                file=sys.stderr,
            )

            if response.stop_reason == "tool_use":
                tool_results = []
                for block in response_content:
                    if block["type"] != "tool_use":
                        continue
                    if block["name"] not in dispatch:
                        tool_results.append(
                            {
                                "type": "tool_result",
                                "tool_use_id": block["id"],
                                "content": [{"type": "text", "text": f"Unknown tool: {block['name']}"}],
                                "is_error": True,
                            }
                        )
                        continue
                    session, original_name = dispatch[block["name"]]
                    result = await session.call_tool(original_name, block["input"])
                    tool_results.append(
                        {
                            "type": "tool_result",
                            "tool_use_id": block["id"],
                            "content": mcp_result_to_tool_result_content(result),
                        }
                    )
                messages.append({"role": "user", "content": tool_results})
                continue

            if response.stop_reason == "pause_turn":
                # Long-running server-side search; resend as-is to continue.
                continue

            break

        if final_response is None:
            raise RuntimeError("no response from Anthropic API")

        return "".join(block.text for block in final_response.content if block.type == "text")


def get_graph_access_token() -> str:
    """Silently refreshes this script's own Graph OAuth grant (created once
    by graph_login.py) and returns a Mail.Send access token."""
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
    parser.add_argument("--dry-run", action="store_true", help="print the brief instead of emailing it")
    parser.add_argument(
        "--prompt-file",
        default=os.environ.get("PROMPT_FILE", str(CONFIG_DIR / "prompt.md")),
        help="path to the brief instructions",
    )
    args = parser.parse_args()

    load_dotenv(CONFIG_DIR / ".env")

    prompt_path = Path(args.prompt_file)
    if not prompt_path.is_file():
        print(f"ERROR: prompt file not found: {prompt_path}", file=sys.stderr)
        sys.exit(1)
    prompt_text = prompt_path.read_text()

    print(f"[{datetime.datetime.now().isoformat(timespec='seconds')}] Generating brief...")
    brief = asyncio.run(generate_brief(prompt_text))
    print(f"Generated brief: {len(brief)} chars")

    if args.dry_run:
        print("\n----- BRIEF (dry run, not emailed) -----\n")
        print(brief)
        return

    send_email(subject="Daily Brief", body_markdown=brief)
    print("Emailed brief successfully.")


if __name__ == "__main__":
    main()
