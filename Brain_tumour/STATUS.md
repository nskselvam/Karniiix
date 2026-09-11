# Brain_tumour Status

## Project Boundary

This project is isolated in `Brain_tumour`. It does not modify or include `karnix_spine_edge`.
It uses the shared C++ components under `../neural_network`. The mathematical headers remain available from the workspace, but the CNN path does not require a separate mathematical runtime.

## Commands

```bash
./setup.sh
```

Configuration is environment-based:

```bash
EPOCHS=12 BATCH_SIZE=16 LEARNING_RATE=0.0005 ./setup.sh
```

`MIN_TRAIN_SECONDS` defaults to `30`, so every training run lasts at least 30 seconds.

Use `./setup.sh --skip-train` to evaluate the existing model, or
`./setup.sh --skip-evaluate` to train without running held-out evaluation.

## Dataset

The balanced subset contains 1,000 images:

- 800 training images
- 200 testing images
- 200 training and 50 testing images per class

Classes:

- glioma tumor
- meningioma tumor
- pituitary tumor
- no tumor

The manifest and provenance files are under `data/brain_tumor`.

## Model

The model uses the shared Karnix CNN pipeline with a separate four-class output head. Checkpoints are written under `models/` and logs under `models/`.

The current measured checkpoint was trained entirely in C++ for 12 epochs. Its held-out result is recorded in the evaluation log and must be treated as research-only. Training confidence is not a substitute for held-out accuracy or clinical validation.
