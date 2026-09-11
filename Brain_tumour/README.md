# Karnix Brain Tumour C++ Engine

A disease-specific four-class C++ model built on Karnix shared infrastructure.

## Architecture

The common pipeline is preserved:

- C++14
- `neural_network/utils/Tensor.hpp`
- `neural_network/layers/ConvolutionLayer.hpp`
- `neural_network/layers/ActivationFunctions.hpp`
- `neural_network/layers/PoolingLayers.hpp`
- `neural_network/layers/FullyConnectedLayer.hpp`
- grayscale preprocessing
- bilinear resize to 64 x 64
- deterministic manifest-driven training
- SGD and cross-entropy
- binary checkpoint serialization
- held-out evaluation and confusion matrix

Only the disease-specific model head changes from binary classification to four classes:

1. `glioma_tumor`
2. `meningioma_tumor`
3. `no_tumor`
4. `pituitary_tumor`

This project does not include or depend on `karnix_spine_edge`. Image decoding is local C++: PPM/PGM/BMP are decoded directly and macOS JPEG/PNG/TIFF decoding uses ImageIO/CoreGraphics. No Python is required for model training or inference.

## Build

```bash
cd /Users/sudharsans/Documents/Karniiix/Brain_tumour
cmake -S . -B build
cmake --build build --target brain_tumour_cli
```

## Train

```bash
./build/brain_tumour_cli \
  --train \
  --manifest data/brain_tumor/manifest.csv \
  --epochs 12 \
  --batch-size 16 \
  --learning-rate 0.0005 \
  --min-train-seconds 30 \
  --checkpoint-dir models/checkpoints \
  --verbose \
  --model models/brain_tumour_cnn.weights
```

## Evaluate

```bash
./build/brain_tumour_cli \
  --evaluate \
  --manifest data/brain_tumor/manifest.csv \
  --model models/brain_tumour_cnn.weights \
  --output data/brain_tumor/predictions.json \
  --verbose
```

## Native CNN Visualization

The brain project has a separate native C++ UI from the spine visualizer. It is
organized as a scan-review console so the model behavior is readable at a glance:

- large current MRI preview
- four class probability bars
- selected top-1 decision and inference time
- visible `INPUT -> CONV BLOCKS -> POOLING -> DENSE HEAD -> SOFTMAX` timeline
- scan navigation with left/right arrow keys
- screening disclaimer in the interface

Build and launch it with:

```bash
cmake --build build --target brain_tumour_visualizer
./build/brain_tumour_visualizer \
  data/brain_tumor/Testing \
  models/brain_tumour_cnn.weights
```

The visualizer is implemented in [brain_tumour_visualizer.cpp](brain_tumour_visualizer.cpp) and uses the real `BrainTumourCNN` output for every displayed probability.

Verbose training output includes every image, label, prediction, confidence,
loss, correctness flag, image time, batch loss/accuracy/timing, epoch loss,
epoch accuracy, throughput, checkpoint path, and total training duration.
Verbose evaluation output includes every held-out image, prediction,
confidence, correctness flag, image time, confusion matrix, test duration,
throughput, and average milliseconds per image.

Training enforces a minimum runtime of 30 seconds by default, including short
smoke runs. Change it with `--min-train-seconds N` or the `MIN_TRAIN_SECONDS`
environment variable used by `setup.sh`.

The shell workflow stores complete logs in:

```text
models/brain_tumour_training.log
models/brain_tumour_evaluation.log
models/checkpoints/epoch_N.weights
data/brain_tumor/predictions.json
```

## Current measured result

The 12-epoch C++ run reached 98.38% training accuracy, but the held-out 200-image test result was:

```text
Accuracy: 44.00%
Mean top-1 confidence: 89.07%
```

This is an overfit research checkpoint. The high confidence must not be interpreted as 89% diagnostic accuracy. The model is not suitable for clinical diagnosis. More independent data, stronger augmentation, patient-level split validation, and a larger validated architecture are required before clinical use.

## Dataset

`data/brain_tumor` contains 1,000 balanced images: 800 training and 200 testing, with 200/50 images per class. `manifest.csv` contains source paths and SHA-256 hashes. `DATASET_METADATA.json` records source provenance and the research-only warning.
