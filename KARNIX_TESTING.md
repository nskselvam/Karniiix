# KARNIX Build and Test Record

**Recorded:** 2026-10-10  
**Host:** macOS 27 arm64, Apple Clang 21, CMake 4.4  

The repository does not have a global test runner. Results below distinguish
executed tests from targets that were skipped or blocked. Build results
reflect this host and checkout; they do not claim universal platform
compatibility.

## LLM and viewer checks

| Check | Result |
| --- | --- |
| C++20 Ollama client and CLI build | Passed |
| LLM CTest suite | Passed; mock HTTP, request/history, image base64 and serialization, invalid image rejection, history bounds, streaming, thought/image marker filtering, runtime/model failure, timeout, cancellation, loopback restriction |
| `karnix_llm --status` | Passed; local Ollama and configured MedGemma model detected |
| Real MedGemma text generation | Passed; actual local response received |
| Two-turn conversation | Passed; model recalled a value supplied in the prior turn |
| Real MedGemma image input | Passed; local non-clinical scheduling chart described through both CLI and viewer |
| External-network-blocked image request | Passed; inference succeeded with network access restricted to local Ollama |
| Hosted KarnixOS C/ncurses build | Passed |
| KarnixOS image/LLM command dispatch | Passed in pseudo-terminal; arguments forwarded to real client and inference completed |
| Earlier hosted setup workflow | Passed before the switch to the x86_64 kernel target |
| `bash -n setup.sh` | Passed |
| New x86_64 freestanding C++20 kernel build | Passed with x86_64-elf GCC and binutils |
| New Limine hybrid ISO build | Passed |
| New kernel QEMU boot/shell smoke test | Passed; serial markers confirmed long mode, 1 GiB identity map, shell initialization, and host-side inference boundary |
| New QEMU PS/2 shell input/calculator check | Passed; injected `calc 2+3*4` through the QEMU monitor and verified the result `14` |
| QEMU-to-host MedGemma chat | Passed; QEMU keyboard sent a prompt to the local MedGemma 1.5 4B model through the private serial bridge; guest confirmed receipt of the reply |
| `bash -n setup.sh` after kernel-workflow rewrite | Passed |
| Setup workflow after kernel-workflow rewrite | Passed; rebuilt the ISO and repeated the QEMU boot smoke test |
| `cmake --build llm/build && ctest --test-dir llm/build --output-on-failure` | Passed; existing hosted LLM CTest still passes |
| `git diff --check` | Passed |

These model checks establish runtime integration only. They do not test or
establish medical accuracy, clinical validity, or suitability.

## Existing component checks

| Component | Result |
| --- | --- |
| Brain tumour CLI | Passed fresh out-of-tree build |
| `Other_Disease` five executable targets | Passed in the prior component audit; setup smoke-builds the unified `medical_screening` target |
| Spine Edge CLI and MURA pipeline | Passed build |
| Math demo | Compiled; existing warning in `mathematical/Calc/CalcEng.hpp` about a non-void path without return remains |
| Freestanding kernel ELF | Cross-build passed from a temporary source copy; tracked kernel artifacts were preserved at audit time |

## Skipped or blocked

- Brain tumour and Spine Edge SDL visualizers were not built because SDL2 and
  SDL2_ttf were unavailable.
- Existing i386 kernel ISO/QEMU boot was not newly revalidated in this turn.
- The new x86_64 milestone is only a minimal boot foundation; native model
  inference, memory management, filesystem, device drivers, and networking are
  not implemented or tested.
- No repository-wide tests exist for all independent modules. Independent
  web, Rust, voice/Python, and research applications were not all run as part
  of the LLM setup workflow.
- No clinical evaluation was run for the MedGemma integration or other
  medical models.

## Reproduce the focused validation

From the repository root:

```bash
cmake -S llm -B llm/build -DCMAKE_BUILD_TYPE=Release
cmake --build llm/build --parallel
ctest --test-dir llm/build --output-on-failure
make -f os/Makefile
./llm/build/karnix_llm --status
```

To test only the separate x86_64 kernel:

```bash
make -C os/karnix64 check
```
