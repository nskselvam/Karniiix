NASM ?= nasm
CXX = x86_64-elf-g++
LD = x86_64-elf-ld
QEMU ?= qemu-system-x86_64
HOST_CMAKE ?= cmake
LLM_DIR := ../../llm
LLM_BUILD := $(LLM_DIR)/build
BRIDGE := $(LLM_BUILD)/karnix_qemu_medgemma_bridge

BUILD := build
ELF := $(BUILD)/karnix64.elf
ISO := $(BUILD)/karnix64.iso
ISO_ROOT := $(BUILD)/iso_root
BOOT_OBJECT := $(BUILD)/boot.o
KERNEL_OBJECT := $(BUILD)/kernel.o
FRAMEBUFFER_OBJECT := $(BUILD)/framebuffer.o
DESKTOP_OBJECT := $(BUILD)/desktop.o
LIMINE_DATA := $(shell limine --print-datadir 2>/dev/null)

CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Wpedantic \
	-ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector \
	-fno-pic -fno-pie -fno-threadsafe-statics -m64 -mno-sse -mno-sse2 \
	-mno-mmx -mno-red-zone \
	-mcmodel=kernel
LDFLAGS := -m elf_x86_64 -T linker.ld --build-id=none

.PHONY: all build iso check check-shell check-chat check-graphics check-desktop host-bridge run clean

all: build

build: $(ELF)

$(BUILD)/.dir:
	mkdir -p $(BUILD)
	touch $@

$(BOOT_OBJECT): boot.asm | $(BUILD)/.dir
	$(NASM) -f elf64 $< -o $@

$(KERNEL_OBJECT): kernel.cpp framebuffer.hpp desktop.hpp | $(BUILD)/.dir
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(FRAMEBUFFER_OBJECT): framebuffer.cpp framebuffer.hpp | $(BUILD)/.dir
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(DESKTOP_OBJECT): desktop.cpp desktop.hpp framebuffer.hpp | $(BUILD)/.dir
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(ELF): $(BOOT_OBJECT) $(KERNEL_OBJECT) $(FRAMEBUFFER_OBJECT) $(DESKTOP_OBJECT) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(BOOT_OBJECT) $(KERNEL_OBJECT) $(FRAMEBUFFER_OBJECT) $(DESKTOP_OBJECT)
	@echo "Built 64-bit freestanding kernel: $@"

iso: $(ELF) limine.conf
	test -n "$(LIMINE_DATA)" || \
		{ echo "Limine is required; install it with 'brew install limine'." >&2; exit 1; }
	command -v xorriso >/dev/null || \
		{ echo "xorriso is required; install it with 'brew install xorriso'." >&2; exit 1; }
	mkdir -p "$(ISO_ROOT)/boot" "$(ISO_ROOT)/EFI/BOOT"
	cp "$(ELF)" "$(ISO_ROOT)/boot/karnix64.elf"
	cp limine.conf "$(ISO_ROOT)/limine.conf"
	cp "$(LIMINE_DATA)/limine-bios.sys" "$(ISO_ROOT)/limine-bios.sys"
	cp "$(LIMINE_DATA)/limine-bios-cd.bin" "$(ISO_ROOT)/limine-bios-cd.bin"
	cp "$(LIMINE_DATA)/limine-uefi-cd.bin" "$(ISO_ROOT)/limine-uefi-cd.bin"
	cp "$(LIMINE_DATA)/BOOTX64.EFI" "$(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI"
	xorriso -as mkisofs -R -r -J \
		-b limine-bios-cd.bin -no-emul-boot -boot-load-size 4 \
		-boot-info-table -hfsplus -apm-block-size 2048 \
		--efi-boot limine-uefi-cd.bin -efi-boot-part \
		--efi-boot-image --protective-msdos-label \
		"$(ISO_ROOT)" -o "$(ISO)"
	limine bios-install "$(ISO)"

