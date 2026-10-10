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

## Unified Medical Prediction Entry Point

`medical_screening` routes a single image to the disease-specific model selected
by `--disease`; it does not combine the distinct MRI, radiograph, and DEXA tasks.
Pass the checkpoint trained for that condition and the matching image modality:

```sh
./build/medical_screening predict --disease brain_tumor \
  --model ../Brain_tumour/models/brain_tumour_cnn.weights \
  --image /path/to/brain_mri.png

./build/medical_screening predict --disease osteoarthritis \
  --model ../karnix_spine_edge/models/osteoarthritis_cnn.weights \
  --image /path/to/knee_xray.png

./build/medical_screening predict --disease osteoporosis \
  --model ../karnix_spine_edge/models/dexa_cnn.weights \
  --image /path/to/dexa_scan.png
```

The modality can be provided explicitly with `--modality MRI`, `XRAY`, or
`DEXA`; mismatches are rejected. The DEXA model is not bundled because a
verified labeled DEXA dataset is not available in this repository. The
osteoporosis command will therefore fail until a compatible DEXA checkpoint
has been trained. Checkpoint task provenance cannot be verified from the
current binary weight format, so never substitute a brain-tumor, osteoarthritis,
or other model file. All output is experimental, research-only, and not a
medical diagnosis.

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
