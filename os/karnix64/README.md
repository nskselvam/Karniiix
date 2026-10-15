# KarnixOS x86_64 Foundation

This is a new, isolated freestanding C++20 kernel target. It does not replace
`../karnix_kernel/` (the existing i386 experiment), the hosted ncurses viewer,
or the hosted Ollama client.

## Current milestone

- Boots as a Multiboot2 kernel from a Limine BIOS/UEFI hybrid ISO.
- Enters x86_64 long mode using a 32-bit assembly bootstrap.
- Establishes 2 MiB identity-mapped pages for the first 4 GiB so QEMU's
  framebuffer can be reached by this early kernel.
- Requests a 1024x768x32 framebuffer through the optional Multiboot2
  framebuffer tag. The VGA text output remains available when the bootloader
  does not provide a supported framebuffer.
- Boots to a cavOS/TWM-inspired graphical desktop: teal root background,
  square slate-bordered windows, an icon manager/program menu, and a workspace
  information panel. The menu lists five apps; `1`-`5` launches them, `A`
  opens the menu, `N` or `Tab` moves the highlight, and `Enter` launches the
  highlighted app. The Terminal and Math Lab are not desktop apps.
- Local AI opens a chat-focused screen for the existing `ASK <prompt>` Ollama
  bridge; each prompt includes Karnix project guidance and all 100 synthetic
  Patient Records demo examples as local context. `Esc` returns to the desktop.
  The chat does not receive real account records. Medical Imaging and CNN Lab
  send fixed launch requests over COM1 to open the existing brain MRI and
  osteoarthritis CNN visualizers as native host windows; neither runs inside
  the guest.
- Patient Records is a native guest app for entering and listing lung-risk
  assessments via the local authenticated API bridge. It also offers 100
  synthetic, local-only demo samples.
- The desktop and chat screen use clipped framebuffer rendering and a
  MIT-licensed, project-authored 5x7 bitmap font scaled for legibility.
  VGA text remains the fallback.
- Accepts polled PS/2 keyboard input in QEMU. Mouse input and interrupts are
  not implemented.
- Provides `HELP`, `ABOUT`, `STATUS`, `CLEAR`, `ECHO`, and `CALC` commands.
  `CALC` evaluates checked signed integer arithmetic with parentheses.
- Supports `ASK <prompt>` in the graphical QEMU run target. Prompts and
  replies travel over a private host-local Unix socket attached to the guest
  COM1 serial device; a host companion calls Ollama on loopback.
- Reports boot and shell initialization markers on COM1; the QEMU smoke test
  checks those markers and the expected debug-exit status.

This is an independently written, **cavOS/TWM-inspired** Karnix desktop, not
an X11/TWM port or a Linux installation. Its colors and classic window layout
are based on `../../cavOS/target/root/.xinitrc` and `.twmrc`; the renderer
continues to use the existing Karnix framebuffer, not cavOS kernel code.
It remains an early
milestone: there is no mouse, multi-window manager, guest filesystem,
guest-native medical imaging or CNN viewer, multitasking, or Linux userspace.
The 5x7 glyph
data is original to this project and licensed under the MIT License in
[FONT-LICENSE.txt](FONT-LICENSE.txt).

MedGemma inference is **not executed inside the bare-metal guest**. The
graphical QEMU launcher requires the host-side `karnix_qemu_medgemma_bridge`,
Ollama, and the configured model (default `medgemma1.5:4b`). It also builds
the existing SDL2 brain MRI and osteoarthritis CNN visualizers; pressing `2`
or `3` in the guest starts the matching host window through an explicit
allowlisted serial request. The bridge has no
TCP listener; its Unix socket is restricted to the current host user. Direct
boots of the ISO do not provide the chat bridge. There is no guest-side
physical memory manager/heap, disk/filesystem/model loader, userspace, network
stack, C++ standard library runtime, or inference engine. The kernel maps only
the first 4 GiB and currently supports 32-bit RGB framebuffers whose complete
pitch-by-height region is below that boundary. Other pixel formats and
out-of-range framebuffers use the VGA text fallback.

