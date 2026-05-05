#!/usr/bin/env bash
set -u

cd "$(dirname "$0")/.."

PORT=9006
URL="http://localhost:${PORT}"
WRK_ARGS=(-t2 -c1000 -d20s "$URL")
TEST_LOG="test.log"
RUNNER_LOG="bench_runner.log"

wait_for_port() {
    for _ in $(seq 1 50); do
        if curl -fsS --max-time 1 "$URL" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.1
    done
    return 1
}

run_case() {
    local m="$1"
    local t="$2"
    local s="$3"
    local n="$4"
    local label="./main -c 1 -T ${t} -M ${m} -S ${s} -N ${n}"

    {
        echo "================================================================"
        echo "$label"
        echo "wrk ${WRK_ARGS[*]}"
        echo "----------------------------------------------------------------"
    } >> "$TEST_LOG"

    pkill -f "./main .* -p ${PORT}" >/dev/null 2>&1 || true
    pkill -f "./main .*${PORT}" >/dev/null 2>&1 || true
    ./main -p "$PORT" -c 1 -T "$t" -M "$m" -S "$s" -N "$n" >> "$RUNNER_LOG" 2>&1 &
    local server_pid=$!

    if ! wait_for_port; then
        echo "server failed to become ready" >> "$TEST_LOG"
        kill "$server_pid" >/dev/null 2>&1 || true
        wait "$server_pid" >/dev/null 2>&1 || true
        return
    fi

    wrk "${WRK_ARGS[@]}" >> "$TEST_LOG" 2>&1
    echo >> "$TEST_LOG"

    kill "$server_pid" >/dev/null 2>&1 || true
    wait "$server_pid" >/dev/null 2>&1 || true
    pkill -P "$server_pid" >/dev/null 2>&1 || true
    sleep 1
}

: > "$TEST_LOG"
: > "$RUNNER_LOG"

run_case 0 3 0 0

for t in 0 2 3; do
    for s in 0 2; do
        for n in 1 2 4 6; do
            run_case 1 "$t" "$s" "$n"
        done
    done
done

pkill -f "./main .* -p ${PORT}" >/dev/null 2>&1 || true
pkill -f "./main .*${PORT}" >/dev/null 2>&1 || true
