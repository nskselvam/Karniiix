# Karnix Repository Architecture and Local LLM Audit

**Audit date:** 2026-10-10  
**Host:** macOS 27, Apple Silicon arm64  
**Scope:** Project boundaries, source/build entrypoints, the hosted KarnixOS
viewer, the freestanding kernel, existing C++ math/neural-network/medical
components, local LLM prerequisites, and focused build verification. Generated
vendor directories and build caches were not treated as application source.

## Executive summary

Karnix is a repository of independent projects, not a single operating system
or unified build. The repository currently has three distinct OS-related programs/targets:

1. `os/main.c` is a hosted C11/ncurses terminal viewer. It runs as an ordinary
   macOS process and can start a hosted C++ program with POSIX `fork`/`exec`.
2. `os/karnix_kernel/` is a separate 32-bit freestanding x86 kernel, assembled
   from `boot.asm` and compiled from `kernel.c`. It has a VGA UI, interrupt
   handlers, keyboard/mouse input, and a small command interface. It does not
   have the hosted runtime, general filesystem, process-launch, or HTTP
   facilities needed to run Ollama or a general-purpose LLM.
3. `os/karnix64/` is a separate freestanding x86_64 C++20 kernel. It boots
   via Limine in QEMU, enters long mode, sets an identity map for the first
   4 GiB, and displays a DOS/TempleOS-inspired blue-and-white framebuffer
   text desktop with polled PS/2 keyboard input. Escape returns from the
   command shell to the home menu. This independent implementation is not an
   exact replica; it does not provide full HolyC, memory allocation,
   filesystem/storage, userspace, or guest networking. In QEMU, `ASK` can
   relay text through a private serial socket to the host Ollama companion;
   model inference stays host-side.

The existing local-LLM integration remains a **hosted C++20 application**
under `llm/`. The ncurses viewer launches it; the x86_64 QEMU shell can relay
text to the host through the serial bridge. Ollama stays loopback-only. No
kernel-side model runtime is implemented.

The hosted C++ client, ncurses viewer, and supported health CLI targets
compile in this environment. Deterministic local mock-server tests pass.
Real MedGemma text and image generation, conversation history, viewer
integration, and loopback-only inference have been verified.

## Architecture map

| Component | Classification and implementation | Build/integration notes |
| --- | --- | --- |
| `os/main.c` | Hosted C11/ncurses research-record viewer | `make -f os/Makefile`; reads repository-relative data and starts hosted commands |
| `os/karnix_kernel/` | Freestanding 32-bit x86 kernel: `boot.asm`, C kernel, linker script | Separate `make build`/QEMU workflow; built in a temporary source copy for this audit |
| `os/karnix64/` | Freestanding x86_64 C++20 kernel with text shell and host Ollama bridge | Cross-build, Limine hybrid ISO, and QEMU shell/chat tests passed; model executes on host |
| `llm/` | New hosted C++20 local Ollama client, CLI, library, and tests | CMake; launched as a child of the ncurses viewer |
| `main.c++`, `mathematical/` | C++14 educational math/calculus/graph/autodiff demonstration and reusable headers | Root demo compiled; no repository-wide C++ build |
| `neural_network/` | C++ tensor, layers, image utilities, losses, optimizers, CNN and gradient-check helper | Header-oriented code; no unified root test runner was found |
| `Brain_tumour/` | C++14 four-class MRI CNN, training/evaluation CLI, optional SDL visualizer | Fresh out-of-tree CLI build passed; visualizer disabled because SDL2/SDL2_ttf are unavailable |
| `Other_Disease/` | C++17 lung-X-ray baseline, brain tumour, osteoarthritis, lumbar stenosis, and unified medical executables | All five CMake executable targets built |
| `karnix_spine_edge/` | C++14 spine image research CLI, MURA pipeline, optional SDL visualizer, data/model workflows | `spine_cli` and `mura_pipeline` built; visualizer disabled because SDL2/SDL2_ttf are unavailable |
| `spine_analytics_dashboard/` | Independent Vite/React frontend and Express backend | Separate npm projects; does not currently invoke the C++ LLM |
| `Karnix_Web/` | Separate web application with its own frontend/backend | Preserve its own environment and test/build workflow |
| `jester/` | Independent Electron/web/voice project | Existing server code references remote OpenAI APIs; it is not used by the new local C++ client |
| `NEWP/`, `Rust/`, `spine_3d/`, `private/os copy/` | Independent research/app/kernel experiments | Preserve separate languages, data, and build systems; no monolithic integration was introduced |

The source inventory includes C++, C, Assembly, JavaScript/TypeScript, Python,
and Rust. Existing non-C++ projects and integration points were preserved;
new local LLM application logic is C++.

### Existing scientific and research boundaries

- `mathematical/` provides arithmetic, matrix/linear algebra, calculus,
  vector, graph, and computational-graph code. The root `main.c++` is a
  demonstration program, not a production scientific-computing package.
- `neural_network/` contains tensors and neural-network building blocks,
  including activation, convolution, pooling, fully-connected, optimizer,
  loss, image-processing, and gradient-check code. The generic CNN header is
  not a trained disease model.
