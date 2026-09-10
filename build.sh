#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
SOURCE_VST="${BUILD_DIR}/ulichperc_artefacts/Debug/VST3/ulichpercs.vst3"
SOURCE_AU="${BUILD_DIR}/ulichperc_artefacts/Debug/AU/ulichpercs.component"
INSTALL_DIR="/Library/Audio/Plug-Ins/VST3"
DEST_VST="${INSTALL_DIR}/$(basename "${SOURCE_VST}")"

cd "${PROJECT_DIR}"

echo "Configuring Xcode project..."
cmake -B "${BUILD_DIR}" -G Xcode

echo "Building Debug VST3 and AU..."
cmake --build "${BUILD_DIR}" --config Debug \
    --target ulichperc_VST3 ulichperc_AU \
    --parallel

if [[ ! -e "${SOURCE_VST}" ]]; then
    echo "Error: expected VST3 bundle was not found at:" >&2
    echo "  ${SOURCE_VST}" >&2
    exit 1
fi

if [[ ! -e "${SOURCE_AU}" ]]; then
    echo "Error: expected AU bundle was not found at:" >&2
    echo "  ${SOURCE_AU}" >&2
    exit 1
fi

if [[ "${DEST_VST}" != "${INSTALL_DIR}/"* || "${DEST_VST}" != *.vst3 ]]; then
    echo "Error: refusing to install to unexpected destination:" >&2
    echo "  ${DEST_VST}" >&2
    exit 1
fi

echo "Installing $(basename "${SOURCE_VST}") to ${INSTALL_DIR}..."
sudo mkdir -p "${INSTALL_DIR}"
sudo rm -rf -- "${DEST_VST}"
sudo cp -R "${SOURCE_VST}" "${INSTALL_DIR}/"

echo "Installed:"
echo "  ${DEST_VST}"

echo "Built AU (not installed):"
echo "  ${SOURCE_AU}"
