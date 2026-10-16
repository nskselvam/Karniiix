# KarnixOS — cavOS on macOS

This directory contains the actual cavOS kernel and X11/TWM userspace
configuration. It can now be **built directly on macOS and booted in QEMU**,
including Apple Silicon Macs. No Linux VM, Docker, sudo, loop mounts, or
chroot is needed for the macOS build. QEMU emulates the x86_64 guest using TCG;
this is not a native macOS application.

## Build and launch on a Mac

Install Homebrew and Node.js first, then from the repository root:

```sh
make -C os tools   # Homebrew: cross-compiler, NASM, mtools, e2fsprogs, QEMU
make -C os check
make -C os disk
make -C os run
```

If your terminal is already inside `os/`, use `make run`, `make disk`, or
`make disk_dirty` without `-C os`.

## Medical image dataset

### Live Mac image inbox

Place images in `../os_image_data/` on your Mac (subfolders supported) while
the OS is running with `make run`. A read-only Node service binds only to
`127.0.0.1:18765`; the guest polls it through QEMU's `10.0.2.2` host address.
New/changed images are downloaded, SHA-256 checked, saved under
`/root/incoming-images/` in the background, normally within a few seconds.
No incoming image viewer opens at startup or when an image arrives. Open
**Incoming Images** from the desktop menu when you want to view them.
Files still being written are deferred until stable. The
service stops when QEMU exits and reports a port conflict explicitly.
No external server, upload endpoint, host folder sharing, or disk rebuild is
used for live transfers. Start via `make run`, not a standalone QEMU command.

Incoming filenames use content hashes to safely handle spaces and duplicate
names. Identical content is transferred once. Deleting a host image removes
it from the published list but does not erase a previously received guest
copy. Receiving images does not open windows or interrupt your work. Transfer failures
are reported in `/var/log/karnix-images.log` and retried.

The boot console now shows Karnix setup stages as initialization proceeds;
the desktop opens a Karnix welcome window describing the inbox and menu.
PGM, JPG, and PNG transfers were verified in
the running QEMU guest. The guest saves completed downloads using copy and
remove because its filesystem does not implement rename.
Visible branding is Karnix. Original copyright notices and
`THIRD-PARTY-NOTICES.txt` retain required attribution.

`make images` copies all supported image files recursively from
`../karnix_spine_edge/data` into `target/root/medical-images`, retaining
subdirectories and filenames. The originals stay in place for existing
applications. Archives and non-image metadata are not copied.
`manifest.json` records every imported path, byte count, and SHA-256.
The imported dataset is ignored by Git.

Both disk build targets import the images automatically. To update an
already-built Mac image, close QEMU first, then from inside `os/`:

```sh
make disk_dirty
make run
```

Click the teal desktop background to open TWM's menu. **Medical Images**
opens the recursive image viewer; use Right/Left to move through images and
`q` to close it. **Image Folders** opens the dataset in Thunar. These are
source dataset images, not automatically linked to a signed-in patient's
record, and the viewer performs no diagnosis or reconstruction.

`make -f os/Makefile ...` also works. The default target is the complete disk.
The slim desktop does not include a web browser or Google Search entry.
The initial build downloads approximately 170 Alpine x86_64 packages.
Keep enough free space for a 2 GiB root image, the combined disk, and caches.

The macOS builder:

1. Compiles the cavOS C/assembly kernel with `x86_64-elf-gcc`, GNU C11,
   imported musl headers, NASM, and uACPI 3.1.0.
2. Resolves Alpine v3.21 main/community packages. It checks dependency
   constraints and rejects conflicts.
3. Downloads packages through HTTPS. It validates control checksums against
   the HTTPS-fetched APK indexes and data SHA-256 hashes against each control
   archive. This does **not** independently verify Alpine signing keys.
4. Extracts package data without executing Linux package scripts on the Mac.
   It overlays the imported guest configuration.
5. Builds FAT32 and Ext2 images using mtools and e2fsprogs, writes an MBR
   partition table, and installs Limine v10 into `disk.img`.

Package installation hooks are intentionally not run; applications requiring
additional setup or unimplemented cavOS syscalls may still have limitations.
The kernel, Xorg/TWM desktop, mouse, and terminal should be checked separately
from other applications; a successful build does not certify all bundled apps.

The guest starts the TWM desktop automatically. Windows are placed
automatically rather than waiting for an initial placement click. The root
menu contains the original cavOS utilities. Exiting X returns to the shell;
`a` starts it again. X startup failures restore the console and report an error.

### Other commands

```sh
make -C os kernel      # compile only the cavOS kernel
make -C os ports       # prepare only the Mac-built guest userspace
make -C os disk_dirty # rebuild disk using already-prepared userspace
make -C os test        # import, native build routing, APK, and sparse-read tests
```

Build output, downloaded packages, and logs live in `build/macos/` (ignored).
`build/macos/packages.json` records the selected package versions/checksums.
`disk.img` is the bootable output. Delete cached APK indexes by their individual
names in `build/macos/downloads/` if a repository update has removed an old
package version; the builder reports failed downloads rather than silently
substituting packages.

QEMU defaults to portable TCG, one CPU, 4 GiB RAM, and the native default
display. `QEMU_ACCEL`, `QEMU_MEMORY`, `QEMU_CPUS`, and `QEMU_DISPLAY` can be
overridden, but additional guest CPUs are not validated.

### Verification on Apple Silicon

The full `make -C os disk` path was exercised on macOS arm64. The resulting
image booted the cavOS kernel, Xorg, TWM, and `st` in QEMU TCG. Mouse focus and
PS/2 typing were checked by executing `echo karnix-mac` in the guest terminal.
The eight tests include compiling the actual Ext2 reader against synthetic
sparse blocks and checking unaligned reads. This does not validate every
bundled application or medical/LLM integration.

## Imported source and licensing

- `src/`: all 467 original cavOS source files.
- `target/`: guest filesystem seed, Xorg configuration, TWM menu, fonts, files.
- `tools/`, `patches/`, `docs/`: upstream tooling, patches, documentation.
- `cavos.mk`: preserved upstream Linux build orchestration.
- `LICENSE`, `README.cavos.md`: upstream GPLv3 license and documentation.

Imported from local `../cavOS`, revision
`2ba0edb47c846761d87657cc423436e15d148077`. Builds do not use that checkout;
only the import verification test compares against it. The original checkout
is unchanged. Original copyright notices remain intact.

Integration changes include portable kernel source discovery/directory
creation, preserving disk-build error statuses, and fixing Ext2 reads to
zero-fill sparse holes and continue reading subsequent blocks. The macOS
image uses 4 KiB Ext2 blocks so the block-group descriptor table fits the
existing cavOS mount implementation. Guest timezone copying stays inside the
target tree, and compiler timestamp probing is deferred until needed.

## Linux and preserved Karnix implementations

On x86_64 Linux, `tools` and `disk` continue to use the original cavOS
toolchain/chroot/loop-mount workflow; see [upstream requirements](docs/install.md).
Use a dedicated build host because those scripts use privileged mounts.

```sh
make -C os hosted       # previous ncurses app, output ../KarnixOS
make -C os legacy64     # previous C++ framebuffer kernel
make -C os legacy32     # previous i386 kernel
make -C os/karnix64 run # previous framebuffer desktop and medical host bridges
```

These remain independent implementations. The patient database, account API,
medical visualizers, and Ollama bridge have not been ported into cavOS.