- Health-related projects use separate architectures, weights, labels, and
  image modalities. Brain MRI, osteoarthritis X-ray, and osteoporosis DEXA
  tasks are not interchangeable.
- There is no verified DEXA/osteoporosis model checkpoint or labeled DEXA
  training dataset in this checkout. The osteoporosis route fails when its
  separate checkpoint is unavailable; it does not substitute another model.
- The structure search found no dedicated C++ orbital-mechanics, spacecraft,
  or signal-processing subsystem. Independent Python/JavaScript and research
  projects remain separate rather than being relabeled as C++ modules.

## Local LLM design

```text
Hosted ncurses viewer (C)
        | fork + exec, argv
        v
Hosted C++20 client/library (llm/)
        | HTTP to localhost only
        v
Ollama local service -> local model

Separate x86_64 kernel (os/karnix64/)
        | ASK -> COM1 -> private host Unix socket
        v
Host companion -> Ollama on loopback

Native model execution inside either kernel: not implemented
```

`llm/src/OllamaClient.cpp` provides:

- Local-only URL validation. `KARNIX_OLLAMA_URL` may select another loopback
  port, but non-loopback and HTTPS endpoints are rejected.
- Proxy bypass for loopback requests so configured HTTP proxies do not receive
  prompts.
- Ollama CLI/service/model checks through PATH and `/api/tags`.
- JSON request construction and newline-delimited streaming response parsing
  for `/api/chat`.
- Conversation history and `/clear`, with a 16,000-character history bound
  and `num_ctx=4096` in the request. Character-to-token correspondence is
  approximate; the Ollama runtime/model still imposes its own context limits.
- Configurable model selection: `KARNIX_MODEL` or `--model`; the default
  is `medgemma1.5:4b` (MedGemma 1.5 4B).
- A 10-minute chat timeout, bounded health-check timeout, Ctrl+C cancellation,
  malformed-response handling, and actionable missing-model/runtime errors.
- MedGemma's `<unused94>`/`<unused95>` internal thought sections and visible
  image boundary markers are suppressed from displayed output and conversation
  history, including when markers span streamed response chunks.
- Up to four local PNG/JPEG images per prompt, size-limited to 10 MiB each,
  validated by file signature, base64-encoded, and sent in Ollama's chat
  `images` field. Text history is capped at 16,000 characters, and the four
  most recent image attachments are retained for follow-up turns.
- A status display of installed models and current available memory/disk.

The client does not download models, call a cloud service, run
tools/shell commands, or perform autonomous actions. Only the image files
explicitly selected by the user are sent, over loopback, to the local Ollama
service. Model availability is not proof of task compatibility. Ollama 0.40.2
and `qwen3:4b` were initially installed for the first inference test; the user
subsequently selected MedGemma, so the default is now `medgemma1.5:4b`. See
the updated test and runtime status below.

The C++ chat interface accepts PNG/JPEG image files. An image prompt can be
sent from either the standalone CLI or the hosted KarnixOS command parser.
MedGemma 1.5 supports vision; other selected models must support vision too.
Image descriptions are model output and are not validated medical findings.

The `llm` command added to the hosted viewer supports:

```text
llm
llm --interactive
llm --status
llm "Explain a matrix"
llm --image "/path/to/scan.png" "Describe this image"
llm --model <installed-model> "Explain a matrix"
```

In an interactive C++ session the supported commands are `/help`, `/clear`,
`/image <path> [prompt]`, `/model [name]`, `/status`, and `/exit`. Up to four
PNG/JPEG images of at most 10 MiB each are accepted per prompt. The bare-metal
kernel is not a caller of the C++ client.

## Dependencies and licensing

