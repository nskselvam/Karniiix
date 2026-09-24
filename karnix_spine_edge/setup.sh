#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
DATASET_URL="https://www.kaggle.com/api/v1/datasets/download/shashwatwork/knee-osteoarthritis-dataset-with-severity"
MRI_DATASET_URL="https://www.kaggle.com/api/v1/datasets/download/abdullahkhan70/lumbar-spinal-mri-dataset"
CACHE_DIR="$SCRIPT_DIR/data/cache"
ARCHIVE="$CACHE_DIR/knee_osteoarthritis.zip"
MRI_ARCHIVE="$CACHE_DIR/lumbar_mri.zip"
OA_DIR="$SCRIPT_DIR/data/osteoarthritis"
MRI_DIR="$SCRIPT_DIR/data/mri"
MODEL="$SCRIPT_DIR/models/osteoarthritis_cnn.weights"
MRI_MODEL="$SCRIPT_DIR/models/lumbar_mri_cnn.weights"
IMAGE_COUNT="${IMAGE_COUNT:-600}"
MRI_IMAGE_COUNT="${MRI_IMAGE_COUNT:-600}"
EPOCHS="${EPOCHS:-3}"
MRI_EPOCHS="${MRI_EPOCHS:-3}"
BATCH_SIZE="${BATCH_SIZE:-16}"
DEXA_SOURCE="${DEXA_SOURCE:-}"
DEXA_SOURCE_NAME="${DEXA_SOURCE_NAME:-}"
DEXA_SOURCE_URL="${DEXA_SOURCE_URL:-}"
DEXA_LICENSE="${DEXA_LICENSE:-}"
DEXA_IMAGE_COUNT="${DEXA_IMAGE_COUNT:-600}"
DEXA_DIR="$SCRIPT_DIR/data/dexa"
DEXA_MODEL="$SCRIPT_DIR/models/dexa_cnn.weights"
LAUNCH_OS=0
FORCE_DATA=0
RUN_VISUALIZER=1
RUN_STARTED=$(date +%s)
TRAIN_LOG="$SCRIPT_DIR/models/oa_training_full.log"
TEST_LOG="$SCRIPT_DIR/models/oa_evaluation_full.log"
MRI_TRAIN_LOG="$SCRIPT_DIR/models/mri_training_full.log"
MRI_TEST_LOG="$SCRIPT_DIR/models/mri_evaluation_full.log"
DEXA_TRAIN_LOG="$SCRIPT_DIR/models/dexa_training_full.log"
DEXA_TEST_LOG="$SCRIPT_DIR/models/dexa_evaluation_full.log"

for argument in "$@"; do
    case "$argument" in
        --launch-os) LAUNCH_OS=1 ;;
        --force-data) FORCE_DATA=1 ;;
        --no-visualizer) RUN_VISUALIZER=0 ;;
        --help)
            printf 'Usage: ./setup.sh [--force-data] [--no-visualizer] [--launch-os]\n'
            printf 'Environment: IMAGE_COUNT=600 EPOCHS=3 MRI_IMAGE_COUNT=600 MRI_EPOCHS=3 BATCH_SIZE=16\n'
            printf 'Optional genuine DEXA: DEXA_SOURCE=/path DEXA_SOURCE_NAME=... DEXA_SOURCE_URL=... DEXA_LICENSE=...\n'
            exit 0
            ;;
        *) printf 'Unknown option: %s\n' "$argument" >&2; exit 2 ;;
    esac
done

step() { printf '\n\033[1;36m[%s]\033[0m %s\n' "$1" "$2"; }
pass() { printf '\033[1;32m[PASS]\033[0m %s\n' "$1"; }
fail() { printf '\033[1;31m[FAIL]\033[0m %s\n' "$1" >&2; exit 1; }

cd "$SCRIPT_DIR"

step "1/13" "Checking prerequisites"
for command_name in python3 curl cmake clang++; do
    command -v "$command_name" >/dev/null 2>&1 || fail "$command_name is required"
    pass "$command_name: $(command -v "$command_name")"
