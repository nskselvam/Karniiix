#!/usr/bin/env python3
"""Prepare a balanced 1,000-image brain MRI subset from the public source cache."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import shutil
from pathlib import Path


CLASSES = ("glioma_tumor", "meningioma_tumor", "no_tumor", "pituitary_tumor")
SOURCE_URL = "https://github.com/sartajbhuvaji/brain-tumor-classification-dataset"
SOURCE_COMMIT = "3ea9f4eae54c67e5bddbf6f7a73582967a5f5f79"
LICENSE = "MIT (source repository metadata; verify upstream terms before redistribution)"


def image_files(directory: Path) -> list[Path]:
    return sorted(
        path for path in directory.iterdir()
        if path.is_file() and path.suffix.lower() in {".jpg", ".jpeg", ".png"}
    )


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def copy_split(source_root: Path, output_root: Path, split: str, per_class: int) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for class_name in CLASSES:
        source_files = image_files(source_root / split / class_name)
        if len(source_files) < per_class:
            raise RuntimeError(f"{split}/{class_name} has {len(source_files)} images; need {per_class}")
        selected = source_files[:per_class]
        destination = output_root / split / class_name
        destination.mkdir(parents=True, exist_ok=True)
        for index, source in enumerate(selected):
            target = destination / f"{class_name}_{index:04d}{source.suffix.lower()}"
            shutil.copy2(source, target)
            rows.append({
                "split": split,
                "label": class_name,
                "path": str(target.relative_to(output_root)),
                "source_path": str(source.relative_to(source_root)),
                "sha256": sha256(target),
            })
    return rows


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source",
        type=Path,
        default=Path("data/cache/brain-tumor-classification-dataset"),
    )
    parser.add_argument("--output", type=Path, default=Path("data/brain_tumor"))
    parser.add_argument("--train-per-class", type=int, default=200)
    parser.add_argument("--test-per-class", type=int, default=50)
    args = parser.parse_args()

    if not args.source.exists():
        raise SystemExit(f"Source dataset not found: {args.source}")
    args.output.mkdir(parents=True, exist_ok=True)

    rows = copy_split(args.source, args.output, "Training", args.train_per_class)
    rows += copy_split(args.source, args.output, "Testing", args.test_per_class)

    with (args.output / "manifest.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=("split", "label", "path", "source_path", "sha256"))
        writer.writeheader()
        writer.writerows(rows)

    metadata = {
        "dataset": "Brain Tumor Classification MRI",
        "source_url": SOURCE_URL,
        "source_commit": SOURCE_COMMIT,
        "license_note": LICENSE,
        "classes": list(CLASSES),
        "counts": {
            "total": len(rows),
            "training": args.train_per_class * len(CLASSES),
            "testing": args.test_per_class * len(CLASSES),
            "training_per_class": args.train_per_class,
            "testing_per_class": args.test_per_class,
        },
        "warning": "Research dataset only. Not for diagnosis or clinical deployment.",
    }
    (args.output / "DATASET_METADATA.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()