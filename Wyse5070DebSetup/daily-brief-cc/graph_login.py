#!/usr/bin/env python3
"""One-time login: creates daily-brief-cc's own independent Microsoft Graph
OAuth grant (Mail.Send only) against the same Azure App Registration the
Microsoft To Do MCP gateway (and the API-key-based daily-brief project) use
-- kept separate so two independently-refreshing consumers never race on
the same token file. Run this once (over SSH is fine -- it uses the device
code flow, so nothing needs to listen on this host); it prints a URL and
code for you to complete in any browser, on any device.

Re-run only if the refresh token is revoked or expires from long inactivity
(send_mail.py refreshes it automatically on every run otherwise).
"""

import os
import sys
from pathlib import Path

import msal
from dotenv import load_dotenv

SCRIPT_DIR = Path(__file__).resolve().parent
CONFIG_DIR = SCRIPT_DIR.parent / "config"
load_dotenv(CONFIG_DIR / ".env")

CLIENT_ID = os.environ.get("MS_CLIENT_ID", "d0f1f47d-77ee-494d-a63b-ee3afeb2fad1")
TENANT_ID = os.environ.get("MS_TENANT_ID", "859244b7-6976-450f-92d4-352c7abd6601")
SCOPES = ["Mail.Send"]
TOKEN_CACHE_PATH = Path(os.environ.get("MS_TOKEN_CACHE", str(CONFIG_DIR / "graph_token_cache.json")))

cache = msal.SerializableTokenCache()
if TOKEN_CACHE_PATH.is_file():
    cache.deserialize(TOKEN_CACHE_PATH.read_text())

app = msal.PublicClientApplication(
    CLIENT_ID,
    authority=f"https://login.microsoftonline.com/{TENANT_ID}",
    token_cache=cache,
)

flow = app.initiate_device_flow(scopes=SCOPES)
if "user_code" not in flow:
    print(f"ERROR: failed to start device flow: {flow}", file=sys.stderr)
    sys.exit(1)

print(flow["message"], flush=True)
result = app.acquire_token_by_device_flow(flow)  # blocks until you complete the login

if "access_token" not in result:
    print(f"ERROR: login failed: {result.get('error_description', result)}", file=sys.stderr)
    sys.exit(1)

TOKEN_CACHE_PATH.write_text(cache.serialize())
os.chmod(TOKEN_CACHE_PATH, 0o600)
print(f"Success. Token cache saved to {TOKEN_CACHE_PATH}")