done

if [[ "$(uname)" == "Darwin" ]]; then
    command -v brew >/dev/null 2>&1 || fail "Homebrew is required for the native SDL2 animation"
    if ! brew list --versions sdl2 >/dev/null 2>&1 ||
       ! brew list --versions sdl2_ttf >/dev/null 2>&1; then
        step "1/13" "Installing SDL2 native graphics dependencies"
        brew install sdl2 sdl2_ttf
    fi
    pass "SDL2 and SDL2_ttf are installed"
fi

step "2/13" "Creating isolated Python environment"
if [[ ! -x "$SCRIPT_DIR/.venv/bin/python3" ]]; then
    python3 -m venv "$SCRIPT_DIR/.venv"
fi
"$SCRIPT_DIR/.venv/bin/python3" -m pip install --quiet --upgrade pip
"$SCRIPT_DIR/.venv/bin/python3" -m pip install --quiet 'Pillow>=10,<13'
pass "Python environment ready"

step "3/13" "Downloading CC BY 4.0 knee osteoarthritis dataset"
mkdir -p "$CACHE_DIR" "$SCRIPT_DIR/models"
if [[ ! -s "$ARCHIVE" || "$FORCE_DATA" -eq 1 ]]; then
    temporary_archive="$ARCHIVE.part"
    rm -f "$temporary_archive"
    curl --fail --location --retry 3 --progress-bar "$DATASET_URL" --output "$temporary_archive"
    mv "$temporary_archive" "$ARCHIVE"
fi
"$SCRIPT_DIR/.venv/bin/python3" -c 'import sys, zipfile; sys.exit(0 if zipfile.is_zipfile(sys.argv[1]) else 1)' "$ARCHIVE" \
    || fail "Downloaded dataset is not a valid ZIP archive"
pass "Dataset archive: $ARCHIVE"

step "4/13" "Downloading CC0 lumbar MRI dataset"
if [[ ! -s "$MRI_ARCHIVE" || "$FORCE_DATA" -eq 1 ]]; then
    temporary_mri_archive="$MRI_ARCHIVE.part"
    rm -f "$temporary_mri_archive"
    curl --fail --location --retry 3 --progress-bar "$MRI_DATASET_URL" \
        --output "$temporary_mri_archive"
    mv "$temporary_mri_archive" "$MRI_ARCHIVE"
fi
"$SCRIPT_DIR/.venv/bin/python3" -c 'import sys, zipfile; sys.exit(0 if zipfile.is_zipfile(sys.argv[1]) else 1)' "$MRI_ARCHIVE" \
    || fail "Downloaded MRI dataset is not a valid ZIP archive"
pass "MRI archive: $MRI_ARCHIVE"

step "5/13" "Preparing balanced OA dataset"
if [[ ! -f "$OA_DIR/train.csv" || ! -f "$OA_DIR/test.csv" || "$FORCE_DATA" -eq 1 ]]; then
    "$SCRIPT_DIR/.venv/bin/python3" "$SCRIPT_DIR/prepare_oa_dataset.py" \
        --archive "$ARCHIVE" --output "$OA_DIR" --count "$IMAGE_COUNT"
fi
train_rows=$(($(wc -l < "$OA_DIR/train.csv") - 1))
test_rows=$(($(wc -l < "$OA_DIR/test.csv") - 1))
[[ $((train_rows + test_rows)) -eq "$IMAGE_COUNT" ]] \
    || fail "Expected $IMAGE_COUNT prepared images, found $((train_rows + test_rows))"
pass "Prepared $train_rows training and $test_rows held-out test images"

