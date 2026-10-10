# KARNIX Module Inventory

This is a boundary map of the audited checkout, not a claim that every module
is production-ready or part of one executable. See each component's README,
CMake file, or Makefile for its authoritative details.

| Path | Language / role | Integration and known boundary |
| --- | --- | --- |
| `llm/` | Hosted C++20 Ollama client, CLI, image input, unit tests | Used by the ncurses viewer via child-process launch; Ollama local only |
| `os/main.c`, `os/Makefile` | Hosted C11/ncurses record viewer | Runs on host; repository-relative records; can start hosted C++ companion |
| `os/karnix_kernel/` | Freestanding 32-bit x86 kernel experiment (C/assembly) | Separate target/toolchain; no Ollama, hosted client, or claimed bridge |
| `os/karnix64/` | Separate freestanding x86_64 kernel and text shell (C++20/NASM, Limine) | QEMU shell; `ASK` uses host-side Ollama over private serial socket; no guest model runtime |
| `mathematical/`, `main.c++` | C++ math, calculus, vectors/matrices, graphs and demos | Educational/research code; standalone build, no unified test runner |
| `neural_network/` | C++ tensor, layers, optimizers, losses, CNN utilities | Reusable research foundation; headers do not imply trained model |
| `Brain_tumour/` | C++ four-class brain MRI CNN | Task-specific preprocessing/labels; optional SDL visualization |
| `Other_Disease/` | C++ lung, tumour, osteoarthritis, lumbar, unified-screening programs | Distinct task executables/checkpoints and input modalities |
| `karnix_spine_edge/` | C++ spine/MURA research tools, data/model workflows | Standalone CLI and optional visualizer/voice workflows |
| `spine_3d/` | C++ spine visualization experiment | Independent Makefile; not connected to local LLM |
| `spine_analytics_dashboard/` | JavaScript/TypeScript frontend/backend | Independent web stack; no automatic C++ LLM bridge |
| `Karnix_Web/` | Independent web application | Use its own README and manifests |
| `jester/` | Electron/web/voice experiment | Separate stack; not used by local C++ client |
| `NEWP/` | Independent software/research experiments | Preserve individual language/build boundaries |
| `Rust/` | Rust experiment(s) | Independent Cargo build |
| `private/os copy/` | Independent OS experiment | Separate from both KarnixOS viewer and `os/karnix_kernel/` |
| `data/` | Shared/sample image assets | Data must not be mistaken for validated patient examples |

## Medical model boundaries

The MRI brain tumour model, osteoarthritis X-ray workflows, lumbar/spine
experiments, lung X-ray baseline, and osteoporosis DEXA route have different
inputs and outputs. Do not substitute checkpoints between them. The audited
checkout does not provide a verified labeled DEXA dataset/checkpoint; the
osteoporosis path must report missing compatible model rather than fabricate
a prediction. Medical model results are research output, not diagnoses or
calibrated risk estimates.

## Scientific and unverified areas

The audited inventory did not verify a dedicated C++ orbital-mechanics,
spacecraft-control, or general signal-processing subsystem. Other possible
domain experiments should be documented in their own project after source
verification, not inferred from folder names. No hardware measurement,
flight readiness, or clinical validation is claimed.
