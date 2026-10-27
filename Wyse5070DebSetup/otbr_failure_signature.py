#!/usr/bin/env python3
"""Reads otbr container log lines (`docker logs -t otbr`) on stdin and prints
one line per otbr-agent exit: "<exit time>\t<signature>".

The signature is what kind of failure it was, stable across occurrences so
the weekly health check can tell "same failure as always" from "something
new": the exit code, the reason from otbr-agent's last critical ([C]) log
line, and whether the RCP reported a serial framing error first, e.g.
    exit=6 RadioSpinelNoResponse (after RCP framing error)

Exits by SIGTERM (a requested stop: docker restart/stop, host shutdown) are
not failures and are skipped.

Used by setup-otbr-watchdog.sh, which records each exit to
/mnt/data/appdata/otbr-log-archive/failures.log as it happens.

Usage:
  docker logs -t --tail 20000 otbr 2>&1 | otbr_failure_signature.py [--before <RFC3339 time>]
"""

import argparse
import re
import sys

EXIT_RE = re.compile(r"otbr-agent exited with code (\d+) \(by signal (\d+)\)")
CRIT_RE = re.compile(r"\[C\] [\w-]+: (.*)")
# Platform's summary line, e.g. "HandleRcpTimeout() at radio_spinel.cpp:2037: RadioSpinelNoResponse"
REASON_RE = re.compile(r"\w+\(\) at [\w.]+:\d+: (\w+)")
SIGTERM = 15


def signatures(lines, before=None):
    crit, framing = [], False
    for line in lines:
        timestamp, _, text = line.rstrip("\n").partition(" ")
        if before and timestamp >= before:
            break
        if "Starting otbr-agent" in text:
            crit, framing = [], False
        elif "Framing error" in text:
            framing = True
        elif match := CRIT_RE.search(text):
            crit.append(match.group(1).strip())
        elif match := EXIT_RE.search(text):
            code, signal = int(match.group(1)), int(match.group(2))
            if signal != SIGTERM:
                if signal:
                    reason = f"killed by signal {signal}"
                else:
                    reasons = [m.group(1) for c in crit if (m := REASON_RE.search(c))]
                    reason = reasons[-1] if reasons else (crit[-1][:80] if crit else "no critical log line")
                yield timestamp[:19] + "Z", f"exit={code} {reason}" + (" (after RCP framing error)" if framing else "")
            crit, framing = [], False


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--before", help="ignore log lines at or after this time (the container's new StartedAt)")
    args = parser.parse_args()
    for timestamp, signature in signatures(sys.stdin, args.before):
        print(f"{timestamp}\t{signature}")


if __name__ == "__main__":
    main()
