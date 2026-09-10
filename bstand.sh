#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 [Debug|Release|RelWithDebInfo|MinSizeRel]"
    echo "Build and open the macOS standalone app with Xcode (default: Debug)."
}

if [[ $# -gt 1 ]]; then
    usage >&2
    exit 2
fi

CONFIG="${1:-Debug}"
case "${CONFIG}" in
    -h|--help) usage; exit 0 ;;
    Debug|Release|RelWithDebInfo|MinSizeRel) ;;
    *) usage >&2; exit 2 ;;
esac

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "Error: this script requires macOS and Xcode." >&2
    exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo "Error: cmake was not found in PATH (version 3.24 or newer required)." >&2
    exit 1
fi

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
APP_PATH="${BUILD_DIR}/ulichperc_artefacts/${CONFIG}/Standalone/ulichpercs.app"

echo "Configuring Xcode project..."
cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" -G Xcode

echo "Building ${CONFIG} standalone app..."
cmake --build "${BUILD_DIR}" --config "${CONFIG}" --target ulichperc_Standalone --parallel

if [[ ! -d "${APP_PATH}" ]]; then
    echo "Error: expected standalone app was not found at: ${APP_PATH}" >&2
    exit 1
fi

echo "Built standalone app:"
echo "  ${APP_PATH}"

echo "Opening standalone app..."
open -n "${APP_PATH}"
