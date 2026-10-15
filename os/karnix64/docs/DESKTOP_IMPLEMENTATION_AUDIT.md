# KarnixOS Desktop Implementation Audit

Audit date: 2026-10-10

## Scope and repository state

This audit covers the current `os/karnix64/` boot/runtime path, its build and
QEMU checks, the existing host-side Ollama connection, and neighboring
medical-image projects that are not yet integrated with the desktop. The
repository had pre-existing staged/unstaged and untracked work before this
milestone. Those changes were left intact; this milestone updates the x86_64
kernel, its existing host bridge, and directly related documentation.

## Existing kernel before the graphics milestone

- `Limine` boots the ELF through its **Multiboot2** entry, not the native
  Limine request protocol. `limine.conf` names the `multiboot2` protocol.
- `boot.asm` starts in 32-bit protected mode, creates page tables, enables
  x86_64 long mode, and calls the C++ entry point. Before this milestone it
  discarded the Multiboot2 registers and identity-mapped the first GiB using
  2 MiB pages.
- The kernel is freestanding C++20 with exceptions, RTTI, stack protectors,
  PIC/PIE, and thread-safe statics disabled. It had no heap, general-purpose
  allocator, filesystem, userspace, interrupt setup, or C++ standard-library
  runtime.
- The visible interface was an 80x25 VGA text console with a polled PS/2
  keyboard. Mouse input and a window/event system were absent.
- COM1 carries boot/test markers and the existing chat protocol. In
  interactive QEMU, the guest sends `ASK` text over COM1 to a per-run,
  host-local Unix socket. The C++ host companion connects to Ollama on
  loopback; model inference does not run in the guest.

## Implemented graphics and desktop milestone

The kernel now asks Multiboot2 for a 1024x768x32 framebuffer using an optional
framebuffer header tag. It validates the boot-information bounds, framebuffer
tag, RGB pixel layout, dimensions, pitch, alignment, and complete
pitch-by-height physical range before writing. Supported framebuffers must be
32-bit RGB, no larger than 4096x2160, with the complete buffer below 4 GiB.
Unsupported or absent framebuffers leave the VGA console path available.

The kernel renders the desktop and existing text shell into the framebuffer
with clipped drawing operations and a small MIT-licensed, project-authored
5x7 bitmap glyph set; VGA text memory is still updated too. The bootstrap now
identity-maps the first 4 GiB with 2 MiB pages to cover the QEMU framebuffer
address used by this configuration. SIMD use is disabled in the freestanding
C++ build because the kernel does not initialize or preserve SIMD/FPU state.

The desktop is an original, keyboard-operated cavOS/TWM-inspired interface
with a teal root background, square bordered windows, an icon manager/program
menu, and a workspace information panel. Its palette follows the local cavOS
`.xinitrc` and `.twmrc`; no X11 server or cavOS kernel code is integrated.
`A` opens the menu, `N` or `Tab` moves its selection, `Enter` launches the
selected app, and number keys launch apps directly. The
Terminal and Math Lab are not listed as desktop applications. Local AI opens a
chat-focused screen that uses the host Ollama path; `Esc` returns to the
desktop.
Medical Imaging and CNN Lab send fixed, allowlisted launch messages through
the private COM1 bridge. The host bridge starts the existing brain MRI
visualizer or osteoarthritis CNN activation visualizer in a native host
window. This is a host-side integration, not guest filesystem access or
guest-native medical inference.
Patient Records is a native guest app for signing in, listing, and creating
lung-risk assessments. It sends fixed protocol messages over the private
COM1 bridge; the host client calls the existing authenticated API, keeps its
cookie session in memory, and restricts the API URL to HTTP on localhost.
The guest has no direct database or network access. The host records API
must be running separately; this app does not launch a browser.
This is not MS-DOS, TempleOS, or a Linux desktop installation. There is no
pointer, window manager, files app, native image viewer, or guest medical
inference. The QEMU keyboard check demonstrates the application overview,
Local AI launch, keyboard prompt input, and Escape-to-desktop navigation; it
does not demonstrate guest medical inference. The Patient Records integration check runs a QEMU
guest against a synthetic authenticated API and verifies rejected and
successful login, listing, assessment creation, logout, and rejection of
non-local API URLs. It does not access a production account or database.

## Existing hosted research modules and compatibility

- `Brain_tumour/BrainTumourCNN.hpp` implements a four-class C++ CNN. Its
  preprocessing resizes a grayscale image to 64x64 and scales values to
  approximately [-1, 1]. The output order is `glioma_tumor`,
  `meningioma_tumor`, `no_tumor`, `pituitary_tumor`. The current documented
  held-out result is 44.00% on 200 images. The model uses hosted C++
  containers and file I/O. The implementation's `forward()` returns final
  probabilities; unlike SpineCNN, it does not currently expose a per-layer
  forward trace.
- `karnix_spine_edge/medical/SpineCNN.hpp` is a separate C++ CNN with a real
  `forward_with_trace()` path used by `cnn_visualizer.c++`. It uses hosted
  containers and filesystem/image-decoding facilities. Its source describes
  a 1x128x128 grayscale input; the same header's current preprocessing
  constants and resize implementation need to be checked together with the
  active checkpoint before freezing a bridge tensor contract.