check: iso
	@set -eu; \
	log_file="$$(mktemp)"; \
	trap 'rm -f "$$log_file"' EXIT; \
	set +e; \
	$(QEMU) -machine pc,accel=tcg -m 256M -display none \
		-serial "file:$$log_file" -monitor none -no-reboot \
		-cdrom "$(ISO)" -boot d \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04; \
	qemu_status=$$?; \
	set -e; \
	cat "$$log_file"; \
	grep -q 'KARNIX64_BOOT_OK' "$$log_file" || \
		{ echo "QEMU kernel boot marker not found." >&2; exit 1; }; \
	grep -q 'shell=ready' "$$log_file" || \
		{ echo "QEMU shell initialization marker not found." >&2; exit 1; }; \
	test "$$qemu_status" -eq 33 || \
		{ echo "QEMU exited with unexpected status $$qemu_status (expected 33)." >&2; exit 1; }; \
	echo "64-bit kernel boot check passed."

check-shell: iso
	@set -eu; \
	log_file="$$(mktemp)"; \
	trap 'rm -f "$$log_file"' EXIT; \
	{ \
		sleep 2; \
		for key in 1 esc a esc 1 c a l c spc 2 shift-equal 3 shift-8 4 ret; do \
			printf 'sendkey %s 50\n' "$$key"; \
			sleep 0.12; \
		done; \
		sleep 1; \
		printf 'quit\n'; \
	} | $(QEMU) -machine pc,accel=tcg -m 256M -display none \
		-serial "file:$$log_file" -monitor stdio -no-reboot \
		-cdrom "$(ISO)" -boot d >/dev/null; \
	cat "$$log_file"; \
	grep -q 'shell-command=calc 2+3\*4' "$$log_file" || \
		{ echo "QEMU keyboard shell command was not received." >&2; exit 1; }; \
	grep -q 'calc-result=14' "$$log_file" || \
		{ echo "QEMU shell calculator produced an unexpected result." >&2; exit 1; }; \
	grep -q 'desktop-overview=opened' "$$log_file" || \
		{ echo "QEMU program menu did not open." >&2; exit 1; }; \
	grep -q 'desktop-app=terminal' "$$log_file" || \
		{ echo "QEMU did not open the Terminal from the program menu." >&2; exit 1; }; \
	grep -q 'desktop-home=returned' "$$log_file" || \
		{ echo "Escape did not return from Terminal to the desktop home screen." >&2; exit 1; }; \
	grep -q 'desktop-home=shown' "$$log_file" || \
		{ echo "Escape did not close the program menu to the home screen." >&2; exit 1; }; \
	echo "64-bit keyboard shell check passed."

check-graphics: iso
	@set -eu; \
	log_file="$$(mktemp)"; \
	trap 'rm -f "$$log_file"' EXIT; \
	set +e; \
	$(QEMU) -machine pc,accel=tcg -m 256M -display none \
		-serial "file:$$log_file" -monitor none -no-reboot \
		-cdrom "$(ISO)" -boot d \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04; \
	qemu_status=$$?; \
	set -e; \
	cat "$$log_file"; \
	grep -q 'framebuffer=ready' "$$log_file" || \
		{ echo "QEMU Multiboot2 framebuffer was not initialized." >&2; exit 1; }; \
	grep -q 'desktop-ui=ready' "$$log_file" || \
	{ echo "Graphical desktop shell was not rendered." >&2; exit 1; }; \
	grep -q 'KARNIX64_BOOT_OK' "$$log_file" || \
		{ echo "QEMU kernel boot marker not found." >&2; exit 1; }; \
	test "$$qemu_status" -eq 33 || \
		{ echo "QEMU exited with unexpected status $$qemu_status (expected 33)." >&2; exit 1; }; \
	echo "64-bit framebuffer graphics check passed."

check-desktop: check-graphics check-shell

