#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build-test}"
LISTEN_ADDR="${LISTEN_ADDR:-127.0.0.1:32147}"
FROM_ADDR="${FROM_ADDR:-mailer-daemon@example.org}"
TO_ADDR="${TO_ADDR:-bounces@example.org}"
MAIL_PATH="${MAIL_PATH:-$ROOT_DIR/tests/bounce_notify.eml}"

mkdir -p "$ROOT_DIR/$BUILD_DIR"
cmake -S "$ROOT_DIR" -B "$ROOT_DIR/$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build "$ROOT_DIR/$BUILD_DIR" -j >/dev/null

LOG_PATH="$ROOT_DIR/$BUILD_DIR/mock_server.log"
"$ROOT_DIR/$BUILD_DIR/mock-bouncer-server" --listen "$LISTEN_ADDR" >"$LOG_PATH" 2>&1 &
SERVER_PID=$!
cleanup() {
  kill "$SERVER_PID" >/dev/null 2>&1 || true
}
trap cleanup EXIT

for _ in $(seq 1 50); do
  if grep -q '^LISTENING ' "$LOG_PATH" 2>/dev/null; then
    break
  fi
  sleep 0.1
done

if ! grep -q '^LISTENING ' "$LOG_PATH" 2>/dev/null; then
  echo "mock server did not start" >&2
  cat "$LOG_PATH" >&2 || true
  exit 1
fi

"$ROOT_DIR/$BUILD_DIR/bounce-notify" \
  --server "$LISTEN_ADDR" \
  --from "$FROM_ADDR" \
  --to "$TO_ADDR" \
  < "$MAIL_PATH"

wait "$SERVER_PID"
trap - EXIT

EXPECTED_LEN="$(wc -c < "$MAIL_PATH" | tr -d ' ')"

grep -F "RESULT ok" "$LOG_PATH" >/dev/null
grep -F "\"from\":\"$FROM_ADDR\"" "$LOG_PATH" >/dev/null
grep -F "\"to\":\"$TO_ADDR\"" "$LOG_PATH" >/dev/null
grep -F "body_len=$EXPECTED_LEN" "$LOG_PATH" >/dev/null

echo "client test passed"
echo "log: $LOG_PATH"