- The documented OA checkpoint is present and its held-out result is
  59.17% accuracy, 75.00% sensitivity, and 43.33% specificity on 120 images.
  Separate MRI checkpoint files are present. Model filenames and the
  checkpoint format do not independently prove task provenance.
- The DEXA import path exists, but there is no verified labeled DEXA image
  dataset or bundled osteoporosis checkpoint. Do not enable osteoporosis
  inference by substituting weights from another task.
- `Other_Disease/medical_screening.cpp` is a hosted executable. It validates
  the requested modality and missing paths, then calls the existing
  brain-tumour or binary SpineCNN implementation. It does not run in the
  freestanding guest, does not validate checkpoint task provenance, and does
  not provide the guest with intermediate tensors.
- The old optional ONNX model notes in
  `karnix_spine_edge/models/README.txt` are not an active ONNX integration:
  the current `mura_pipeline.c++` identifies its model as custom SpineCNN,
  not ResNet50/ONNX. Treat the ONNX notes as setup guidance, not evidence of a
  working predictor.
- Image decoding is component-specific and hosted. The brain model has
  native PPM/PGM/BMP decoding and Apple ImageIO paths; SpineCNN has native
  PPM/PGM/BMP decoding and a macOS `sips` path for JPEG/PNG/TIFF. Neither path
  can be linked into this freestanding kernel as-is.
- Checkpoint files are present for brain MRI, OA, and lumbar MRI in the
  checkout, but their task compatibility must be established by loading and
  testing the exact artifacts before marking a model ready. No verified DEXA
  dataset or DEXA checkpoint was found.
- `spine_3d/`, the ncurses viewer, and browser applications remain separate
  hosted projects; no automatic connection from them to this kernel exists.

## Next compatible stages

1. Keep the guest responsible for rendering, keyboard/pointer input, bounded
   UI state, and truthful status/errors.
2. Extend the renderer and input/event layer before adding a window manager or
   multiple applications. Preserve VGA fallback and a recoverable terminal.
3. Keep image decoding and hosted model execution in an explicitly allowlisted
   C++ host companion. Extend the existing private serial/Unix-socket path
   with a versioned, bounded protocol only when the guest UI reaches those
   features; preserve the current text-chat path.
4. Validate task, modality, checkpoint, dimensions, and buffer sizes on both
   sides. Return real traced tensors only for models that expose them. Do not
   serialize checkpoints into guest memory or accept arbitrary host commands.
5. Add future desktop and hosted-application capabilities in stages, with
   QEMU checks at each stage. Treat a fully guest-native inference runtime as
   a separate project milestone.

## Baseline and milestone verification

Before code changes, on the current dirty worktree:

| Check | Result | Evidence |
| --- | --- | --- |
| `make -C os/karnix64 check` | PASS | Boot marker, shell marker, and expected QEMU debug-exit status |
| `make -C os/karnix64 check-shell` | PASS | Emulated keyboard entered `calc 2+3*4`; guest reported `14` |
| `make -C os/karnix64 check-chat` | PASS | Local Ollama/MedGemma reply returned through the guest serial bridge |

After the graphics change:

| Check | Result | Evidence |
| --- | --- | --- |
| `make -C os/karnix64 build` | PASS | Freestanding C++20 kernel and graphics renderer linked successfully |
| `make -C os/karnix64 check` | PASS | Boot marker, shell marker, and expected QEMU debug-exit status |
| `make -C os/karnix64 check-shell` | PASS | Emulated keyboard opened Local AI, navigated the application overview, typed help, and returned to the desktop with Escape |
| `make -C os/karnix64 check-graphics` | PASS | QEMU reported `framebuffer=ready` and `desktop-ui=ready` |
| `make -C os/karnix64 check-desktop` | PASS | Graphics initialization and Escape-to-home keyboard path passed |
| `make -C os/karnix64 check-chat` | PASS | Local Ollama/MedGemma reply returned through the guest serial bridge |
| `cmake --build Other_Disease/build --target medical_screening --parallel` | PASS | Existing hosted medical router target built |
| QEMU `screendump` | PASS | Captured 1024x768 image was opened and inspected; it shows the GNOME-inspired framebuffer desktop |

After the native Patient Records integration:

| Check | Result | Evidence |
| --- | --- | --- |
| `make -C os/karnix64 check-patient-records` | PASS | QEMU sign-in (including rejected credentials), list, complete assessment creation, list-after-save, logout, and authenticated API calls against a synthetic API; non-local API URL forms rejected |
| `make -C os/karnix64 check-desktop` | PASS | Graphics, keyboard shell, visualizer launch, and native Patient Records checks passed |
| `make -C os/karnix64 check-chat` | PASS | Local Ollama/MedGemma reply returned through the guest serial bridge |
| `git diff --check` | PASS | No whitespace errors |

These checks do not exercise medical inference, CNN tracing, input devices
other than the PS/2 keyboard, a pointer, or a window manager.
