# Stage 3: Lung X-ray Research ML

This folder is isolated from `Karnix_Web` authentication and assessment code. It contains a C++17 baseline classifier for the Kaggle dataset:

`https://www.kaggle.com/datasets/quynhlecl/lung-cancer-x-ray`

The classifier is a reproducible binary logistic-regression baseline over a 32x32 grayscale image feature grid. It is an engineering baseline, not a clinically validated model. It does not diagnose cancer, produce medical advice, or claim accuracy.

## Dataset setup

The direct Kaggle download timed out in this environment. Download the dataset from Kaggle in a browser or with authenticated Kaggle CLI, then place/extract it locally:

```sh
mkdir -p dataset
# Put the downloaded archive at dataset/lung-cancer-x-ray.zip
unzip -q dataset/lung-cancer-x-ray.zip -d dataset/raw
```

The training loader discovers image files recursively by their parent directory. It maps these folder names to classes:

- `normal` or `healthy` -> class 0
- `sick`, `pneumonia`, `lung_cancer`, `cancer`, or `abnormal` -> class 1

Check the extracted folder names before training. If the Kaggle archive uses different labels, rename only the class directories or update the explicit mapping in `lung_xray_ml.cpp`; never silently infer labels from filenames.

Supported on macOS: PNG, JPG/JPEG, BMP, TIFF, and PGM. macOS ImageIO is used for common image formats.

## Build

```sh
cd /Users/sudharsans/Karniiix/Other_Disease
cmake -S . -B build
cmake --build build --config Release
```

## Train, evaluate, predict

```sh
./build/lung_xray_ml train --data dataset/raw --model models/lung_xray.weights
./build/lung_xray_ml evaluate --data dataset/raw --model models/lung_xray.weights
./build/lung_xray_ml predict --model models/lung_xray.weights --image dataset/raw/normal/example.png
```

Prediction output is JSON with `research_only:true` and `diagnosis:false`. The class names are deliberately generic until the dataset's verified label semantics are inspected.

## Model contract

Input: one supported lung X-ray image, decoded to grayscale, resized to a 32x32 feature grid, centered by image mean.

Output:

```json
{
  "disease": "lung_cancer",
  "model": "research_logistic_pixel_baseline",
  "class": "positive_class",
  "probability": 0.5,
  "research_only": true,
  "diagnosis": false
}
```

The current artifact is a baseline only. Before any healthcare use, establish dataset provenance, patient-level split discipline, external validation, calibration, sensitivity/specificity, subgroup evaluation, clinical review, and regulatory/privacy controls.

## Deferred integration

No Fastify prediction route is connected yet. The existing web app remains limited to authentication and background assessment. When the model contract and validation are approved, expose this executable behind a separately authenticated Fastify adapter; do not connect the browser directly to the binary or PostgreSQL.
