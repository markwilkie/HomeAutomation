#!/bin/sh
# Host unit tests for src/control_logic.c. Uses a local gcc if there is one,
# otherwise the stock gcc Docker image (e.g. on wyse).
set -e
cd "$(dirname "$0")"
if command -v gcc >/dev/null 2>&1; then
    make clean test
else
    docker run --rm -v "$(cd ../.. && pwd)":/src -w /src/test/host gcc:14 make clean test
fi
