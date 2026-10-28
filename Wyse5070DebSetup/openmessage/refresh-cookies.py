#!/usr/bin/env python3
"""OPENMESSAGE_COOKIE_REFRESH_SCRIPT for the openmessage-chromium profile.

Runs upstream's refresh-google-session-cookies-linux.py unchanged except for
where the cookie key comes from: upstream reads it from KWallet, but the
container Chromium has no keyring, so it encrypts with Chromium's fixed
basic-store password ("peanuts", forced with --password-store=basic).
OpenMessage calls this with --quiet --no-backup when Google rejects the
session; profile and session paths come from OPENMESSAGE_CHROME_PROFILE and
OPENMESSAGE_SESSION_PATH.
"""
import importlib.util
import sys

spec = importlib.util.spec_from_file_location(
    "refresh", "/opt/openmessage/refresh-google-session-cookies-linux.py"
)
refresh = importlib.util.module_from_spec(spec)
spec.loader.exec_module(refresh)
refresh.chrome_safe_storage_secret = lambda args: b"peanuts"
sys.exit(refresh.main())
