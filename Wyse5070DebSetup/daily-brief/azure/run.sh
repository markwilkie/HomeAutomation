#!/usr/bin/env bash
# The daily brief (cron 6:30am). Kept as the crontab's entry point; the work
# is in run.py, shared with the events email. Usage: ./run.sh [--dry-run]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
exec venv/bin/python run.py brief "$@"
