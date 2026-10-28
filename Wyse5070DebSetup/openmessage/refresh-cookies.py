#!/usr/bin/env python3
"""OPENMESSAGE_COOKIE_REFRESH_SCRIPT for the openmessage-chromium profile.

Runs upstream's refresh-google-session-cookies-linux.py unchanged except for
where the cookie key comes from: upstream reads it from KWallet, but the
container Chromium has no keyring, so it encrypts with Chromium's fixed
basic-store password ("peanuts", forced with --password-store=basic).
OpenMessage calls this with --quiet --no-backup when Google rejects the
session; profile and session paths come from OPENMESSAGE_CHROME_PROFILE and
OPENMESSAGE_SESSION_PATH.

Upstream also expects the cookie DB at <profile>/Network/Cookies; this
Chromium keeps it at <profile>/Cookies, so point upstream at a temporary
profile whose Network/Cookies links to it.
"""
import importlib.util
import os
import sys
import tempfile
from pathlib import Path

profile = Path(os.environ["OPENMESSAGE_CHROME_PROFILE"])
if not (profile / "Network" / "Cookies").exists() and (profile / "Cookies").exists():
    shim = Path(tempfile.mkdtemp())
    (shim / "Network").mkdir()
    (shim / "Network" / "Cookies").symlink_to(profile / "Cookies")
    os.environ["OPENMESSAGE_CHROME_PROFILE"] = str(shim)

spec = importlib.util.spec_from_file_location(
    "refresh", "/opt/openmessage/refresh-google-session-cookies-linux.py"
)
refresh = importlib.util.module_from_spec(spec)
spec.loader.exec_module(refresh)
refresh.chrome_safe_storage_secret = lambda args: b"peanuts"
sys.exit(refresh.main())
