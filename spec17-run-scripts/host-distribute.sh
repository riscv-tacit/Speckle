#!/bin/bash
# Copy intspeed.sh to the overlay directory
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OVERLAY_DIR="${SCRIPT_DIR}/../build/overlay/intspeed/test"

if [ ! -f "${SCRIPT_DIR}/intspeed.sh" ]; then
  echo "error: intspeed.sh not found in ${SCRIPT_DIR}" >&2
  exit 1
fi

if [ ! -d "${OVERLAY_DIR}" ]; then
  echo "error: overlay directory not found at ${OVERLAY_DIR}" >&2
  exit 1
fi

cp "${SCRIPT_DIR}/intspeed.sh" "${OVERLAY_DIR}/intspeed.sh"
echo "copied intspeed.sh to ${OVERLAY_DIR}"
