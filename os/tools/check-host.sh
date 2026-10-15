#!/usr/bin/env bash
set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
mode="${1:-full}"
missing=0

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        printf 'Missing command: %s\n' "$1" >&2
        missing=1
    fi
}

require_file() {
    if [ ! -f "$root/$1" ]; then
        printf 'Missing %s; %s\n' "$1" "$2" >&2
        missing=1
    fi
}

if [ "$(uname -s)" = Darwin ]; then
    for tool in node curl tar make cc nasm x86_64-elf-gcc x86_64-elf-ld; do
        require_command "$tool"
    done
    if [ "$mode" != kernel ]; then
        for tool in brew mformat mcopy qemu-system-x86_64; do
            require_command "$tool"
        done
        if command -v brew >/dev/null 2>&1; then
            prefix="$(brew --prefix e2fsprogs)"
            if [ ! -x "$prefix/sbin/mke2fs" ]; then
                echo 'Missing mke2fs; run: brew install e2fsprogs' >&2
                missing=1
            fi
        fi
    fi
    if [ "$missing" -ne 0 ]; then
        echo 'Install missing Mac build dependencies with: make -C os tools' >&2
        exit 1
    fi
    echo 'Native macOS cavOS build prerequisites are available.'
    exit 0
fi

if [ "$mode" != kernel ] && { [ "$(uname -s)" != Linux ] || [ "$(uname -m)" != x86_64 ]; }; then
    echo 'The complete X11/TWM image needs an x86_64 Linux build host or VM.' >&2
    echo 'macOS cannot execute the Alpine chroot or Linux loop-mount build steps.' >&2
    missing=1
fi

if [ ! -x "$HOME/opt/cross/bin/x86_64-cavos-gcc" ]; then
    echo 'Missing cavOS cross-compiler; on x86_64 Linux run: make -C os tools' >&2
    missing=1
fi
if [ ! -x "$HOME/opt/cross/bin/x86_64-cavos-ld" ]; then
    echo 'Missing cavOS cross-linker; on x86_64 Linux run: make -C os tools' >&2
    missing=1
fi
require_command nasm
require_file src/kernel/acpi/uacpi/tables.c 'run make -C os uacpi'
require_file src/kernel/include/uacpi/uacpi.h 'run make -C os uacpi'
require_file src/libs/musl/cavos-out/include/stdint.h 'run make -C os musl'

if [ "$mode" != kernel ]; then
    for tool in make gcc g++ git curl wget tar patch autoconf automake \
        bison flex makeinfo parted losetup mount mountpoint sudo mke2fs; do
        require_command "$tool"
    done
    if ! command -v mkdosfs >/dev/null 2>&1 && ! command -v mkfs.fat >/dev/null 2>&1; then
        echo 'Missing FAT filesystem tools; install dosfstools.' >&2
        missing=1
    fi
fi

if [ "$missing" -ne 0 ]; then
    echo 'Prerequisite check failed; no build or system changes were made.' >&2
    exit 1
fi
echo 'cavOS build prerequisites are available.'
