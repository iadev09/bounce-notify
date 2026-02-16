#!/usr/bin/env bash
set -euo pipefail

NAME="bounce-notify"
INSTALL_DIR="/usr/local/bin"
HOST=""
BINARY=""
LOCAL_HOSTNAME="$(hostname -s)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="build-release"
BUILD_ENABLED=1

usage() {
  cat <<'EOF'
Usage:
  ./scripts/deploy.sh [--host [user@]server] [--install-dir DIR] [--no-build]
  ./scripts/deploy.sh [--binary PATH] [--host [user@]server] [--install-dir DIR] [--no-build]

Examples:
  ./scripts/deploy.sh
  ./scripts/deploy.sh --host root@mail01
  ./scripts/deploy.sh --host
  ./scripts/deploy.sh --binary ./build-release/bounce-notify --host mail01
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --binary)
      if [[ $# -lt 2 || "${2:-}" == --* ]]; then
        echo "--binary requires a path value" >&2
        exit 1
      fi
      BINARY="${2:-}"
      shift 2
      ;;
    --host)
      if [[ $# -ge 2 && "${2:-}" != --* ]]; then
        HOST="$2"
        shift 2
      else
        HOST="${LOCAL_HOSTNAME}"
        shift 1
      fi
      ;;
    --install-dir)
      if [[ $# -lt 2 || "${2:-}" == --* ]]; then
        echo "--install-dir requires a directory value" >&2
        exit 1
      fi
      INSTALL_DIR="${2:-}"
      shift 2
      ;;
    --no-build)
      BUILD_ENABLED=0
      shift 1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ -z "$BINARY" ]]; then
  if [[ "$BUILD_ENABLED" -eq 1 ]]; then
    echo "Building ${NAME} (Release) ..."
    cmake -S "$PROJECT_ROOT" -B "$PROJECT_ROOT/$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$PROJECT_ROOT/$BUILD_DIR" -j
    BINARY="$PROJECT_ROOT/$BUILD_DIR/$NAME"
  else
    for candidate in \
      "$PROJECT_ROOT/build/${NAME}" \
      "$PROJECT_ROOT/build-release/${NAME}" \
      "$PROJECT_ROOT/build/Release/${NAME}"
    do
      if [[ -x "$candidate" ]]; then
        BINARY="$candidate"
        break
      fi
    done
  fi
else
  if [[ "$BINARY" != /* ]]; then
    BINARY="$PROJECT_ROOT/$BINARY"
  fi
fi

if [[ -z "$BINARY" ]]; then
  echo "Binary not found. Run without --no-build or pass --binary PATH." >&2
  exit 1
fi

if [[ ! -f "$BINARY" ]]; then
  echo "Binary path does not exist: $BINARY" >&2
  exit 1
fi

if [[ -z "$HOST" ]]; then
  echo "Installing locally with sudo: ${INSTALL_DIR}/${NAME}"
  sudo install -m 0755 "$BINARY" "${INSTALL_DIR}/${NAME}"
  echo "Done: ${INSTALL_DIR}/${NAME}"
else
  TMP_PATH="/tmp/${NAME}.$$"
  echo "Copying to ${HOST}:${TMP_PATH}"
  scp "$BINARY" "${HOST}:${TMP_PATH}"
  echo "Installing on ${HOST} with sudo: ${INSTALL_DIR}/${NAME}"
  ssh "$HOST" "sudo install -m 0755 '${TMP_PATH}' '${INSTALL_DIR}/${NAME}' && rm -f '${TMP_PATH}'"
  echo "Done: ${HOST}:${INSTALL_DIR}/${NAME}"
fi
