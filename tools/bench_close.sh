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

if ! command -v wrk >/dev/null 2>&1; then
    echo "wrk not found. Install it first: sudo apt install wrk" >&2
    exit 1
fi

echo "== short connection test =="
echo "url=$URL duration=$DURATION connections=$CONNECTIONS threads=$THREADS"
wrk -t"$THREADS" -c"$CONNECTIONS" -d"$DURATION" -H "Connection: close" "$URL"
