#!/usr/bin/env python3
"""Reads Claude Code CLI's `--output-format json` output from stdin and
prints just the final result text to stdout on success.

Exists because `--output-format text` runs the response through a
terminal-oriented renderer that was observed reflowing long bullet lists
into run-on lines joined by " - " instead of preserving real newlines
(broke markdown list parsing downstream). The JSON `result` field carries
the model's actual raw text untouched by that renderer.

On any failure (invalid/non-JSON stdout -- e.g. an outright auth error
prints plain text, not JSON -- or a valid JSON envelope with is_error
true), prints a short diagnostic to stderr and exits 1, so the caller can
detect the failure via exit code alone rather than string-matching stdout.
"""

import json
import sys

raw = sys.stdin.read()

try:
    data = json.loads(raw)
except json.JSONDecodeError as exc:
    print(f"Claude Code CLI did not return valid JSON ({exc}): {raw[:500]}", file=sys.stderr)
    sys.exit(1)

if data.get("is_error"):
    print(f"Claude Code CLI reported is_error=true: {data.get('result', '(no result field)')}", file=sys.stderr)
    sys.exit(1)

result = data.get("result")
if not result:
    print(f"Claude Code CLI JSON had no non-empty 'result' field: {raw[:500]}", file=sys.stderr)
    sys.exit(1)

sys.stdout.write(result)