| Dependency | Purpose | License/source |
| --- | --- | --- |
| C++20 compiler and CMake 3.20+ | Build the hosted client | Free toolchain; Apple Clang and CMake used for this build |
| libcurl | HTTP transport to the loopback Ollama API | curl license, MIT-style; see [curl copyright](https://curl.se/docs/copyright.html) |
| nlohmann/json 3.11+ | JSON encoding/decoding | MIT; see [nlohmann/json license](https://github.com/nlohmann/json/blob/develop/LICENSE.MIT) |
| Ollama | Optional local model runtime | Install from [Ollama's official download page](https://ollama.com/download); review runtime/model terms separately |

On macOS, if CMake cannot find the C++ prerequisites, install only the missing
build dependencies, for example:

```bash
brew install cmake nlohmann-json
```

The current build finds Apple SDK libcurl and nlohmann/json from the selected
environment. No C++ dependency was copied into the repository.

## Build and launch

From the repository root:

```bash
cmake -S llm -B llm/build -DCMAKE_BUILD_TYPE=Release
cmake --build llm/build --parallel
ctest --test-dir llm/build --output-on-failure
./llm/build/karnix_llm --status
```

Ollama 0.40.2 was installed with Homebrew and runs as a local service. The
current selected model is MedGemma 1.5 4B; its Ollama manifest lists a model
layer of approximately 3.34 GB:

```bash
ollama pull medgemma1.5:4b
ollama list
```

If the service stops, start it with `brew services start ollama` or `ollama
serve`. Verify local readiness with
`./llm/build/karnix_llm --status`. Then launch from the viewer:

```bash
make -f os/Makefile
./KarnixOS
```

Enter `llm` for an interactive local session. The viewer must be launched
from the repository root because existing data, model, and companion paths
are repository-relative.

## Audit host and build verification

The inspected host is macOS 27/arm64 with Apple Clang 21, CMake 4.4, libcurl
8.7.1, and nlohmann/json 3.11.2 discoverable. At the time of the check,
available disk space was approximately 544 GiB. The `qwen3:4b` and
`medgemma1.5:4b` models are both retained locally. The MedGemma model layer is
about 3.34 GB. Available memory varies while inference runs; the status
command reports a current measurement, not a compatibility or performance
guarantee.

| Verification | Result |
| --- | --- |
| New C++20 local client/library and CLI build | Passed |
| CTest mock/local HTTP suite | Passed: request payload, image base64 and payload serialization, rejection of non-images, image-context retention/bounds, context trimming, history clearing, streaming/history preservation, internal-thought and image-sentinel suppression, runtime/model detection, missing-model guidance, malformed response, stopped service, timeout, cancellation, and rejection of non-local endpoints |
| `karnix_llm --status` | Passed with Ollama running and MedGemma installed; correctly reports selected model and loopback service |
| ncurses KarnixOS build | Passed with `make -f os/Makefile OUT=/tmp/KarnixOS` |
| KarnixOS `llm --status` pseudo-terminal test | Passed; viewer launched the C++ executable and returned to its command loop |
| KarnixOS one-shot MedGemma pseudo-terminal test | Passed; generated a matrix definition through the viewer and returned to its command loop |
| Brain tumour CLI | Passed fresh out-of-tree build; optional visualizer unavailable because SDL2/SDL2_ttf are not installed |
| Other_Disease CMake targets | Passed: lung baseline, osteoarthritis, brain tumour, lumbar stenosis, unified medical predictor |
| Spine Edge CMake targets | Passed: spine CLI and MURA pipeline; optional SDL visualizer unavailable |
| Root math demonstration | Compiled; existing warning at `mathematical/Calc/CalcEng.hpp:31` for a non-void function path without a return remains unfixed |
| Freestanding kernel ELF | Passed cross-build from a temporary copy using i686-elf-gcc, NASM, and linker; existing tracked object/ELF files were not overwritten |
| Existing i386 kernel boot/ISO | Not verified during the current 64-bit milestone |
| New x86_64 kernel build | Passed with `x86_64-elf-g++`, `x86_64-elf-ld`, and NASM |
| New x86_64 Limine hybrid ISO | Passed |
| New x86_64 QEMU boot smoke test | Passed; serial marker confirmed long mode, 1 GiB identity map, and explicit `guest_llm_inference=not_implemented` |
| Native MedGemma inside either kernel | Not implemented; no allocator/model loader/filesystem/inference runtime/network stack |
| Real Ollama generation with `qwen3:4b` | Passed: generated a pointer explanation; two-turn test recalled “emerald”; same client also passed under a sandbox permitting only localhost:11434 |
| Real MedGemma 1.5 generation | Passed: generated a matrix definition through the C++ CLI; output contained only the final answer |
| Real MedGemma 1.5 image input | Passed: described a local non-clinical scheduler chart through both the standalone CLI and KarnixOS viewer |
| MedGemma multi-turn history | Passed: recalled “emerald” from the preceding turn |
| Offline MedGemma image inference | Passed in a macOS sandbox denying outbound network access except `localhost:11434` |
| KarnixOS setup workflow | Passed: built the host bridge and x86_64 kernel, created the ISO, and passed QEMU boot, desktop, and keyboard checks |
| `bash -n setup.sh` | Passed |

Warnings from existing shared neural-network headers occurred during the
health C++ builds; they did not fail those builds and were not changed as part
of this integration.

## Existing working-tree changes preserved

The repository already contained uncommitted medical/OS changes and modified
kernel data/binary artifacts before this local LLM implementation. Those
changes were retained. The kernel source and pre-existing kernel artifacts
were not edited by the LLM integration; the audit kernel build used a
temporary source copy. Build output for the new LLM target is ignored under
`llm/build/`.

## Remaining blockers and next actions

1. MedGemma is governed by Google's Health AI Developer Foundations Terms and
   Prohibited Use Policy. It is a research model, not a clinically validated
   diagnostic system; image or text generation does not establish medical
   accuracy or clinical suitability.
2. `qwen3:4b` remains installed as a fallback only if explicitly selected with
   `KARNIX_MODEL` or `--model`. No automatic fallback occurs.
3. The kernel ISO boot flow requires GRUB tooling. A successful ELF compile
   does not claim that a bootable ISO was built or launched in QEMU.
4. Medical models remain research-only. In particular, there is no validated
   DEXA checkpoint/dataset; the brain-tumour project's documented held-out
   result is poor and does not support clinical use.
5. Existing standalone web, Python, Rust, and research projects retain their
   own build/dependency systems and have not been converted or connected to
   the C++ local LLM.
