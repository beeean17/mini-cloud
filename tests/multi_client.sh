#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN_DIR="$ROOT_DIR/bin"
PORT=${PORT:-9600}
CLIENTS=${CLIENTS:-4}
ROUNDS=${ROUNDS:-3}
STORAGE_DIR=${STORAGE_DIR:-"$ROOT_DIR/storage"}
AUTH_TOKEN=${AUTH_TOKEN:-"mini-cloud-secret"}
MAX_UPLOAD_BYTES=${MAX_UPLOAD_BYTES:-0}

mkdir -p "$STORAGE_DIR"
make -C "$ROOT_DIR" server client >/dev/null

SERVER_LOG=$(mktemp -t mc-stress-server.XXXXXX)
CLIENT_LOG_DIR=$(mktemp -d -t mc-stress-clients.XXXXXX)

cleanup() {
    if [[ -f "$SERVER_LOG" ]]; then
        echo "Server log:" >&2
        cat "$SERVER_LOG" >&2
        rm -f "$SERVER_LOG"
    fi
    if [[ -d "$CLIENT_LOG_DIR" ]]; then
        echo "Client logs:" >&2
        for log in "$CLIENT_LOG_DIR"/*.log; do
            [[ -f "$log" ]] && { echo "=== $log ===" >&2; cat "$log" >&2; }
        done
        rm -rf "$CLIENT_LOG_DIR"
    fi
    if [[ -n "${SERVER_PID:-}" ]]; then
        kill -INT "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}

trap cleanup EXIT

MC_SERVER_TOKEN="$AUTH_TOKEN" MC_MAX_UPLOAD_BYTES="$MAX_UPLOAD_BYTES" \
    "$BIN_DIR/server" "$PORT" "$STORAGE_DIR" >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!
sleep 1

run_client() {
    local idx=$1
    local log_file="$CLIENT_LOG_DIR/client_${idx}.log"
    for ((round=1; round<=ROUNDS; ++round)); do
        local tmp_file
        tmp_file=$(mktemp -t mc-stress-upload.XXXXXX)
        echo "client $idx round $round $(date --iso-8601=ns)" >"$tmp_file"
        local base
        base=$(basename "$tmp_file")
        printf "UPLOAD %s\nDOWNLOAD %s\nLIST\nQUIT\n" "$tmp_file" "$base" |
            MC_CLIENT_TOKEN="$AUTH_TOKEN" "$BIN_DIR/client" 127.0.0.1 "$PORT" >>"$log_file" 2>&1 || return 1
        rm -f "$tmp_file" "$base"
    done
    return 0
}

pids=()
for ((i=1; i<=CLIENTS; ++i)); do
    run_client "$i" &
    pids+=("$!")
    sleep 0.2
done

status=0
for pid in "${pids[@]}"; do
    if ! wait "$pid"; then
        status=1
    fi
done

if [[ $status -ne 0 ]]; then
    echo "Stress test detected failures" >&2
    exit $status
fi

echo "Stress test completed successfully with $CLIENTS clients x $ROUNDS rounds." >&2
exit 0
