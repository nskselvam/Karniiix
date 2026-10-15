NASM ?= nasm
CXX = x86_64-elf-g++
LD = x86_64-elf-ld
QEMU ?= qemu-system-x86_64
HOST_CMAKE ?= cmake
HOST_CXX ?= c++
LLM_DIR := ../../llm
LLM_BUILD := $(LLM_DIR)/build
BRIDGE := $(LLM_BUILD)/karnix_qemu_medgemma_bridge
KARNIX_API_URL ?= http://127.0.0.1:4000
BRAIN_DIR := ../../Brain_tumour
SPINE_DIR := ../../karnix_spine_edge

BUILD := build
BRAIN_BUILD := $(BUILD)/host-brain
SPINE_BUILD := $(BUILD)/host-spine
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

.PHONY: all build iso check check-shell check-chat check-graphics check-desktop \
	check-desktop-render check-host-app-launch check-patient-records host-bridge host-apps run clean

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
		for key in 1 esc a tab tab tab tab tab ret h e l p ret esc a esc; do \
			printf 'sendkey %s 50\n' "$$key"; \
			sleep 0.12; \
		done; \
		printf 'quit\n'; \
	} | $(QEMU) -machine pc,accel=tcg -m 256M -display none \
		-serial "file:$$log_file" -monitor stdio -no-reboot \
		-cdrom "$(ISO)" -boot d >/dev/null; \
	cat "$$log_file"; \
	grep -q 'local-ai-chat=ready' "$$log_file" || \
		{ echo "QEMU did not launch the Local AI app." >&2; exit 1; }; \
	grep -q 'shell-command=help' "$$log_file" || \
		{ echo "Local AI keyboard input was not received." >&2; exit 1; }; \
	grep -q 'desktop-overview=opened' "$$log_file" || \
		{ echo "QEMU application overview did not open." >&2; exit 1; }; \
	grep -q 'desktop-app=local-ai' "$$log_file" || \
		{ echo "QEMU did not open Local AI from the desktop." >&2; exit 1; }; \
	test "$$(grep -c 'desktop-app=local-ai' "$$log_file")" -eq 2 || \
		{ echo "Menu selection did not wrap and launch Local AI with Enter." >&2; exit 1; }; \
	! grep -q 'desktop-app=terminal' "$$log_file" && \
	! grep -q 'desktop-app=math-lab' "$$log_file" || \
		{ echo "Removed Terminal or Math Lab app is still exposed." >&2; exit 1; }; \
	grep -q 'desktop-home=returned' "$$log_file" || \
		{ echo "Escape did not return from Local AI to the desktop." >&2; exit 1; }; \
	grep -q 'desktop-home=shown' "$$log_file" || \
		{ echo "Escape did not close the application overview." >&2; exit 1; }; \
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

