# KARNIX Build Guide

Karnix has no single repository-wide build. Configure and build only the
component you intend to use. Commands below assume macOS and a repository-root
working directory unless otherwise noted.

## One-command KarnixOS setup on macOS

Run the repository's setup script:

```bash
./setup.sh
```

It installs missing Homebrew packages (`x86_64-elf-gcc`, `limine`, `qemu`,
`nasm`, `xorriso`, `cmake`, `nlohmann-json`, `ollama`, `sdl2`, and
`sdl2_ttf`), builds the separate x86_64 kernel, creates a Limine ISO, builds
the brain MRI and CNN host visualizers, and verifies boot, desktop
initialization, and keyboard app selection in QEMU, then opens a QEMU window
with a host-side bridge for the shell's `ask` command and visualizer launch
requests. Ollama must be running and the selected model installed to launch the
interactive desktop; the script does not download models. With an interactive
terminal, `./setup.sh` launches the desktop after the checks pass. Without
one, it prints the command to launch QEMU manually. By default, the checkout
is the directory containing the script; set `KARNIX_ROOT` to use another
checkout. The script is macOS-specific and refuses unsupported operating
systems rather than guessing commands. MedGemma inference runs in host
Ollama, not in the guest.

## Hosted C++ local LLM and viewer

Prerequisites: C++20 compiler, CMake 3.20+, libcurl, nlohmann/json 3.11+,
Ollama, and ncurses for the terminal viewer. The selected Mac has these build
dependencies installed.

```bash
cmake -S llm -B llm/build -DCMAKE_BUILD_TYPE=Release
cmake --build llm/build --parallel
ctest --test-dir llm/build --output-on-failure

cmake -S Other_Disease -B Other_Disease/build -DCMAKE_BUILD_TYPE=Release
cmake --build Other_Disease/build --target medical_screening --parallel

make -f os/Makefile
./KarnixOS
```

The viewer must run from the repository root because its data/model paths are
relative. The `llm` viewer command launches the hosted C++ process.

## Existing C++ targets

Build in `/tmp` to avoid replacing existing project outputs:

```bash
cmake -S Brain_tumour -B /tmp/karnix-brain-build
cmake --build /tmp/karnix-brain-build --target brain_tumour_cli

cmake -S Other_Disease -B /tmp/karnix-other-disease-build
cmake --build /tmp/karnix-other-disease-build --parallel

cmake -S karnix_spine_edge -B /tmp/karnix-spine-build
cmake --build /tmp/karnix-spine-build --target spine_cli mura_pipeline --parallel

clang++ -std=c++14 -I. main.c++ -o /tmp/karnix-math-demo
```

Native visualizers require SDL2 and SDL2_ttf. The `host-apps` target builds
the existing brain-tumour MRI viewer and the osteoarthritis CNN activation
viewer for use from the QEMU desktop.

## Freestanding kernel targets

The isolated C++20 x86_64 kernel and text shell can be built and boot-tested
with:

```bash
make -C os/karnix64 build
make -C os/karnix64 check
make -C os/karnix64 check-shell
make -C os/karnix64 check-chat
make -C os/karnix64 run
```

It uses `x86_64-elf-g++`, `x86_64-elf-ld`, NASM, Limine, xorriso, and
`qemu-system-x86_64`. `run` builds the host-side
`karnix_qemu_medgemma_bridge` from `llm/` and the existing brain MRI and CNN
visualizers. The bridge connects to the guest over a private Unix socket
attached to COM1. Start Ollama and install MedGemma on the Mac before launching; then
click the QEMU window and use `ask <prompt>`. In the QEMU window, `help`,
`status`, and `calc 2+3*4` work without a model. The shell is
DOS/TempleOS-inspired, not an exact replica, and is text-only; it does not
provide full HolyC. This milestone enables long mode and establishes an
identity map for the first 4 GiB; it does not yet provide a heap,
disk/filesystem, userspace, network stack, or in-guest LLM runtime.
`make -C os/karnix64 check-chat` runs a real local model prompt through QEMU;
it requires the selected Ollama model and a running local service.

The existing i386 kernel remains separate and unchanged. Follow
[`os/karnix_kernel/`](os/karnix_kernel/) instructions for its own cross-build.

Web, Rust, Electron, Python, and other experimental projects retain their
own README files, manifests, toolchains, and build instructions. This guide
does not claim that they share one build or have all been validated.

## Dependencies and licensing

- CMake and the selected compiler are free build tools.
- libcurl uses the curl license (MIT-style); see
  [curl's license information](https://curl.se/docs/copyright.html).
- nlohmann/json is MIT-licensed; see
  [its license](https://github.com/nlohmann/json/blob/develop/LICENSE.MIT).
- Limine is BSD-2-Clause; the x86_64 cross-compiler toolchain is GCC with its
  GCC Runtime Library Exception. NASM, QEMU, and xorriso have separate
  upstream licenses; review their package/license notices when distributing.
- Ollama is a local inference runtime; install from
  [Ollama's official download page](https://ollama.com/download) and review
  its terms and the selected model terms separately.
- MedGemma is governed by Google's Health AI Developer Foundations terms and
  prohibited-use policy. No paid API or cloud inference is required by the
  hosted Karnix LLM path.
