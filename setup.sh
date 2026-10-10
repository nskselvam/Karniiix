#!/usr/bin/env bash

set -euo pipefail

MODEL="${KARNIX_MODEL:-medgemma1.5:4b}"

usage() {
    cat <<'EOF'
Usage: ./setup.sh

Installs the macOS build tools, builds the KarnixOS x86_64 kernel and host
Ollama bridge, and runs QEMU boot and desktop keyboard checks. By default,
the script opens KarnixOS in a QEMU window.

Ollama must be running locally and KARNIX_MODEL (default: medgemma1.5:4b)
must already be installed to launch the interactive desktop with chat.
The model is not downloaded automatically.

Set KARNIX_ROOT to use a checkout other than the directory containing this
script. MedGemma inference runs in host Ollama, not inside the guest.
EOF
}

if (($# > 0)); then
    echo "This script does not accept options." >&2
    usage >&2
    exit 2
fi

HOST_OS="$(uname -s)"
HOST_ARCH="$(uname -m)"
if [[ "$HOST_OS" != "Darwin" ]]; then
    echo "This setup script currently supports macOS only." >&2
    exit 1
fi
if [[ "$HOST_ARCH" != "arm64" && "$HOST_ARCH" != "x86_64" ]]; then
    echo "Unsupported macOS CPU architecture: $HOST_ARCH" >&2
    exit 1
fi

if ! command -v brew >/dev/null 2>&1; then
    for brew_path in /opt/homebrew/bin/brew /usr/local/bin/brew; do
        if [[ -x "$brew_path" ]]; then
            export PATH="$(dirname "$brew_path"):$PATH"
            break
        fi
    done
fi

if ! command -v brew >/dev/null 2>&1; then
    echo "Homebrew was not found. Install it from https://brew.sh, then rerun ./setup.sh." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="${KARNIX_ROOT:-$SCRIPT_DIR}"
if [[ ! -f "$REPO_ROOT/os/karnix64/Makefile" ]]; then
    echo "KarnixOS x86_64 kernel source not found at: $REPO_ROOT" >&2
    echo "Set KARNIX_ROOT to the repository directory and rerun ./setup.sh." >&2
    exit 1
fi
REPO_ROOT="$(cd "$REPO_ROOT" && pwd -P)"

echo "KarnixOS x86_64 setup"
echo "Host: $HOST_OS ($HOST_ARCH)"
echo "Repository: $REPO_ROOT"
echo "Ollama model for interactive chat: $MODEL"
echo

packages_to_install=()
for package in x86_64-elf-gcc limine qemu nasm xorriso cmake nlohmann-json ollama; do
    if ! brew list --versions "$package" >/dev/null 2>&1; then
        packages_to_install+=("$package")
    fi
done

if ((${#packages_to_install[@]} > 0)); then
    echo "Installing missing Homebrew packages: ${packages_to_install[*]}"
    brew install "${packages_to_install[@]}"
else
    echo "Required Homebrew packages are installed."
fi

for command_name in x86_64-elf-g++ x86_64-elf-ld limine \
    qemu-system-x86_64 nasm xorriso cmake ollama curl; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Required command is unavailable after dependency setup: $command_name" >&2
        exit 1
    fi
done

echo
echo "Building the host-side Ollama bridge and running kernel checks..."
make -C "$REPO_ROOT/os/karnix64" host-bridge check check-desktop

if [[ ! -t 0 || ! -t 1 ]]; then
    echo
    echo "Build and QEMU checks passed, but no interactive terminal is available."
    echo "Launch the desktop with: make -C \"$REPO_ROOT/os/karnix64\" run"
    exit 0
fi

if ! curl --silent --show-error --fail \
    http://127.0.0.1:11434/api/tags >/dev/null 2>&1; then
    echo "Ollama is not running at 127.0.0.1:11434." >&2
    echo "Start the Ollama app or run 'ollama serve', then rerun ./setup.sh." >&2
    exit 1
fi
if ! ollama show "$MODEL" >/dev/null 2>&1; then
    echo "The configured model '$MODEL' is not installed in Ollama." >&2
    echo "Install it explicitly with: ollama pull \"$MODEL\"" >&2
    exit 1
fi

echo
echo "Launching the KarnixOS GNOME-inspired desktop in QEMU."
echo "Press A for Activities, T for Terminal, or 2 for Local AI."
exec make -C "$REPO_ROOT/os/karnix64" run
