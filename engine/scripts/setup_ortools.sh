#!/bin/bash
set -euo pipefail

VERSION="v9.12"
ASSET="or-tools_amd64_ubuntu-22.04_cpp_v9.12.4544.tar.gz"
URL="https://github.com/google/or-tools/releases/download/${VERSION}/${ASSET}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENGINE_DIR="$(dirname "$SCRIPT_DIR")"
DEST_DIR="$ENGINE_DIR/third_party/ortools"

if [ -d "$DEST_DIR" ] && [ -f "$DEST_DIR/README.md" ]; then
    echo "OR-Tools already present at $DEST_DIR — skipping download."
    exit 0
fi

if ! command -v mold >/dev/null 2>&1; then
    echo "WARNING: mold linker not found. Install it before building with -DWITH_ORTOOLS=ON."
fi

mkdir -p "$DEST_DIR"
TMP_TAR="$(mktemp --suffix=.tar.gz)"
trap 'rm -f "$TMP_TAR"' EXIT

echo "Downloading OR-Tools ${VERSION} (pinned, Ubuntu 22.04 C++ build)..."
curl -fL --progress-bar "$URL" -o "$TMP_TAR"

echo "Extracting to $DEST_DIR ..."
tar -xzf "$TMP_TAR" -C "$DEST_DIR" --strip-components=1

echo "Done. Build with: cmake -S engine -B engine/build -DWITH_ORTOOLS=ON"