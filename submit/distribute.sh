#!/bin/bash
# Copy trace-submit (and trace-stop) to every benchmark overlay directory
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OVERLAY_DIR="${SCRIPT_DIR}/../build/overlay/intspeed/test"

if [ ! -f "${SCRIPT_DIR}/trace-submit" ]; then
  echo "error: trace-submit not found in ${SCRIPT_DIR}, run 'make trace-submit' first" >&2
  exit 1
fi

for bench in "${OVERLAY_DIR}"/*/; do
  cp "${SCRIPT_DIR}/trace-submit" "${bench}"
  if [ -f "${SCRIPT_DIR}/trace-stop" ]; then
    cp "${SCRIPT_DIR}/trace-stop" "${bench}"
  fi
  echo "copied to $(basename "${bench}")"
done