## macOS build and boot

Install the free cross-compiler, assembler, bootloader, emulator, and ISO tool
if they are not already present:

```bash
brew install x86_64-elf-gcc limine qemu nasm xorriso cmake sdl2 sdl2_ttf
```

Build the ELF, make the bootable ISO, boot-test it headlessly, or launch the
kernel screen in a QEMU window:

```bash
make -C os/karnix64 build
make -C os/karnix64 iso
make -C os/karnix64 check
make -C os/karnix64 check-shell
make -C os/karnix64 check-graphics
make -C os/karnix64 check-desktop-render
make -C os/karnix64 check-desktop
make -C os/karnix64 check-chat
make -C os/karnix64 run
```

`check-desktop-render` checks layout bounds and menu highlighting from the
minimum 480x200 framebuffer through 4096x2160, including compact layouts.
`check-graphics` verifies that QEMU supplies a supported Multiboot2 RGB
framebuffer and that the desktop initializes. `check-desktop` also checks app
launch and return to the desktop from Local AI. The general
QEMU test prints
`KARNIX64_BOOT_OK`, long-mode/page-map status, framebuffer status, and
`shell=ready`; the optional QEMU debug-exit device ends the test. `check-shell`
verifies keyboard navigation through the overview and Local AI screen.
`run` builds the host companion and both visualizers, then opens the ISO in a
graphical QEMU machine configured with 256 MiB RAM. Start Ollama and make sure the selected
model is available on the host first:

If Ollama is not already running, start it in another terminal with
`ollama serve`. Install the model once with
`ollama pull medgemma1.5:4b`.

In the QEMU window, press `A` to open the program menu, `N` or `Tab`
to move the highlight, `Enter` to launch the selected app, or `1`-`5` to launch
an app directly. Local AI opens a
chat-focused screen; type a question and press Enter, then press `Esc` to
return to the desktop. The model runs in host Ollama and the guest displays
its response. The bridge adds project-specific instructions and the 100
synthetic Patient Records examples to each chat request, so questions about
the project demo can be answered without presenting the samples as real
patients. Real account records are not sent to Local AI. `KARNIX_MODEL`
selects a different already-installed model, such as `medgemma:4b`.
Asking Local AI to summarize patient data also starts the separate legacy
32-bit OS by running `os/karnix_kernel/setup.sh --run-only`. This opens its
existing QEMU build without reinstalling packages, rebuilding, or refreshing
the database on each summary request. Chat replies keep their paragraph breaks
so longer answers are easier to read.
Press `2` for the brain MRI visualizer or `3` for the osteoarthritis CNN
activation visualizer. These SDL2 windows run on the host and use the
repository's existing datasets and checkpoints; they are not guest-native.
Press `4` to open the native Patient Records app. Sign in with the username
`Hyper` provisioned by interactive `./setup.sh` (or another existing Karnix
email/username), list that account's lung-risk assessments, or enter a new
assessment. The host records service and authenticated API must be running;
the guest has no direct database or network access. The host bridge keeps the
API session in memory and accepts only an HTTP API URL on localhost.
Set `KARNIX_API_URL` to use a different local development port, for example:

```bash
make -C os/karnix64 run KARNIX_API_URL=http://127.0.0.1:4001
```

Passwords are masked in the guest UI; credentials are sent only to the local
host bridge and are not logged. API cookies remain in host memory and are
discarded on logout or bridge shutdown. Use `make -C os/karnix64
check-patient-records` to exercise sign-in (including rejected credentials),
listing, assessment creation, logout, and URL restrictions against a synthetic
API. In Patient Records, enter `demo` to browse 100 generated synthetic
examples in pages of 10; these examples are local to the OS demo and are never
saved to an account or sent to the API. Press `5` for basic display and
keyboard settings.
`check-chat` runs a real text prompt through the guest keyboard, local
MedGemma, and serial reply path.

From this Mac, QEMU emulates an x86_64 guest on the arm64 host. This validates
the boot path, not native x86 hardware support or performance.
