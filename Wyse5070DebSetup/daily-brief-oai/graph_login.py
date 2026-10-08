#!/usr/bin/env python3
"""One-time login: daily-brief-oai's own Microsoft Graph OAuth grant
(Mail.Send to email the brief, Mail.Read to scan Inbox/Clutter -- the job
the claude.ai Microsoft 365 connector does for daily-brief-cc). Same Azure
App Registration as the other daily-brief projects, but its own token cache,
so independently-refreshing consumers never race on one refresh token.

Device code flow: prints a URL and code to complete in any browser.
Re-run only if the refresh token is revoked or expires from inactivity.
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
SCOPES = ["Mail.Send", "Mail.Read"]
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
