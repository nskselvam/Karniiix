#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
DATA_DIR="$SCRIPT_DIR/data/brain_tumor"
MANIFEST="$DATA_DIR/manifest.csv"
MODEL_DIR="$SCRIPT_DIR/models"
MODEL="$MODEL_DIR/brain_tumour_cnn.weights"
CHECKPOINT_DIR="$MODEL_DIR/checkpoints"
TRAIN_LOG="$MODEL_DIR/brain_tumour_training.log"
EVAL_LOG="$MODEL_DIR/brain_tumour_evaluation.log"
PREDICTIONS="$DATA_DIR/predictions.json"
EPOCHS="${EPOCHS:-12}"
BATCH_SIZE="${BATCH_SIZE:-16}"
LEARNING_RATE="${LEARNING_RATE:-0.0005}"
MIN_TRAIN_SECONDS="${MIN_TRAIN_SECONDS:-30}"
SKIP_TRAIN=0
SKIP_EVAL=0
RUN_VISUALIZER=1

for argument in "$@"; do
    case "$argument" in
        --skip-train) SKIP_TRAIN=1 ;;
        --skip-evaluate) SKIP_EVAL=1 ;;
        --no-visualizer) RUN_VISUALIZER=0 ;;
        --help)
            printf 'Usage: ./setup.sh [--skip-train] [--skip-evaluate] [--no-visualizer]\n'
            printf 'Environment: EPOCHS=12 BATCH_SIZE=16 LEARNING_RATE=0.0005 MIN_TRAIN_SECONDS=30\n'
            exit 0
            ;;
        *) printf 'Unknown option: %s\n' "$argument" >&2; exit 2 ;;
    esac
done

step() { printf '\n\033[1;36m[%s]\033[0m %s\n' "$1" "$2"; }
pass() { printf '\033[1;32m[PASS]\033[0m %s\n' "$1"; }
fail() { printf '\033[1;31m[FAIL]\033[0m %s\n' "$1" >&2; exit 1; }

cd "$SCRIPT_DIR"

step "1/6" "Checking C++ prerequisites"
for command_name in cmake clang++ shasum; do
    command -v "$command_name" >/dev/null 2>&1 || fail "$command_name is required"
    pass "$command_name: $(command -v "$command_name")"
done

step "2/6" "Validating the balanced brain MRI dataset"
[[ -f "$MANIFEST" ]] || fail "Missing manifest: $MANIFEST"
manifest_rows=$(($(wc -l < "$MANIFEST") - 1))
[[ "$manifest_rows" -eq 1000 ]] || fail "Expected 1000 manifest rows, found $manifest_rows"
for label in glioma_tumor meningioma_tumor no_tumor pituitary_tumor; do
    training_count=$(awk -F, -v label="$label" '$1 == "Training" && $2 == label { count++ } END { print count + 0 }' "$MANIFEST")
    testing_count=$(awk -F, -v label="$label" '$1 == "Testing" && $2 == label { count++ } END { print count + 0 }' "$MANIFEST")
    [[ "$training_count" -eq 200 && "$testing_count" -eq 50 ]] || fail "Unexpected class balance for $label"
    pass "$label: 200 training / 50 testing"
done
[[ -f "$DATA_DIR/DATASET_METADATA.json" ]] || fail "Missing dataset provenance metadata"
pass "Dataset manifest and provenance validated"

step "3/6" "Building the isolated C++ engine"
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" --target brain_tumour_cli brain_tumour_visualizer
[[ -x "$BUILD_DIR/brain_tumour_cli" ]] || fail "brain_tumour_cli was not built"
[[ -x "$BUILD_DIR/brain_tumour_visualizer" ]] || fail "brain_tumour_visualizer was not built"
pass "Built $BUILD_DIR/brain_tumour_cli"
pass "Built $BUILD_DIR/brain_tumour_visualizer"

mkdir -p "$MODEL_DIR" "$CHECKPOINT_DIR"

if [[ "$SKIP_TRAIN" -eq 0 ]]; then
    step "4/6" "Training the four-class brain-tumor CNN"
    rm -f "$MODEL"
    rm -rf "$CHECKPOINT_DIR"
    mkdir -p "$CHECKPOINT_DIR"
    "$BUILD_DIR/brain_tumour_cli" \
        --train \
        --manifest "$MANIFEST" \
        --epochs "$EPOCHS" \
        --batch-size "$BATCH_SIZE" \
        --learning-rate "$LEARNING_RATE" \
        --min-train-seconds "$MIN_TRAIN_SECONDS" \
        --checkpoint-dir "$CHECKPOINT_DIR" \
        --model "$MODEL" --verbose | tee "$TRAIN_LOG"
    [[ -s "$MODEL" ]] || fail "Training did not create $MODEL"
    shasum -a 256 "$MODEL" | tee "$MODEL_DIR/brain_tumour_cnn.weights.sha256"
    pass "Model saved: $MODEL"
else
    pass "Training skipped"
fi

if [[ "$SKIP_EVAL" -eq 0 ]]; then
    step "5/6" "Evaluating the held-out brain MRI set"
    [[ -s "$MODEL" ]] || fail "Cannot evaluate without $MODEL"
    "$BUILD_DIR/brain_tumour_cli" \
        --evaluate \
        --manifest "$MANIFEST" \
        --model "$MODEL" \
        --output "$PREDICTIONS" --verbose | tee "$EVAL_LOG"
    [[ -s "$PREDICTIONS" ]] || fail "Evaluation did not create $PREDICTIONS"
    pass "Predictions saved: $PREDICTIONS"
else
    pass "Evaluation skipped"
fi

step "6/6" "Writing setup status"
printf 'Brain_tumour setup complete.\n'
printf 'Model: %s\n' "$MODEL"
printf 'Manifest: %s (%s rows)\n' "$MANIFEST" "$manifest_rows"
printf 'No Python runtime or karnix_spine_edge dependency is used by this project.\n'
printf 'Training and evaluation are both controlled by this setup.sh.\n'

if [[ "$RUN_VISUALIZER" -eq 1 ]]; then
    [[ -s "$MODEL" ]] || fail "Cannot launch visualizer without model: $MODEL"
    step "VISUALIZER" "Launching native Brain MRI CNN visualization"
    "$BUILD_DIR/brain_tumour_visualizer" "$DATA_DIR/Testing" "$MODEL" \
        > "$MODEL_DIR/brain_tumour_visualizer.log" 2>&1 &
    visualizer_pid=$!
    pass "Brain MRI visualizer launched with PID $visualizer_pid"
    printf 'Close the visualizer window to finish the UI session.\n'
else
    pass "Visualizer skipped"
fi
