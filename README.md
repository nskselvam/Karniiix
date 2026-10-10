# Karnix

Karnix is a collection of research, education, and software-development
projects. It is not one application with one build. The repository includes
C++ mathematics and neural-network components, experimental medical-image
models, a spine screening CLI, terminal viewers, web applications, and
independent operating-system and language experiments.

> **Medical safety:** Everything in this repository that handles health data
> or produces model outputs is experimental and for research/education only.
> It is not a medical device, must not be used to diagnose or treat anyone,
> and does not replace a qualified clinician. A model's class, score, or
> confidence is not a clinical risk estimate. Do not submit identifiable
> patient data to these projects unless you have separately reviewed privacy,
> security, consent, and regulatory requirements.

## Contents

- [Start here](#start-here)
- [Project map](#project-map)
- [What is integrated](#what-is-integrated)
- [C++ projects and health models](#c-projects)
- [KarnixOS x86_64 kernel foundation](#karnixos-x86_64-kernel-foundation)
- [KarnixOS terminal research viewer](#karnixos-terminal-research-viewer)
- [Spine analytics dashboard](#spine-analytics-dashboard)
- [Other repository projects](#other-repository-projects)
- [Data, models, and limitations](#data-models-and-limitations)
- [Detailed architecture and workflow guides](#detailed-architecture-and-workflow-guides)
- [Development guidance](#development-guidance)

## Start here

Choose the project you want to run. There is no repository-wide build command.
The following are the most direct starting points:

| Goal | Start with |
| --- | --- |
| Explore C++ math, calculus, graphs, and autodiff | [Mathematical demo](#mathematical-demo) |
| Train/evaluate or visualize the brain-tumour research model | [Brain-tumour project](#brain-tumour-mri) |
| Run the spine image screening CLI | [Spine Edge CLI](#spine-edge-spine-image-research) |
| Route an image to one of the available disease-specific C++ models | [Unified medical predictor](#unified-medical-prediction-entry-point) |
| Boot the KarnixOS keyboard-operated text desktop in QEMU | [KarnixOS x86_64 kernel](#karnixos-x86_64-kernel-foundation) |
| Open the ncurses record viewer | [KarnixOS terminal viewer](#karnixos-terminal-research-viewer) |
| Run the browser-based spine analytics app | [Spine analytics dashboard](#spine-analytics-dashboard) |

Most commands below are written to run from the repository root unless a
section explicitly says to change directories.

## Project map

| Path | Purpose |
| --- | --- |
| `main.c++`, `mathematical/` | Standalone C++ math, calculus, visualization, and autodiff demonstrations |
| `neural_network/` | Reusable C++ tensor, layer, activation, loss, optimizer, and computational-graph code |
| `llm/` | Hosted C++20 client for local-only Ollama chat; launched from the ncurses viewer |
| `Brain_tumour/` | Four-class brain MRI research model, training/evaluation CLI, and optional visualizer |
| `Other_Disease/` | C++ medical-image experiments and the unified prediction executable |
| `karnix_spine_edge/` | Spine image research CLI, model helpers, sample data, and voice wrapper |
| `spine_analytics_dashboard/` | Separate Vite/React frontend and Express/PostgreSQL backend |
| `Karnix_Web/` | Separate web application; consult its own README and environment examples |
| `spine_3d/` | C++ spine visualization experiment |
| `os/main.c`, `os/Makefile` | ncurses terminal research-record viewer called KarnixOS |
| `os/karnix_kernel/` | Existing separate 32-bit bare-metal kernel experiment |
| `os/karnix64/` | Freestanding x86_64 C++20 kernel with a DOS/TempleOS-inspired blue-and-white framebuffer desktop, keyboard program menu, text terminal, and host-bridged MedGemma chat |
| `private/os copy/` | Another independent OS experiment |
| `NEWP/`, `jester/`, `Rust/` | Independent experiments; use their local project instructions |
| `data/` | Shared/sample data and image assets used by some projects |

## What is integrated

The repository is a set of projects, not one combined application. These are
the actual connections between its major components:

| Source | Connects to | How the connection works | Current boundary |
| --- | --- | --- | --- |
| `os/main.c` ncurses viewer | Spine records and UCI sample data | Reads repository-relative JSON/CSV files when present | Viewer and datasets are hosted; records are not connected to real patients |
| ncurses viewer | `Other_Disease/build/medical_screening` | `predict <disease> <image>` starts the separate predictor executable | Hosted process integration; model checkpoint must exist and match the task |
| ncurses viewer | `llm/build/karnix_llm` | `llm` command starts the C++ client as a child process | Hosted only; client sends requests to Ollama on loopback |
| `os/karnix64/` QEMU guest shell | `llm/build/karnix_qemu_medgemma_bridge` | `ask <prompt>` uses the emulated COM1 serial port connected to a host-private Unix socket | Text only; model inference runs in host Ollama, not in the guest |
| host bridge / C++ LLM client | Ollama and MedGemma | HTTP requests to `127.0.0.1:11434`; model is selected by `KARNIX_MODEL` | Local host service; no cloud API in this path; guest cannot invoke shell/tools |
| `os/karnix_kernel/` 32-bit kernel | — | No host-model or dashboard bridge | Independent kernel experiment |
| `spine_analytics_dashboard/` | Its own Express/PostgreSQL backend | Separate frontend/backend configuration and API | Does not invoke the C++ predictors or Ollama automatically |
| `Brain_tumour/`, `Other_Disease/`, `karnix_spine_edge/` | Their own data, checkpoints, CLIs, and optional visualizers | Separate CMake/manual build workflows; predictor routes are exposed to the ncurses viewer where documented | Not one shared model; modalities/checkpoints must not be interchanged |
| `Karnix_Web/`, `jester/`, `spine_3d/`, `NEWP/`, `Rust/`, `private/os copy/` | Their respective runtimes | Separate applications and experiments | No automatic connection to KarnixOS, MedGemma, or the medical predictor |

In brief, **the hosted ncurses viewer is the most connected application**.
The x86_64 kernel is a separate QEMU-booted, DOS/TempleOS-inspired text
desktop with an optional host-side chat bridge. Neither the old 32-bit kernel
nor the medical model executables are running inside the x86_64 kernel.

## Requirements

Install only the tools required by the component you plan to build:

- A C++14-capable compiler (Apple Clang or GCC) for the C++14 projects.
- CMake. `Other_Disease/` requires CMake 3.16 or newer; the local LLM project
  requires CMake 3.20 or newer; the brain and spine projects provide their own
  CMake files and targets.
- ncurses and a C compiler for the terminal viewer. On macOS, the system
  toolchain generally provides ncurses.
- `x86_64-elf-gcc`, NASM, Limine, QEMU, xorriso, CMake, libcurl, and
  nlohmann/json to build the x86_64 kernel and its optional host chat bridge.
- Ollama and an already installed compatible model to use `ASK` from that
  kernel's QEMU window. The default model is `medgemma1.5:4b`; inference runs
  on the host Mac, not inside the guest.
- Node.js and npm for the JavaScript web projects.
- PostgreSQL only for dashboard/backend configurations that use a database.
- SDL2 and SDL2_ttf are optional and needed only for the brain-tumour native
  visualizer when that target is enabled.
- Python and macOS speech tools are optional dependencies for some voice
  control workflows; core C++ model inference does not require Python.

## C++ projects

### Mathematical demo

Build and run from the repository root:

```bash
clang++ -std=c++14 -I. main.c++ -o /tmp/karnix_demo
/tmp/karnix_demo
```

The demo covers vector/matrix operations, trace/rank/eigen and projection
examples, elementary and multivariate calculus, differentiation/integration,
gradients/Jacobians/Hessians, ASCII graphs, computational graphs,
backpropagation, and activation gradients. It is an educational demonstration,
not a packaged library or production training system.

The reusable implementations are under `mathematical/` and
`neural_network/`. `neural_network/cnn/CNNArchitecture.hpp` is a generic CNN
building block; it is not itself a trained disease-specific model.

### Brain tumour MRI

The `Brain_tumour/` project implements a four-class C++ CNN with these labels:

- `glioma_tumor`
- `meningioma_tumor`
- `no_tumor`
- `pituitary_tumor`

It uses grayscale preprocessing and resizes images to 64 x 64. The project
supports direct PPM/PGM/BMP decoding and uses Apple's ImageIO/CoreGraphics for
PNG/JPEG/TIFF on macOS. It does not require Python.

Build the CLI:

```bash
cmake -S Brain_tumour -B /tmp/karnix-brain-build
cmake --build /tmp/karnix-brain-build --target brain_tumour_cli
```

Training and evaluation require the project's manifest and image data:

```bash
/tmp/karnix-brain-build/brain_tumour_cli \
  --train \
  --manifest Brain_tumour/data/brain_tumor/manifest.csv \
  --epochs 12 \
  --batch-size 16 \
  --learning-rate 0.0005 \
  --min-train-seconds 30 \
  --checkpoint-dir Brain_tumour/models/checkpoints \
  --model Brain_tumour/models/brain_tumour_cnn.weights \
  --verbose

/tmp/karnix-brain-build/brain_tumour_cli \
  --evaluate \
  --manifest Brain_tumour/data/brain_tumor/manifest.csv \
  --model Brain_tumour/models/brain_tumour_cnn.weights \
  --output Brain_tumour/data/brain_tumor/predictions.json \
  --verbose
```

The native scan-review visualizer is optional. Its CMake target is enabled on
macOS when SDL2 and SDL2_ttf are found:

```bash
cmake --build /tmp/karnix-brain-build --target brain_tumour_visualizer
/tmp/karnix-brain-build/brain_tumour_visualizer \
  Brain_tumour/data/brain_tumor/Testing \
  Brain_tumour/models/brain_tumour_cnn.weights
```

The checked-in README reports 1,000 images (800 train/200 test) and a 44%
held-out accuracy result for the documented 12-epoch run, despite a mean
top-1 confidence of 89.07%. This illustrates why confidence is not accuracy.
This is a research checkpoint with poor generalization, not a clinical tool. See
[Brain_tumour/README.md](Brain_tumour/README.md) and
[Brain_tumour/STATUS.md](Brain_tumour/STATUS.md) for dataset provenance,
commands, logs, and the project's current evaluation details.

### Other_Disease C++ experiments

`Other_Disease/` builds separate executables for lung X-ray baseline
classification, osteoarthritis, brain tumour, lumbar stenosis, and the unified
predictor. Build all targets from the repository root:

```bash
cmake -S Other_Disease -B Other_Disease/build
cmake --build Other_Disease/build --config Release
```

The disease-specific executables accept a `predict` command and explicit
checkpoint and image paths. Example:

```bash
Other_Disease/build/brain_tumor_ml predict \
  --model Brain_tumour/models/brain_tumour_cnn.weights \
  --image /path/to/brain_mri.png

Other_Disease/build/osteoarthritis_ml predict \
  --model /path/to/compatible_osteoarthritis.weights \
  --image /path/to/xray.png

Other_Disease/build/lumbar_stenosis_ml predict \
  --model /path/to/compatible_lumbar_mri.weights \
  --image /path/to/spine_mri.png
```

Successful outputs are JSON and explicitly mark supported predictions as
`research_only:true` and `diagnosis:false`. The executable needs a compatible
checkpoint; the existence of a target or a weight file does not establish
clinical validity.

The arthritis-specific image path here is osteoarthritis only; it is not a
general model for every type of arthritis.

#### Lung X-ray baseline

`lung_xray_ml` is a separate C++17 logistic-regression baseline using a 32 x 32
grayscale feature grid. Dataset download/training instructions and label
mapping are in [Other_Disease/README.md](Other_Disease/README.md). The dataset
is not included as a ready-to-train Kaggle download; obtain it through the
dataset's authorized channel and inspect its folder labels before training.
Do not infer labels silently from filenames.

#### Unified medical prediction entry point

`medical_screening` routes one image to a model for exactly one of these
tasks. It does not combine modalities or automatically validate checkpoint
provenance.

| `--disease` | Required input | Class output | Default checkpoint expected by KarnixOS |
| --- | --- | --- | --- |
| `brain_tumor` | Brain MRI | Glioma, meningioma, no tumour, or pituitary tumour | `Brain_tumour/models/brain_tumour_cnn.weights` |
| `osteoarthritis` | X-ray | Osteoarthritis / no osteoarthritis | `karnix_spine_edge/models/osteoarthritis_cnn.weights` |
| `osteoporosis` | DEXA | Osteoporosis / normal BMD | `karnix_spine_edge/models/dexa_cnn.weights` |

Run the predictor directly:

```bash
Other_Disease/build/medical_screening predict \
  --disease brain_tumor \
  --model Brain_tumour/models/brain_tumour_cnn.weights \
  --image /path/to/brain_mri.png

Other_Disease/build/medical_screening predict \
  --disease osteoarthritis \
  --model /path/to/compatible_osteoarthritis.weights \
  --image /path/to/xray.png \
  --modality XRAY

Other_Disease/build/medical_screening predict \
  --disease osteoporosis \
  --model /path/to/compatible_dexa.weights \
  --image /path/to/dexa_scan.png \
  --modality DEXA
```

The modality option is optional; if supplied, it must match the disease
(`MRI`, `XRAY`, or `DEXA`). The model file must exist, be loadable by the
corresponding C++ model, and have the expected output shape. The binary
checkpoint format does not prove which task it was trained for: do not
substitute another disease's weights.

**Osteoporosis is not currently ready for real inference.** No verified,
labeled DEXA training dataset or DEXA checkpoint is provided. The osteoporosis
route exists to enforce the correct DEXA modality and to fail clearly when a
compatible model file is unavailable; it does not create a prediction from
pixels or substitute an osteoarthritis model. Do not treat a newly supplied
checkpoint as validated just because it loads.

For details on the lung baseline and unified entry point, see
[Other_Disease/README.md](Other_Disease/README.md).

### Spine Edge spine-image research

`karnix_spine_edge/` contains the offline spine CLI, model helpers, training
and evaluation flows, UCI vertebral sample handling, record output, and
optional voice wrapper. Consult
[karnix_spine_edge/readme.md](karnix_spine_edge/readme.md) before running a
workflow; optional model and voice dependencies vary by command.

Build with CMake:

```bash
cmake -S karnix_spine_edge -B /tmp/karnix-spine-build
cmake --build /tmp/karnix-spine-build
```

Or use the manual C++14 build documented by the project:

```bash
cd karnix_spine_edge
clang++ -std=c++14 -O3 spine_cli.c++ -o /tmp/spine_cli
/tmp/spine_cli
cd ..
```

The CLI can write screening records to
`karnix_spine_edge/data/spine_records.json`; the KarnixOS viewer reads that
path when launched from the repository root. Sample/test data must not be
mistaken for patient cases. The optional voice wrapper is documented in the
component README and can be inspected with:

```bash
cd karnix_spine_edge
./voice.sh help
cd ..
```

The DEXA/osteoporosis workflow uses a separate DEXA checkpoint path and
rejects an input-modality mismatch. The repository does not contain a
validated DEXA dataset/model. Feature-based or simulation modes, if enabled
for development, are not clinical image inference. See
[karnix_spine_edge/models/README.txt](karnix_spine_edge/models/README.txt) for
the optional ONNX model notes and
[karnix_spine_edge/readme.md](karnix_spine_edge/readme.md) for the current
CLI behavior.

## KarnixOS x86_64 kernel foundation

The new `os/karnix64/` target is a separate freestanding C++20 x86_64 kernel.
Limine loads it using Multiboot2; it enters long mode, maps the first 4 GiB,
and requests a 1024x768x32 framebuffer. Supported RGB modes render an original
DOS/TempleOS-inspired blue-and-white text desktop, with VGA text output as a
fallback. Press a number key to launch an app; press **Esc** from the command
terminal to return to the home menu. The shell provides the existing command
terminal and local-AI chat workflow. It is not MS-DOS, TempleOS, or a Linux
desktop; mouse input, movable windows, a filesystem, image viewer, and CNN Lab
are not implemented. Unavailable research apps are labelled instead of
showing fabricated results. The existing 32-bit kernel and hosted ncurses
viewer are not replaced.

Build and boot-test the kernel and shell:

```bash
make -C os/karnix64 build
make -C os/karnix64 check
make -C os/karnix64 check-shell
make -C os/karnix64 check-graphics
make -C os/karnix64 check-desktop
```

To test real MedGemma chat through a QEMU guest, start Ollama on the host,
ensure the model is installed, then run:

```bash
ollama serve
ollama pull medgemma1.5:4b
make -C os/karnix64 check-chat
```

`check-chat` types a prompt through the emulated guest keyboard, sends it
through the private serial bridge to local Ollama, and checks that a reply
returns to the guest. To open the interactive OS window:

```bash
make -C os/karnix64 run
```

At boot, press **1–6** to select an app; **A** highlights the program menu.
**T** opens the terminal directly. Press **Esc** in the terminal to return to
the home menu, or type `DESKTOP`. Terminal and Math Lab use the working shell.
Local AI opens the terminal with instructions for the existing `ASK <prompt>`
command. Medical Imaging and CNN Lab currently show explicit not-available
messages.

The QEMU run builds a host companion and uses a per-run, current-user-only
Unix socket connected to the guest COM1 serial port. Click inside the QEMU
window to give it keyboard focus. Try `help`, `status`, `echo hello`,
`calc 2+3*4`, or `ask What can you do?`. `ASK` currently supports text only.
MedGemma runs in Ollama on the host Mac; the guest does not contain or execute
the model. Directly booting the ISO has no host bridge.

Run the repository's `./setup.sh` to install required Homebrew tools,
build/test the kernel and host bridge, and open QEMU. Interactive chat needs
local Ollama and the configured model; setup does not automatically download
the model. Set `KARNIX_ROOT` to use a different checkout.

The guest still lacks an allocator, filesystem/model loader, userspace,
network stack, multi-window manager, and native inference engine. See
[os/karnix64/README.md](os/karnix64/README.md) for target details.

## KarnixOS terminal research viewer

`os/main.c` builds a local ncurses **terminal viewer**. Despite its name, this
program is neither the separate bare-metal kernel in `os/karnix_kernel/` nor
a diagnostic operating system.

Build to the default repository-root executable and run it from the repository
root (its data/model paths are relative to that directory):

```bash
make -f os/Makefile
./KarnixOS
```

To build without putting the executable in the repository:

```bash
make -f os/Makefile OUT=/tmp/KarnixOS
/tmp/KarnixOS
```

Use a terminal at least 80 columns by 24 rows. The viewer reads
`karnix_spine_edge/data/spine_records.json` and
`karnix_spine_edge/data/uci_vertebral_samples.csv` when available. It does not
invent replacement records when files are absent. The UCI values are a
separate sample set and are not linked to an individual screening record.

### KarnixOS controls and commands

- `j` / Down: select the next matching record.
- `k` / Up: select the previous matching record.
- Page Up / Page Down: scroll the record list.
- `search <text>`: filter loaded records; `search clear` clears the filter.
- `dir`: show loaded record and UCI row counts.
- `uci`: inspect the first loaded UCI sample row.
- `scan`: invoke the configured osteoarthritis scan command.
- `train`: invoke the configured OA training/evaluation command.
- `brain` / `brain_tumour`: launch the brain MRI visualizer workflow.
- `predict <disease> <image-path>`: invoke the unified predictor for
  `brain_tumor`, `osteoarthritis`, or `osteoporosis`.
- `find <text>`: alias for `search <text>`; `find clear` also clears the filter.
- `select <index>`: select a loaded record by zero-based index.
- `up` / `down`: aliases for record navigation.
- `clear`: clear the viewer's log panel.
- `help`: show the viewer's command list.
- `quit`, `q`, `exit`, or Ctrl+Q: exit.

The `predict` command requires
`Other_Disease/build/medical_screening` to have been built. Its checkpoint
paths are listed in the disease table above. In this checkout the DEXA
checkpoint is absent, so osteoporosis prediction must fail rather than
fabricate a result. The OA checkpoint path configured in KarnixOS is
`karnix_spine_edge/models/osteoarthritis_cnn.weights`. That exact file is not
present in this checkout. A separate `osteoarthritis_cnn.baseline.weights`
artifact exists, but KarnixOS does not select it automatically; only use it
after confirming it is the correct compatible task checkpoint. If the
configured path is missing, the viewer reports that error. Review all
predictions as experimental model output, not diagnosis.

See [os/Makefile](os/Makefile) and
[karnix_spine_edge/readme.md](karnix_spine_edge/readme.md) for build and
component-specific details.

## Local LLM from KarnixOS

The ncurses viewer can launch a separate hosted C++20 client that talks to
Ollama only through its local HTTP API at `127.0.0.1:11434`. It does not use a
cloud model, API key, shell/tool execution, or model-controlled OS actions.
The hosted ncurses viewer can start it directly. The x86_64 QEMU shell can
also send `ask <prompt>` through a private serial socket to a separate host
companion, which calls Ollama on loopback. MedGemma inference runs on the Mac,
not inside the kernel. The 32-bit kernel has no such bridge.

The C++ client uses system libcurl and the nlohmann/json header library. Both
are free/open-source; libcurl uses its MIT-style curl license and
nlohmann/json is MIT-licensed. The CMake configure step requires CMake 3.20+,
libcurl, and nlohmann/json 3.11+ to be discoverable by CMake.

Build the companion and its tests:

```bash
cmake -S llm -B llm/build -DCMAKE_BUILD_TYPE=Release
cmake --build llm/build --parallel
ctest --test-dir llm/build --output-on-failure
```

Check actual local readiness:

```bash
./llm/build/karnix_llm --status
```

Ollama is installed in this development environment. Start its local service
if needed and explicitly pull the selected MedGemma model:

```bash
ollama serve
ollama pull medgemma1.5:4b
```

The app never downloads a model automatically. `medgemma1.5:4b` is the
configured model (`KARNIX_MODEL` overrides it); it is the Ollama 1.5 4B tag,
not `medgemma:1.5`. Its registry artifact is about 3.34 GB, so check available
disk and memory before downloading. MedGemma is a medical text/image research
model and is **not clinically validated**; Google's Health AI Developer
Foundations terms and prohibited-use policy apply. Review and comply with
[those terms](https://developers.google.com/health-ai-developer-foundations/terms)
before use. The status command reports whether Ollama is reachable, which
models are installed, available memory and disk, and whether the configured
model is available. If you choose MedGemma 1 instead, the Ollama registry tag
is `medgemma:4b`; select it with `KARNIX_MODEL=medgemma:4b` or
`--model medgemma:4b`.

Run it directly:

```bash
./llm/build/karnix_llm --interactive
./llm/build/karnix_llm "Explain a matrix"
./llm/build/karnix_llm --model medgemma:4b "Explain a matrix"
./llm/build/karnix_llm --image /path/to/scan.png "Describe this image"
```

Or start the ncurses viewer from the repository root and enter `llm` for an
interactive session, `llm "Explain a matrix"` for one prompt, or `llm --status`
to check the runtime. Attach images with
`llm --image /path/to/scan.png "Describe this image"`; quote paths containing
spaces. Multiple `--image` flags may be used, up to four per prompt. In
interactive mode, enter `/image "/path/to/scan.png" [prompt]` to attach an
image; without a prompt the client asks for a description of visible content.
The interactive client also supports `/help`, `/clear`, `/model [name]`,
`/status`, and `/exit`.

PNG and JPEG files up to 10 MiB each are accepted. The client encodes the
selected image locally and includes it in the request sent only to the Ollama
service on localhost; it does not upload it to a cloud endpoint. Text
conversation history is bounded to 16,000 characters, and up to four image
attachments are retained for follow-up turns. Other models must support
vision for image prompts to work.

The C++ client unit tests use a local mock HTTP server and do not replace real
inference verification. Image descriptions are model output, not a diagnosis
or a substitute for clinical review; generation success does not establish
medical accuracy or suitability.
MedGemma's marked internal-thought sections are suppressed from the streamed
display and saved conversation history. Real local generation, two-turn
history, image description, and image inference with outbound networking
blocked (except localhost Ollama) have been verified. These checks test
runtime integration, not medical accuracy or clinical suitability.
See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the repository audit,
integration boundary, build results, and remaining blockers.

## Detailed architecture and workflow guides

- [Verified architecture](KARNIX_ARCHITECTURE.md)
- [Build commands and setup](KARNIX_BUILD.md)
- [Local LLM and image input](KARNIX_LOCAL_LLM.md)
- [Build/test record](KARNIX_TESTING.md)
- [Module inventory](KARNIX_MODULES.md)
- [Detailed implementation audit](docs/ARCHITECTURE.md)

## Spine analytics dashboard

`spine_analytics_dashboard/` is independent from the C++ predictor and has a
Vite/React frontend plus an Express backend. It does not automatically expose
the C++ model executables as an API.

Install dependencies in each app:

```bash
cd spine_analytics_dashboard/backend
npm install

cd ../frontend
npm install
```

Run the backend and frontend in separate terminals:

```bash
# Terminal 1
cd spine_analytics_dashboard/backend
npm start

# Terminal 2
cd spine_analytics_dashboard/frontend
npm run dev
```

Configure environment/database settings using the backend's example/config
files. The Vite server prints its local URL after starting. Available frontend
commands include `npm run build` and `npm run preview`; backend scripts include
`npm run dev` and `npm run import:karnix`. The backend's current `npm test`
script is a placeholder that exits with an error, not a test suite.

Further reading:

- [spine_analytics_dashboard/README.md](spine_analytics_dashboard/README.md)
- [spine_analytics_dashboard/QUICKSTART.md](spine_analytics_dashboard/QUICKSTART.md)
- [spine_analytics_dashboard/API_REFERENCE.md](spine_analytics_dashboard/API_REFERENCE.md)

The separate [Karnix_Web/](Karnix_Web/) project has its own README and
environment examples. Do not copy real secrets into source control; use its
documented local environment setup.

## Other repository projects

These projects are separate and are not part of the health C++ build:

- `spine_3d/`: C++ spine visualization experiment.
- `os/karnix_kernel/`: separate bare-metal kernel sources and build artifacts.
- `private/os copy/`: another OS experiment.
- `NEWP/`: independent experiments with local READMEs where available.
- `jester/`: independent application project.
- `Rust/my_project/`: Rust experiment.
- `new.c++` and other root-level files: standalone experiments; inspect their
  source/build instructions before compiling.

Use each project's own build files and README. Do not assume the root C++
commands build these projects.

## Data, models, and limitations

- Model files are task-specific. MRI, X-ray, and DEXA are different modalities
  and must not share checkpoints by assumption.
- A checkpoint being present, or producing a numeric score, does not establish
  correct training, calibration, clinical validity, or safety.
- The brain tumour README documents 44% held-out accuracy for its current
  12-epoch run, indicating that checkpoint is not suitable for clinical use.
- No DEXA/osteoporosis checkpoint or verified training dataset is included.
- Some sample records and sample datasets are for software testing and
  demonstration only. Do not represent them as real patient records.
- Dataset licenses, provenance, label correctness, patient-level separation,
  and privacy protections must be reviewed before training or sharing data.
- Before any clinical use could be considered, the model would require
  appropriate independent validation, calibration, subgroup evaluation,
  clinician review, and applicable regulatory/privacy controls.
- No browser frontend should call local model binaries directly. Any future
  API integration needs its own authentication, authorization, validation,
  and privacy/security review.

## Development guidance

- Build only the component you are working on; projects have different
  standards, dependencies, and runtimes.
- Keep build directories and generated outputs within their respective
  component directories or `/tmp`.
- Review a component's README before downloading datasets, enabling optional
  integrations, or using external data.
- Preserve the research-only labels in model outputs and user interfaces.
- Never interpret output probabilities as a diagnosis or calibrated clinical
  risk.