step "6/13" "Preparing balanced lumbar MRI dataset"
if [[ ! -f "$MRI_DIR/train.csv" || ! -f "$MRI_DIR/test.csv" ||
    ! -f "$MRI_DIR/screen.csv" || "$FORCE_DATA" -eq 1 ]]; then
    "$SCRIPT_DIR/.venv/bin/python3" "$SCRIPT_DIR/prepare_mri_dataset.py" \
        --archive "$MRI_ARCHIVE" --output "$MRI_DIR" --count "$MRI_IMAGE_COUNT"
fi
mri_train_rows=$(($(wc -l < "$MRI_DIR/train.csv") - 1))
mri_test_rows=$(($(wc -l < "$MRI_DIR/test.csv") - 1))
[[ $((mri_train_rows + mri_test_rows)) -eq "$MRI_IMAGE_COUNT" ]] \
    || fail "Expected $MRI_IMAGE_COUNT MRI images, found $((mri_train_rows + mri_test_rows))"
pass "Prepared $mri_train_rows MRI training and $mri_test_rows MRI test images"

step "7/13" "Building the C++ CNN and native animation"
build_started=$(date +%s)
if [[ -f "$SCRIPT_DIR/build/CMakeCache.txt" ]]; then
    cached_source=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$SCRIPT_DIR/build/CMakeCache.txt")
    if [[ -n "$cached_source" && "$cached_source" != "$SCRIPT_DIR" ]]; then
        printf '[INFO] Removing stale CMake cache from %s\n' "$cached_source"
        rm -rf "$SCRIPT_DIR/build"
    fi
fi
cmake -S "$SCRIPT_DIR" -B "$SCRIPT_DIR/build"
cmake --build "$SCRIPT_DIR/build" --config Release
[[ -x "$SCRIPT_DIR/build/spine_cli" ]] || fail "spine_cli was not built"
[[ -x "$SCRIPT_DIR/build/cnn_visualizer" ]] || fail "cnn_visualizer was not built"
pass "Built spine_cli and cnn_visualizer in $(( $(date +%s) - build_started )) seconds"

step "8/13" "Training OA CNN with per-image terminal telemetry"
rm -f "$MODEL"
rm -rf "$SCRIPT_DIR/models/checkpoints/oa"
mkdir -p "$SCRIPT_DIR/models/checkpoints/oa"
train_started=$(date +%s)
"$SCRIPT_DIR/build/spine_cli" --train "$OA_DIR/train.csv" "$EPOCHS" \
    --batch-size "$BATCH_SIZE" --checkpoint-dir "$SCRIPT_DIR/models/checkpoints/oa" --verbose \
    | tee "$TRAIN_LOG"
train_duration=$(( $(date +%s) - train_started ))
[[ -s "$MODEL" ]] || fail "Training did not create $MODEL"
[[ -s "$SCRIPT_DIR/models/checkpoints/oa/epoch_${EPOCHS}.weights" ]] \
    || fail "Final OA epoch checkpoint is missing"
pass "Model saved: $MODEL"
pass "Training wall-clock duration: ${train_duration} seconds"
pass "Complete training log: $TRAIN_LOG"

step "9/13" "Testing every held-out OA image with detailed telemetry"
test_started=$(date +%s)
"$SCRIPT_DIR/build/spine_cli" --evaluate "$OA_DIR/test.csv" --verbose \
    | tee "$TEST_LOG"
test_duration=$(( $(date +%s) - test_started ))
pass "Testing wall-clock duration: ${test_duration} seconds"
pass "Complete test log: $TEST_LOG"

step "10/13" "Training lumbar MRI CNN with per-image telemetry"
rm -f "$MRI_MODEL"
rm -rf "$SCRIPT_DIR/models/checkpoints/mri"
mkdir -p "$SCRIPT_DIR/models/checkpoints/mri"
mri_train_started=$(date +%s)
"$SCRIPT_DIR/build/spine_cli" --train "$MRI_DIR/train.csv" "$MRI_EPOCHS" \
    --model "$MRI_MODEL" --task LUMBAR_MRI --negative NO_STENOSIS \
    --positive STENOSIS --batch-size "$BATCH_SIZE" \
    --checkpoint-dir "$SCRIPT_DIR/models/checkpoints/mri" --verbose | tee "$MRI_TRAIN_LOG"
