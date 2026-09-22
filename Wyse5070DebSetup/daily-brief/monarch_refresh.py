#!/usr/bin/env python3
"""Kicks off a Monarch Money account refresh via the local MCP gateway.

Run at 6am, 30 minutes before brief.py's 6:30am run, so Monarch's sync with
the underlying financial institutions (which takes a few minutes, not
instant) has finished by the time the brief reads balances/transactions.
"""

import asyncio
import os
import sys
from pathlib import Path

from dotenv import load_dotenv
from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client

SCRIPT_DIR = Path(__file__).resolve().parent
CONFIG_DIR = SCRIPT_DIR.parent / "config"
load_dotenv(CONFIG_DIR / ".env")

MONARCH_MCP_URL = os.environ.get("MONARCH_MCP_URL", "http://127.0.0.1:8602/mcp")


async def main() -> None:
    async with streamable_http_client(MONARCH_MCP_URL) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()
            result = await session.call_tool("refresh_accounts", {})
            text = "\n".join(getattr(block, "text", str(block)) for block in result.content)
            print(text)
            if result.is_error:
                sys.exit(1)


if __name__ == "__main__":
    asyncio.run(main())