check-host-app-launch: iso host-bridge
	@set -eu; \
	test_dir="$$(mktemp -d)"; \
	socket_path="$(abspath $(BUILD))/host-launch-check-$$$$.sock"; \
	bridge_log="$$(mktemp)"; \
	bridge_pid=; \
	cleanup() { \
		if test -n "$$bridge_pid"; then \
			kill "$$bridge_pid" 2>/dev/null || true; \
			wait "$$bridge_pid" 2>/dev/null || true; \
		fi; \
		rm -f "$$socket_path" "$$bridge_log" "$$test_dir/model"; \
		rmdir "$$test_dir/images" "$$test_dir" 2>/dev/null || true; \
	}; \
	trap cleanup EXIT INT TERM; \
	mkdir "$$test_dir/images"; \
	: > "$$test_dir/model"; \
	"$(abspath $(BRIDGE))" --socket "$$socket_path" \
		--medical-executable /usr/bin/true \
		--medical-images "$$test_dir/images" \
		--medical-model "$$test_dir/model" \
		--medical-directory "$$test_dir" \
		--cnn-executable /usr/bin/true \
		--cnn-images "$$test_dir/images" \
		--cnn-model "$$test_dir/model" \
		--cnn-directory "$$test_dir" \
		--cnn-task TEST --cnn-negative-label NO --cnn-positive-label YES \
		>"$$bridge_log" 2>&1 & \
	bridge_pid=$$!; \
	attempt=0; \
	while test ! -S "$$socket_path" && test "$$attempt" -lt 50; do \
		sleep 0.1; \
		attempt=$$((attempt + 1)); \
	done; \
	test -S "$$socket_path" || \
		{ cat "$$bridge_log"; echo "Host-app bridge did not start." >&2; exit 1; }; \
	{ \
		sleep 3; \
		printf 'sendkey 2\n'; \
		sleep 1; \
		printf 'sendkey 3\n'; \
		sleep 1; \
		printf 'sendkey 5\n'; \
		sleep 1; \
		printf 'quit\n'; \
	} | $(QEMU) -machine pc,accel=tcg -m 256M -display none \
		-serial "unix:$$socket_path,server=off" -monitor stdio \
		-no-reboot -cdrom "$(ISO)" -boot d >/dev/null; \
	wait "$$bridge_pid"; \
	bridge_pid=; \
	cat "$$bridge_log"; \
	test "$$(grep -c '^Guest accepted host app launch response\.' "$$bridge_log")" -eq 2 || \
		{ echo "The guest did not accept both host visualizer launch responses." >&2; exit 1; }; \
	grep -q '^Guest opened basic desktop settings\.$$' "$$bridge_log" || \
		{ echo "The Settings app did not remain available at key 5." >&2; exit 1; }; \
	echo "QEMU host-visualizer launch check passed."

