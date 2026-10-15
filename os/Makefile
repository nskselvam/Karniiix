OS_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
.DEFAULT_GOAL := disk
QEMU ?= qemu-system-x86_64
QEMU_ACCEL ?= tcg
QEMU_MEMORY ?= 4G
QEMU_DISPLAY ?= default
QEMU_CPUS ?= 1
HOST_OS := $(shell uname -s)

.PHONY: all disk disk_dirty tools ports musl limine uacpi kernel check test images \
	check-host check-linux qemu run qemu_dbg hosted legacy64 legacy32 help

all: disk

help:
	@printf '%s\n' \
		'KarnixOS: cavOS-based kernel and X11/TWM userspace' \
		'make -C os check       Check build prerequisites without changing the host' \
		'make -C os tools       Install Mac build tools, or build the Linux cross-toolchain' \
		'make -C os disk        Build full bootable disk and graphical userspace' \
		'make -C os kernel      Build only the cavOS kernel (requires dependencies)' \
		'make -C os run         Boot the built disk using QEMU' \
		'make -C os hosted      Build the preserved ncurses application' \
		'make -C os legacy64    Build the preserved framebuffer Karnix kernel' \
		'make -C os legacy32    Build the preserved i386 Karnix kernel'

check:
	@bash "$(OS_ROOT)/tools/check-host.sh"

test:
	node --test "$(OS_ROOT)/tools/test-integration.mjs" "$(OS_ROOT)/tools/import-images.test.mjs"

images:
	node "$(OS_ROOT)/tools/import-images.mjs"

check-host:
	@bash "$(OS_ROOT)/tools/check-host.sh" kernel

check-linux:
	@test "$$(uname -s)" = Linux && test "$$(uname -m)" = x86_64 || \
		{ echo 'Full cavOS builds require an x86_64 Linux host/VM (chroot and loop mounts).' >&2; exit 1; }

ifeq ($(HOST_OS),Darwin)
disk: check images
	node "$(OS_ROOT)/tools/macos-build.mjs"

disk_dirty: check images
	node "$(OS_ROOT)/tools/macos-build.mjs" disk-only

tools:
	brew install x86_64-elf-gcc nasm mtools e2fsprogs qemu

ports: check
	node "$(OS_ROOT)/tools/macos-build.mjs" userspace

musl: ports

kernel: check-host
	node "$(OS_ROOT)/tools/macos-build.mjs" kernel
else
disk disk_dirty: check-linux images
	@$(MAKE) -C "$(OS_ROOT)" -f cavos.mk $@

tools ports musl: check-linux
	@$(MAKE) -C "$(OS_ROOT)" -f cavos.mk $@

kernel: check-host
	@$(MAKE) -C "$(OS_ROOT)/src/kernel" all
endif

limine uacpi:
	@$(MAKE) -C "$(OS_ROOT)" -f cavos.mk $@

run: qemu

qemu:
	@test -s "$(OS_ROOT)/disk.img" || \
		{ echo 'No cavOS disk image. Build it with: make -C os disk' >&2; exit 1; }
	$(QEMU) -machine pc,accel=$(QEMU_ACCEL) -m $(QEMU_MEMORY) -smp $(QEMU_CPUS) \
		-serial stdio -display $(QEMU_DISPLAY) -vga vmware \
		-drive file="$(OS_ROOT)/disk.img",format=raw,id=disk,if=none \
		-device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 \
		-netdev user,id=mynet0 -device e1000,netdev=mynet0 -no-reboot

qemu_dbg:
	@$(MAKE) -C "$(OS_ROOT)" qemu QEMU='$(QEMU) -s -S'

hosted:
	@$(MAKE) -C "$(OS_ROOT)/.." -f "$(OS_ROOT)/Makefile.hosted"

legacy64:
	@$(MAKE) -C "$(OS_ROOT)/karnix64" build

legacy32:
	@$(MAKE) -C "$(OS_ROOT)/karnix_kernel"