check-chat: iso host-bridge
	@set -eu; \
	command -v ollama >/dev/null || \
		{ echo "Ollama is required for check-chat." >&2; exit 1; }; \
	curl --silent --show-error --fail http://127.0.0.1:11434/api/tags >/dev/null || \
		{ echo "Start local Ollama before running check-chat." >&2; exit 1; }; \
	model="$${KARNIX_MODEL:-medgemma1.5:4b}"; \
	ollama show "$$model" >/dev/null || \
		{ echo "Model '$$model' is not installed in local Ollama." >&2; exit 1; }; \
	log_file="$$(mktemp)"; \
	socket_path="$(abspath $(BUILD))/chat-check-$$$$.sock"; \
	bridge_pid=; \
	cleanup() { \
		if test -n "$$bridge_pid"; then \
			kill "$$bridge_pid" 2>/dev/null || true; \
			wait "$$bridge_pid" 2>/dev/null || true; \
		fi; \
		rm -f "$$log_file" "$$socket_path"; \
	}; \
	trap cleanup EXIT INT TERM; \
	"$(abspath $(BRIDGE))" --socket "$$socket_path" --model "$$model" \
		>"$$log_file" 2>&1 & \
	bridge_pid=$$!; \
	attempt=0; \
	while test ! -S "$$socket_path" && test "$$attempt" -lt 50; do \
		sleep 0.1; \
		attempt=$$((attempt + 1)); \
	done; \
	test -S "$$socket_path" || \
		{ cat "$$log_file"; echo "MedGemma bridge did not start." >&2; exit 1; }; \
	{ \
		sleep 3; \
		for key in t a s k spc s a y spc r e a d y ret; do \
			printf 'sendkey %s 50\n' "$$key"; \
			sleep 0.12; \
		done; \
		attempt=0; \
		while ! grep -q 'KarnixOS guest received the MedGemma reply.' "$$log_file" && \
		      test "$$attempt" -lt 120; do \
			sleep 1; \
			attempt=$$((attempt + 1)); \
		done; \
		printf 'quit\n'; \
	} | $(QEMU) -machine pc,accel=tcg -m 256M -display none \
		-serial "unix:$$socket_path,server=off" -monitor stdio \
		-no-reboot -cdrom "$(ISO)" -boot d >/dev/null; \
	cat "$$log_file"; \
	grep -q 'KarnixOS guest received the MedGemma reply.' "$$log_file" || \
		{ echo "No MedGemma reply made it back to the KarnixOS guest." >&2; exit 1; }; \
	echo "QEMU-to-host-Ollama MedGemma chat check passed."

host-bridge:
	$(HOST_CMAKE) -S "$(LLM_DIR)" -B "$(LLM_BUILD)" \
		-DCMAKE_BUILD_TYPE=Release
	$(HOST_CMAKE) --build "$(LLM_BUILD)" --target karnix_qemu_medgemma_bridge --parallel

run: iso host-bridge
	@set -eu; \
	socket_path="$(abspath $(BUILD))/medgemma-$$$$.sock"; \
	bridge_pid=; \
	cleanup() { \
		if test -n "$$bridge_pid"; then \
			kill "$$bridge_pid" 2>/dev/null || true; \
			wait "$$bridge_pid" 2>/dev/null || true; \
		fi; \
		rm -f "$$socket_path"; \
	}; \
	trap cleanup EXIT INT TERM; \
	"$(abspath $(BRIDGE))" --socket "$$socket_path" & \
	bridge_pid=$$!; \
	attempt=0; \
	while test ! -S "$$socket_path" && test "$$attempt" -lt 50; do \
		sleep 0.1; \
		attempt=$$((attempt + 1)); \
	done; \
	test -S "$$socket_path" || \
		{ echo "MedGemma host bridge did not create its local socket." >&2; exit 1; }; \
	$(QEMU) -machine pc,accel=tcg -m 256M -vga std \
		-no-reboot -no-shutdown \
		-serial "unix:$$socket_path,server=off" \
		-cdrom "$(ISO)" -boot d

clean:
	rm -rf $(BUILD)