check-patient-records: iso host-bridge
	@set -eu; \
	command -v node >/dev/null || \
		{ echo "Node.js is required for the Patient Records integration check." >&2; exit 1; }; \
	test_dir="$$(mktemp -d)"; \
	socket_path="$(abspath $(BUILD))/patient-records-check-$$$$.sock"; \
	api_log="$$test_dir/api.log"; \
	bridge_log="$$test_dir/bridge.log"; \
	api_pid=; \
	bridge_pid=; \
	cleanup() { \
		if test -n "$$bridge_pid"; then \
			kill "$$bridge_pid" 2>/dev/null || true; \
			wait "$$bridge_pid" 2>/dev/null || true; \
		fi; \
		if test -n "$$api_pid"; then \
			kill "$$api_pid" 2>/dev/null || true; \
			wait "$$api_pid" 2>/dev/null || true; \
		fi; \
		rm -f "$$socket_path" "$$api_log" "$$bridge_log"; \
		rmdir "$$test_dir" 2>/dev/null || true; \
	}; \
	trap cleanup EXIT INT TERM; \
	for invalid_url in \
		https://127.0.0.1:4000 \
		http://attacker.example \
		http://127.0.0.1.evil:4000 \
		http://localhost:70000; do \
		if "$(abspath $(BRIDGE))" --socket "$$test_dir/invalid.sock" \
			--api-url "$$invalid_url" >"$$api_log" 2>&1; then \
			echo "Host bridge accepted a non-local records API URL: $$invalid_url" >&2; exit 1; \
		fi; \
		grep -q -- '--api-url must use HTTP on localhost or 127.0.0.1' "$$api_log" || \
			{ cat "$$api_log"; echo "Invalid API URL was not rejected clearly." >&2; exit 1; }; \
	done; \
	node "$(CURDIR)/tests/mock_records_api.mjs" --port 0 >"$$api_log" 2>&1 & \
	api_pid=$$!; \
	attempt=0; \
	while ! grep -q '^LISTENING ' "$$api_log" && test "$$attempt" -lt 50; do \
		sleep 0.1; attempt=$$((attempt + 1)); \
	done; \
	api_port="$$(sed -n 's/^LISTENING //p' "$$api_log" | head -n 1)"; \
	test -n "$$api_port" || \
		{ cat "$$api_log"; echo "Mock records API did not start." >&2; exit 1; }; \
	"$(abspath $(BRIDGE))" --socket "$$socket_path" \
		--api-url "http://127.0.0.1:$$api_port" >"$$bridge_log" 2>&1 & \
	bridge_pid=$$!; \
	attempt=0; \
	while test ! -S "$$socket_path" && test "$$attempt" -lt 50; do \
		sleep 0.1; attempt=$$((attempt + 1)); \
	done; \
	test -S "$$socket_path" || \
		{ cat "$$bridge_log"; echo "Records bridge did not start." >&2; exit 1; }; \
	{ \
		sleep 3; \
		send_text() { \
			text="$$1"; \
			index=1; \
			while test "$$index" -le "$${#text}"; do \
				character="$$(printf '%s' "$$text" | cut -c "$$index")"; \
				case "$$character" in \
					_) key=shift-minus ;; \
					-) key=minus ;; \
					.) key=dot ;; \
					@) key=shift-2 ;; \
					,) key=comma ;; \
					*) key="$$character" ;; \
				esac; \
				printf 'sendkey %s 40\n' "$$key"; sleep 0.05; \
				index=$$((index + 1)); \
			done; \
		}; \
		send_line() { send_text "$$1"; printf 'sendkey ret\n'; sleep 0.15; }; \
		printf 'sendkey 4\n'; sleep 1; \
		send_line login; \
		send_line hyper; \
		send_line wrong-pass; sleep 1; \
		send_line login; \
		send_line hyper; \
		send_line karnix-test-pass; sleep 1; \
		send_line list; sleep 1; \
		send_line new; \
		for value in 45 female white never 0 0 - never none - never moderate \
			1_3_hours 6_8_hours average 170 70 low urban low low no - - no no -; do \
			send_line "$$value"; \
		done; \
		sleep 1; \
		send_line list; sleep 1; \
		send_line logout; sleep 1; \
		send_line exit; sleep 1; \
		printf 'quit\n'; \
	} | $(QEMU) -machine pc,accel=tcg -m 256M -display none \
		-serial "unix:$$socket_path,server=off" -monitor stdio \
		-no-reboot -cdrom "$(ISO)" -boot d >/dev/null; \
	wait "$$bridge_pid"; bridge_pid=; \
	kill "$$api_pid" 2>/dev/null || true; \
	wait "$$api_pid" 2>/dev/null || true; api_pid=; \
	cat "$$bridge_log"; \
	cat "$$api_log"; \
	grep -q '^Karnix OS authenticated to the records API\.$$' "$$bridge_log" || \
		{ echo "Native OS login did not authenticate through the host bridge." >&2; exit 1; }; \
	grep -q '^Karnix OS retrieved the authenticated user.s records\.$$' "$$bridge_log" || \
		{ echo "Native OS could not list authenticated records." >&2; exit 1; }; \
	grep -q '^Karnix OS saved a lung-risk assessment\.$$' "$$bridge_log" || \
		{ echo "Native OS did not save the assessment to the mock backend." >&2; exit 1; }; \
	grep -q '^Karnix OS signed out from the records API\.$$' "$$bridge_log" || \
		{ echo "Native OS did not revoke the records session." >&2; exit 1; }; \
	grep -q '^LOGIN_REJECTED$$' "$$api_log" || \
		{ cat "$$api_log"; echo "Invalid credentials were not rejected by the API." >&2; exit 1; }; \
	test "$$(grep -c '^LOGIN_OK$$' "$$api_log")" -eq 1 || \
		{ cat "$$api_log"; echo "Expected exactly one successful records login." >&2; exit 1; }; \
	grep -q '^LIST_COUNT 0$$' "$$api_log" && \
	grep -q '^LIST_COUNT 1$$' "$$api_log" && \
	grep -q '^CREATE_OK$$' "$$api_log" || \
		{ cat "$$api_log"; echo "Records list/create operations did not reach the API." >&2; exit 1; }; \
	echo "QEMU native Patient Records login, create, list, and logout check passed."

check-desktop-render: | $(BUILD)/.dir
	$(HOST_CXX) -std=c++20 -Wall -Wextra -Wpedantic \
		tests/desktop_render.cpp desktop.cpp -o $(BUILD)/desktop-render-test
	./$(BUILD)/desktop-render-test

