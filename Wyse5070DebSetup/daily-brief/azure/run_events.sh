#!/usr/bin/env bash
# The Weekend & Ahead events email (cron Thu 5:00am full, Sat 5:00am refresh; both before the 6:30 brief).
# Kept as the crontab's entry point; the work is in run.py, shared with the
# daily brief. Usage: ./run_events.sh [full|refresh] [--dry-run]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
exec venv/bin/python run.py events "$@"
