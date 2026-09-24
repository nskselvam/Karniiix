#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$ROOT/dataset/raw" "$ROOT/models"

if [[ ! -f "$ROOT/dataset/lung-cancer-x-ray.zip" ]]; then
  echo "Place the Kaggle archive at: $ROOT/dataset/lung-cancer-x-ray.zip"
  echo "Dataset: https://www.kaggle.com/datasets/quynhlecl/lung-cancer-x-ray"
  exit 1
fi

unzip -q -o "$ROOT/dataset/lung-cancer-x-ray.zip" -d "$ROOT/dataset/raw"
echo "Extracted dataset to $ROOT/dataset/raw"
find "$ROOT/dataset/raw" -type d -maxdepth 3 | sort
