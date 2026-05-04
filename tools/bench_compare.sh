#!/usr/bin/env bash
set -euo pipefail

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    echo "Usage: $0 [url] [duration] [connections] [threads]"
    echo "Example: $0 http://127.0.0.1:9006/login 10s 100 4"
    exit 0
fi

URL="${1:-http://127.0.0.1:9006/login}"
DURATION="${2:-10s}"
CONNECTIONS="${3:-100}"
THREADS="${4:-4}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"$SCRIPT_DIR/bench_keepalive.sh" "$URL" "$DURATION" "$CONNECTIONS" "$THREADS"
echo
"$SCRIPT_DIR/bench_close.sh" "$URL" "$DURATION" "$CONNECTIONS" "$THREADS"