mri_train_duration=$(( $(date +%s) - mri_train_started ))
[[ -s "$MRI_MODEL" ]] || fail "MRI training did not create $MRI_MODEL"
[[ -s "$SCRIPT_DIR/models/checkpoints/mri/epoch_${MRI_EPOCHS}.weights" ]] \
    || fail "Final MRI epoch checkpoint is missing"
pass "MRI model saved: $MRI_MODEL"

step "11/13" "Testing every held-out lumbar MRI image"
mri_test_started=$(date +%s)
"$SCRIPT_DIR/build/spine_cli" --evaluate "$MRI_DIR/test.csv" \
    --model "$MRI_MODEL" --task LUMBAR_MRI --negative NO_STENOSIS \
    --positive STENOSIS --verbose | tee "$MRI_TEST_LOG"
mri_test_duration=$(( $(date +%s) - mri_test_started ))
pass "MRI testing wall-clock duration: ${mri_test_duration} seconds"

if [[ -n "$DEXA_SOURCE" ]]; then
    [[ -n "$DEXA_SOURCE_NAME" && -n "$DEXA_SOURCE_URL" && -n "$DEXA_LICENSE" ]] \
        || fail "DEXA_SOURCE_NAME, DEXA_SOURCE_URL, and DEXA_LICENSE are required with DEXA_SOURCE"
    step "DEXA" "Preparing genuine labeled DEXA scans"
    "$SCRIPT_DIR/.venv/bin/python3" "$SCRIPT_DIR/prepare_dexa_dataset.py" \
        --source "$DEXA_SOURCE" --output "$DEXA_DIR" --count "$DEXA_IMAGE_COUNT" \
        --source-name "$DEXA_SOURCE_NAME" --source-url "$DEXA_SOURCE_URL" \
        --license "$DEXA_LICENSE"
    rm -f "$DEXA_MODEL"
    rm -rf "$SCRIPT_DIR/models/checkpoints/dexa"
    mkdir -p "$SCRIPT_DIR/models/checkpoints/dexa"
    "$SCRIPT_DIR/build/spine_cli" --train "$DEXA_DIR/train.csv" "$EPOCHS" \
        --model "$DEXA_MODEL" --task DEXA_BMD --negative NORMAL_BMD \
        --positive OSTEOPOROSIS --batch-size "$BATCH_SIZE" \
        --checkpoint-dir "$SCRIPT_DIR/models/checkpoints/dexa" --verbose | tee "$DEXA_TRAIN_LOG"
    [[ -s "$SCRIPT_DIR/models/checkpoints/dexa/epoch_${EPOCHS}.weights" ]] \
        || fail "Final DEXA epoch checkpoint is missing"
    "$SCRIPT_DIR/build/spine_cli" --evaluate "$DEXA_DIR/test.csv" \
        --model "$DEXA_MODEL" --task DEXA_BMD --negative NORMAL_BMD \
        --positive OSTEOPOROSIS --verbose | tee "$DEXA_TEST_LOG"
else
    printf '\033[1;33m[DEXA NOTICE]\033[0m No verified open labeled DEXA image archive was found.\n'
    printf 'Set DEXA_SOURCE and source/license metadata to train only on genuine DXA exports.\n'
fi

step "12/13" "Screening OA and MRI demonstration images"
"$SCRIPT_DIR/build/spine_cli" --screen "$OA_DIR/screen" | tee "$SCRIPT_DIR/models/oa_screening.txt"
"$SCRIPT_DIR/build/spine_cli" --evaluate "$MRI_DIR/screen.csv" \
    --model "$MRI_MODEL" --task LUMBAR_MRI --negative NO_STENOSIS \
    --positive STENOSIS | tee "$SCRIPT_DIR/models/mri_screening.txt"
pass "Evaluation and screening completed"