check-desktop: check-desktop-render check-graphics check-shell check-host-app-launch check-patient-records

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
	setup_script="$$(mktemp)"; \
	setup_marker="$$(mktemp)"; \
	rm -f "$$setup_marker"; \
	socket_path="$(abspath $(BUILD))/chat-check-$$$$.sock"; \
	bridge_pid=; \
	printf '#!/bin/bash\nprintf started > "%s"\n' "$$setup_marker" \
		>"$$setup_script"; \
	cleanup() { \
		if test -n "$$bridge_pid"; then \
			kill "$$bridge_pid" 2>/dev/null || true; \
			wait "$$bridge_pid" 2>/dev/null || true; \
		fi; \
		rm -f "$$log_file" "$$setup_script" "$$setup_marker" "$$socket_path"; \
	}; \
	trap cleanup EXIT INT TERM; \
	"$(abspath $(BRIDGE))" --socket "$$socket_path" --model "$$model" \
		--legacy-setup "$$setup_script" \
		--api-url "$(KARNIX_API_URL)" \
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
		sleep 6; \
		printf 'sendkey 1\n'; \
		sleep 2; \
		for key in s u m m a r i z e spc t h e spc d a t a ret; do \
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
	attempt=0; \
	while test ! -f "$$setup_marker" && test "$$attempt" -lt 50; do \
		sleep 0.1; \
		attempt=$$((attempt + 1)); \
	done; \
	test -f "$$setup_marker" || \
		{ echo "Patient-data summary did not launch the configured OS setup script." >&2; exit 1; }; \
	grep -q 'Started the legacy 32-bit KarnixOS in a separate window.' "$$log_file" || \
		{ echo "Patient-data summary did not report launching the legacy OS." >&2; exit 1; }; \
	echo "QEMU-to-host-Ollama MedGemma chat check passed."

host-bridge:
	$(HOST_CMAKE) -S "$(LLM_DIR)" -B "$(LLM_BUILD)" \
		-DCMAKE_BUILD_TYPE=Release
	$(HOST_CMAKE) --build "$(LLM_BUILD)" --target karnix_qemu_medgemma_bridge --parallel

host-apps:
	$(HOST_CMAKE) -S "$(BRAIN_DIR)" -B "$(BRAIN_BUILD)" \
		-DCMAKE_BUILD_TYPE=Release
	$(HOST_CMAKE) --build "$(BRAIN_BUILD)" --target brain_tumour_visualizer --parallel
	$(HOST_CMAKE) -S "$(SPINE_DIR)" -B "$(SPINE_BUILD)" \
		-DCMAKE_BUILD_TYPE=Release
	$(HOST_CMAKE) --build "$(SPINE_BUILD)" --target cnn_visualizer --parallel

run: iso host-bridge host-apps
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
	"$(abspath $(BRIDGE))" --socket "$$socket_path" \
		--api-url "$(KARNIX_API_URL)" \
		--legacy-setup "$(abspath ../karnix_kernel/setup.sh)" \
		--medical-executable "$(abspath $(BRAIN_BUILD)/brain_tumour_visualizer)" \
		--medical-images "$(abspath $(BRAIN_DIR)/data/brain_tumor/Testing)" \
		--medical-model "$(abspath $(BRAIN_DIR)/models/brain_tumour_cnn.weights)" \
		--medical-directory "$(abspath $(BRAIN_DIR))" \
		--cnn-executable "$(abspath $(SPINE_BUILD)/cnn_visualizer)" \
		--cnn-images "$(abspath $(SPINE_DIR)/data/osteoarthritis/screen)" \
		--cnn-model "$(abspath $(SPINE_DIR)/models/osteoarthritis_cnn.weights)" \
		--cnn-directory "$(abspath $(SPINE_DIR))" \
		--cnn-task OSTEOARTHRITIS --cnn-negative-label "NO OA" \
		--cnn-positive-label OA & \
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
