#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$ROOT/dataset"
ARCHIVE="$ROOT/dataset/lung-cancer-x-ray.zip"
URL="https://www.kaggle.com/api/v1/datasets/download/quynhlecl/lung-cancer-x-ray"

if command -v kaggle >/dev/null 2>&1; then
  kaggle datasets download -d quynhlecl/lung-cancer-x-ray -p "$ROOT/dataset" -o
else
  echo "Kaggle CLI was not found; trying the public download endpoint."
  curl --fail --location --retry 3 --connect-timeout 15 --max-time 300 "$URL" --output "$ARCHIVE"
fi

[[ -s "$ARCHIVE" ]] || { echo "Dataset archive was not downloaded: $ARCHIVE" >&2; exit 1; }
file "$ARCHIVE"
echo "Archive ready at $ARCHIVE"