step "13/13" "Building KarnixOS host application"
if command -v gcc >/dev/null 2>&1 && make -C "$ROOT_DIR" -f os/Makefile; then
    pass "KarnixOS built at $ROOT_DIR/KarnixOS"
else
    printf '\033[1;33m[WARN]\033[0m KarnixOS host build skipped; gcc and ncurses are required\n'
fi

total_duration=$(( $(date +%s) - RUN_STARTED ))
model_train_seconds=$(awk '/TRAINING DURATION:/ {print $3}' "$TRAIN_LOG")
model_test_seconds=$(awk '/TEST DURATION:/ {print $3}' "$TEST_LOG")
mri_train_seconds=$(awk '/TRAINING DURATION:/ {print $3}' "$MRI_TRAIN_LOG")
mri_test_seconds=$(awk '/TEST DURATION:/ {print $3}' "$MRI_TEST_LOG")
printf '\n\033[1;32mFULL PIPELINE COMPLETE\033[0m\n'
printf 'Training duration: %s seconds (shell wall clock: %d seconds)\n' \
    "$model_train_seconds" "$train_duration"
printf 'Testing duration:  %s seconds (shell wall clock: %d seconds)\n' \
    "$model_test_seconds" "$test_duration"
printf 'MRI training:      %s seconds (shell wall clock: %d seconds)\n' \
    "$mri_train_seconds" "$mri_train_duration"
printf 'MRI testing:       %s seconds (shell wall clock: %d seconds)\n' \
    "$mri_test_seconds" "$mri_test_duration"
printf 'Total setup time:  %d seconds (%d min %d sec)\n' \
    "$total_duration" "$((total_duration / 60))" "$((total_duration % 60))"
printf 'Training log:      %s\n' "$TRAIN_LOG"
printf 'Testing log:       %s\n\n' "$TEST_LOG"
printf 'MRI training log:  %s\n' "$MRI_TRAIN_LOG"
printf 'MRI testing log:   %s\n\n' "$MRI_TEST_LOG"
printf 'Train again:  cd %s && ./build/spine_cli --train data/osteoarthritis/train.csv %s --batch-size %s --checkpoint-dir models/checkpoints/oa --verbose\n' "$SCRIPT_DIR" "$EPOCHS" "$BATCH_SIZE"
printf 'Evaluate:     ./build/spine_cli --evaluate data/osteoarthritis/test.csv --verbose\n'
printf 'Screen:       ./build/spine_cli --screen data/osteoarthritis/screen\n'
printf 'Native CNN:   ./build/cnn_visualizer data/osteoarthritis/screen models/osteoarthritis_cnn.weights\n'
printf 'MRI CNN:      ./build/cnn_visualizer data/mri/screen models/lumbar_mri_cnn.weights LUMBAR_MRI NO_STENOSIS STENOSIS\n'
printf 'Run KarnixOS: cd %s && ./KarnixOS\n' "$ROOT_DIR"

if [[ "$RUN_VISUALIZER" -eq 1 ]]; then
    printf '\nLaunching native C++ CNN animation...\n'
    "$SCRIPT_DIR/build/cnn_visualizer" "$OA_DIR/screen" "$MODEL" \
        >"$SCRIPT_DIR/models/cnn_visualizer.log" 2>&1 &
    visualizer_pid=$!
    "$SCRIPT_DIR/build/cnn_visualizer" "$MRI_DIR/screen" "$MRI_MODEL" \
        LUMBAR_MRI NO_STENOSIS STENOSIS >"$SCRIPT_DIR/models/mri_visualizer.log" 2>&1 &
    mri_visualizer_pid=$!
    printf 'OA visualizer PID %d; MRI visualizer PID %d. Close windows when finished.\n' \
        "$visualizer_pid" "$mri_visualizer_pid"
fi

if [[ "$LAUNCH_OS" -eq 1 ]]; then
    cd "$ROOT_DIR"
    exec ./KarnixOS
fi
