# KARNIX Verified Architecture

**Verified:** 2026-10-10  
**Host:** macOS 27, Apple Silicon arm64

Karnix is a collection of independently built research and software projects,
not one monolithic application. The OS-related work has three distinct targets:

1. [`os/main.c`](os/main.c) is a hosted C11/ncurses terminal viewer. It runs
   under macOS, reads repository-relative research data, and can launch a
   hosted child process.
2. [`os/karnix_kernel/`](os/karnix_kernel/) is an independent 32-bit
   freestanding x86 kernel experiment with assembly boot code, C kernel code,
   a linker script, VGA console, interrupts, and basic keyboard/mouse input.
   It has no general process, filesystem, HTTP, or Ollama runtime and does not
   execute the LLM.
3. [`os/karnix64/`](os/karnix64/) is a separate freestanding x86_64 C++20
   kernel. It boots through Limine in QEMU, enters long mode, establishes an
   identity map for the first 4 GiB, and provides a DOS/TempleOS-inspired
   blue-and-white framebuffer text desktop with polled PS/2 keyboard input.
   Escape returns from the command shell to the home menu. Its `ASK` command
   can relay prompts through a private serial socket to a host-side companion
   and local Ollama service. MedGemma inference remains on the host, not in
   the guest. This is not an exact replica; there is no full HolyC
   implementation, allocator, filesystem, userspace, or guest network stack.

The existing local LLM is a hosted C++20 application in [`llm/`](llm/). The
ncurses viewer starts it using POSIX `fork`/`exec` with argument-vector
forwarding. The x86_64 QEMU launcher also builds a host-side bridge: guest
text prompts travel through a private Unix socket connected to the emulated
COM1 port, and the bridge calls Ollama only at `127.0.0.1:11434`. This
supports guest-shell text chat but does not place MedGemma in the guest. The
32-bit kernel remains unconnected; native in-kernel inference is not
implemented.

## Component boundaries

| Component | Verified role | Build and status |
| --- | --- | --- |
| `os/main.c`, `os/Makefile` | Hosted record viewer and command dispatcher | C11 + ncurses; viewer build passed |
| `os/karnix_kernel/` | Separate freestanding 32-bit x86 kernel | Separate assembly/C toolchain; ELF cross-build passed from a temporary copy; ISO/QEMU boot not verified |
| `os/karnix64/` | Separate freestanding x86_64 C++20 kernel with a text shell | Cross-build, Limine hybrid ISO, and QEMU shell tests passed; chat uses a host-side Ollama bridge, no guest inference |
| `llm/` | Local Ollama API client, CLI, image-input handling, tests | C++20, CMake, libcurl, nlohmann/json; build and CTest passed |
| `mathematical/`, `main.c++` | Educational math, calculus, vector/matrix, graph and computational-graph code | Standalone C++14 demo compiled; no root build/test orchestration |
| `neural_network/` | Header-oriented tensors, layers, activations, losses, optimizers, CNN and gradient checking | Existing research building blocks; no repository-wide runner |
| `Brain_tumour/` | Four-label brain MRI research CNN and CLI | CLI built; optional SDL visualizer unavailable without SDL2/SDL2_ttf |
| `Other_Disease/` | C++17 lung, brain tumour, osteoarthritis, lumbar stenosis and unified medical-screening executables | All five targets built in the prior component audit; setup script builds `medical_screening` |
| `karnix_spine_edge/` | Spine-image and MURA research CLI, models/data, optional visualizer and voice wrapper | CLI and MURA pipeline built; optional SDL visualizer unavailable |
| `spine_analytics_dashboard/`, `Karnix_Web/`, `jester/` | Independent web, backend, or Electron applications | Separate manifests and runtimes; Karnix Web optionally invokes fixed C++ image-model adapters, but is not unified with hosted LLM |
| `spine_3d/`, `NEWP/`, `Rust/`, `private/os copy/` | Independent visualization, research, language, and OS experiments | Preserved; no monolithic integration |

The repository contains C++, C, assembly, Python, JavaScript/TypeScript, and
Rust. New local LLM application logic is C++; existing independent modules
remain in their native languages and build systems.

## Karnix Web research inference slice

`Karnix_Web/backend/` now exposes authenticated routes for a fixed disease
registry. Its model adapter invokes the configured lung X-ray, brain MRI,
osteoarthritis X-ray, or lumbar MRI executable with a bounded timeout,
restricted image root, file-signature check, and validated JSON output. The
catalog reports configured asset paths by status and checkpoint SHA-256; it
does not claim runtime compatibility until an actual inference succeeds.
Scores are exposed as uncalibrated model scores, never as clinical
probabilities. Osteoporosis/DEXA is explicitly unavailable because a
compatible checkpoint and verified labeled dataset are not present.

On 2026-10-10, the four C++ adapters built and each ran against its configured
repository sample image and a compatible checkpoint. The 6.2 MiB
`karnix_spine_edge/models/spine_model.weights` checkpoint failed to load for
the lumbar adapter; the smaller `lumbar_mri_cnn.weights` checkpoint loaded and
ran, so the backend environment example points to the latter. These are
engineering smoke checks, not model validation.

The backend test suite covers authentication, prediction authorization,
catalog readiness, image-root containment and signatures, model-output
validation, demo labeling, and audit events. No synthetic patient cases,
doctor roles, case authorization, report approval, DICOM viewer, GenAI
clinical copilot, or medical-reference retrieval are implemented yet.

## Research boundaries and non-claims

- MRI, X-ray, DEXA, and spine models have distinct modalities, preprocessing,
  labels, and checkpoint expectations; one model is not substituted for
  another.
- No verified osteoporosis DEXA checkpoint and labeled training dataset are
  included. The osteoporosis route fails when its model is unavailable.
- The LLM is a conversational research companion, not an autonomous agent. It
  cannot run shell commands, call model-directed tools, edit source files, or
  issue kernel operations.
- Image input accepts PNG/JPEG, up to four images of 10 MiB each. The model
  describes selected images, but its descriptions are not validated findings.
- No dedicated C++ orbital-mechanics, spacecraft-control, or general DSP
  subsystem was verified in the audited source tree. Do not treat unrelated
  experiments as flight-ready or production scientific systems.
- Medical outputs are experimental and are not diagnoses or calibrated
  clinical risk estimates. MedGemma is subject to Google's Health AI
  Developer Foundations terms and prohibited-use policy.

## Integration flow

```text
Hosted ncurses viewer (C11)
        | fork + exec, argv (no shell interpolation)
        v
Hosted C++20 CLI/client (llm/)
        | HTTP/JSON to loopback only
        v
Ollama local service -> selected local model
```

For commands, dependencies, and verification details see
[`KARNIX_BUILD.md`](KARNIX_BUILD.md),
[`KARNIX_LOCAL_LLM.md`](KARNIX_LOCAL_LLM.md),
[`KARNIX_TESTING.md`](KARNIX_TESTING.md), and
[`KARNIX_MODULES.md`](KARNIX_MODULES.md). The earlier, more detailed audit is
also retained in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).
