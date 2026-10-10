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
- Boots to an original DOS/TempleOS-inspired blue-and-white text desktop with
  a high-contrast program menu. The home screen lists six keyboard-selected
  programs; `1`-`6` runs one and `A` highlights the program menu.
- Terminal (`T` or `1`) opens the working command shell; `Esc` or `DESKTOP`
  returns to the home screen.
  Local AI opens the terminal instructions for the existing `ASK <prompt>`
  Ollama bridge. Medical Imaging and CNN Lab show unavailable states instead
  of simulated results.
- The desktop and terminal use clipped framebuffer rendering and a
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

This is an independently written, **DOS/TempleOS-inspired** Karnix text
desktop, not MS-DOS, TempleOS, or a Linux desktop installation. It remains an
early milestone:
there is no mouse, multi-window manager, guest filesystem, guest-native
medical imaging or CNN viewer, multitasking, or full HolyC implementation.
The 5x7 glyph
data is original to this project and licensed under the MIT License in
[FONT-LICENSE.txt](FONT-LICENSE.txt).

MedGemma inference is **not executed inside the bare-metal guest**. The
graphical QEMU launcher requires the host-side `karnix_qemu_medgemma_bridge`,
Ollama, and the configured model (default `medgemma1.5:4b`). The bridge has no
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
brew install x86_64-elf-gcc limine qemu nasm xorriso
```

Build the ELF, make the bootable ISO, boot-test it headlessly, or launch the
kernel screen in a QEMU window:

```bash
make -C os/karnix64 build
make -C os/karnix64 iso
make -C os/karnix64 check
make -C os/karnix64 check-shell
make -C os/karnix64 check-graphics
make -C os/karnix64 check-desktop
make -C os/karnix64 check-chat
make -C os/karnix64 run
```

`check-graphics` verifies that QEMU supplies a supported Multiboot2 RGB
framebuffer and that the desktop initializes. `check-desktop` also checks app
launch and that Escape returns from Terminal to the home screen. The general
QEMU test prints
`KARNIX64_BOOT_OK`, long-mode/page-map status, framebuffer status, and
`shell=ready`; the optional QEMU debug-exit device ends the test. `check-shell`
types `calc 2+3*4` through QEMU's emulated keyboard and verifies `14`.
`run` builds the host companion, then opens the ISO in a graphical QEMU
machine configured with 256 MiB RAM. Start Ollama and make sure the selected
model is available on the host first:

If Ollama is not already running, start it in another terminal with
`ollama serve`. Install the model once with
`ollama pull medgemma1.5:4b`.

In the QEMU window, press a number key to run an app, or `A` to highlight the
program menu. Press `T` to open the terminal directly. In the terminal, press
`Esc` to return to the home screen, or type `help`, `status`, `calc 2+3*4`,
or `ask What is MedGemma?`; type `desktop` to return as well. The model runs
in host Ollama;
the guest shell displays its response. `KARNIX_MODEL` selects a different
already-installed model, such as `medgemma:4b`.
`check-chat` runs a real text prompt through the guest keyboard, local
MedGemma, and serial reply path.

From this Mac, QEMU emulates an x86_64 guest on the arm64 host. This validates
the boot path, not native x86 hardware support or performance.
