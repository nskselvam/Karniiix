#!/usr/bin/env bash
# =============================================================================
#  KarnixOS – One-shot setup, build, and launch script
#  Usage:
#    bash setup.sh                 # install deps + build + run ELF in QEMU
#    bash setup.sh --run-only      # skip install/build, just launch existing ELF
#    bash setup.sh --build-only    # install + build but don't open QEMU
#    bash setup.sh --iso           # build ISO with GRUB and boot from it
#    bash setup.sh --skip-db       # skip DB export (use fallback / existing db_data.h)
# =============================================================================
set -euo pipefail

# ── colours ───────────────────────────────────────────────────────────────────
RED='\033[1;31m'; GRN='\033[1;32m'; YEL='\033[1;33m'
CYN='\033[1;36m'; WHT='\033[1;37m'; DIM='\033[0;37m'; RST='\033[0m'

say()  { printf "${CYN}[KarnixOS]${RST} %s\n" "$*"; }
ok()   { printf "${GRN}  ✓${RST}  %s\n" "$*"; }
warn() { printf "${YEL}  ⚠${RST}  %s\n" "$*"; }
die()  { printf "${RED}[FATAL]${RST}   %s\n" "$*"; exit 1; }
sep()  { printf "${DIM}────────────────────────────────────────────────────────${RST}\n"; }

# ── parse flags ───────────────────────────────────────────────────────────────
RUN_ONLY=0; BUILD_ONLY=0; USE_ISO=0; SKIP_DB=0
for arg in "$@"; do
  case "$arg" in
    --run-only)   RUN_ONLY=1   ;;
    --build-only) BUILD_ONLY=1 ;;
    --iso)        USE_ISO=1    ;;
    --skip-db)    SKIP_DB=1    ;;
    --help|-h)
      printf "Usage: bash setup.sh [OPTIONS]\n"
      printf "  (none)        install deps + build + launch QEMU\n"
      printf "  --run-only    launch existing ELF/ISO without rebuilding\n"
      printf "  --build-only  build only, do not launch QEMU\n"
      printf "  --iso         build bootable ISO via GRUB, boot from it\n"
      printf "  --skip-db     skip PostgreSQL export (use fallback data)\n"
      exit 0 ;;
  esac
done

# ── anchor to this script's directory ─────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

printf "\n${WHT}"
printf "  ██╗  ██╗ █████╗ ██████╗ ███╗   ██╗██╗██╗  ██╗ ██████╗ ███████╗\n"
printf "  ██║ ██╔╝██╔══██╗██╔══██╗████╗  ██║██║╚██╗██╔╝██╔═══██╗██╔════╝\n"
printf "  █████╔╝ ███████║██████╔╝██╔██╗ ██║██║ ╚███╔╝ ██║   ██║███████╗\n"
printf "  ██╔═██╗ ██╔══██║██╔══██╗██║╚██╗██║██║ ██╔██╗ ██║   ██║╚════██║\n"
printf "  ██║  ██╗██║  ██║██║  ██║██║ ╚████║██║██╔╝ ██╗╚██████╔╝███████║\n"
printf "  ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═══╝╚═╝╚═╝  ╚═╝ ╚═════╝ ╚══════╝\n"
printf "${RST}\n"
say "Bare-metal x86 OS simulation – $(date '+%Y-%m-%d %H:%M')"
say "Directory: $SCRIPT_DIR"
sep

# =============================================================================
#  PHASE 1 – Dependency installation
# =============================================================================
if [[ $RUN_ONLY -eq 0 ]]; then
  say "Phase 1 – checking / installing dependencies"

  # Homebrew
  if ! command -v brew &>/dev/null; then
    say "Installing Homebrew..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    [[ -f /opt/homebrew/bin/brew ]] && eval "$(/opt/homebrew/bin/brew shellenv)"
    [[ -f /usr/local/bin/brew   ]] && eval "$(/usr/local/bin/brew shellenv)"
  fi
  ok "brew $(brew --version | head -1)"

  # Generic brew installer ─────────────────────────────────────────────────────
  need_brew() {  # need_brew <binary> [brew-pkg]
    local bin="$1" pkg="${2:-$1}"
    if ! command -v "$bin" &>/dev/null; then
      say "  brew install $pkg..."
      brew install "$pkg" 2>&1 | tail -3 || warn "brew install $pkg failed – continuing"
    fi
    command -v "$bin" &>/dev/null && ok "$bin  ($(command -v "$bin"))" \
      || warn "$bin still not found after install"
  }

  # NASM
  need_brew nasm nasm

  # i686-elf cross-compiler
  if ! command -v i686-elf-gcc &>/dev/null; then
    say "  Installing i686-elf cross-compiler..."
    brew tap nativeos/i686-elf-toolchain 2>/dev/null || true
    brew install nativeos/i686-elf-toolchain/i686-elf-gcc \
                 nativeos/i686-elf-toolchain/i686-elf-binutils 2>/dev/null || \
    brew install i686-elf-gcc i686-elf-binutils 2>/dev/null || \
    warn "i686-elf-gcc unavailable – Makefile will fall back to clang cross-target"
  fi
  command -v i686-elf-gcc &>/dev/null && ok "i686-elf-gcc" \
    || warn "i686-elf-gcc not found – using clang fallback"

  # QEMU
  need_brew qemu-system-i386 qemu

  # xorriso (needed for ISO)
  if [[ $USE_ISO -eq 1 ]]; then
    need_brew xorriso xorriso
    # GRUB mkrescue
    if ! command -v grub-mkrescue &>/dev/null && \
       ! command -v grub2-mkrescue &>/dev/null && \
       ! command -v i686-elf-grub-mkrescue &>/dev/null; then
      say "  Installing GRUB mkrescue..."
      brew install i686-elf-grub 2>/dev/null || \
      brew install grub          2>/dev/null || \
      warn "grub-mkrescue not found – ISO build may fail"
    else
      ok "grub-mkrescue available"
    fi
  fi

  # Node.js (db_exporter)
  if ! command -v node &>/dev/null; then
    say "  brew install node..."
    brew install node 2>/dev/null || warn "node install failed"
  fi
  command -v node &>/dev/null && ok "node $(node --version)" || warn "node not found"

  sep
fi  # end PHASE 1

# =============================================================================
#  PHASE 2 – Generate db_data.h
# =============================================================================
if [[ $RUN_ONLY -eq 0 && $SKIP_DB -eq 0 ]]; then
  say "Phase 2 – generating db_data.h"

  if command -v node &>/dev/null && [[ -f db_exporter.js ]]; then
    # Install npm deps if needed
    if [[ ! -d node_modules ]] || [[ ! -f node_modules/.bin/.package-lock ]]; then
      say "  npm install..."
      npm install --silent 2>/dev/null || warn "npm install had errors"
    fi
    say "  Running db_exporter.js..."
    node db_exporter.js 2>/dev/null \
      && ok "db_data.h refreshed from PostgreSQL" \
      || warn "DB unreachable – fallback data will be used (db_data.h may already exist)"
  else
    if [[ -f db_data.h ]]; then
      ok "db_data.h already present (skipping export)"
    else
      warn "node or db_exporter.js missing – writing minimal fallback db_data.h"
      cat > db_data.h <<'HDRF'
/* Auto-generated fallback – replace by running: node db_exporter.js */
#ifndef DB_DATA_H
#define DB_DATA_H
#define DB_TOTAL_PATIENTS 20
#define DB_HIGH_RISK  6
#define DB_MOD_RISK   4
#define DB_LOW_RISK  10
#define DB_AVG_RISK_X10 542
#define DB_CNT_ABNORMAL 4
#define DB_CNT_NORMAL  10
#define DB_CNT_SPONDYL  3
#define DB_CNT_DISK     3
#define DB_SOURCE "FALLBACK"
typedef struct{const char*patient_id;int age;const char*sex;const char*date;
int risk_score_int;int abnorm_pct;const char*classification;
const char*risk_category;int cobb_x10;int bone_x10;int frac_grade;int disc_grade;}SpineRec;
#define NRECORDS 20
static const SpineRec g_records[NRECORDS]={
{"PT-001",45,"M","2026-02-26",577,99,"Abnormal","HIGH",180,-120,1,2},
{"PT-002",32,"F","2026-02-26",1000,0,"Normal","LOW",40,50,0,0},
{"PT-003",58,"M","2026-02-15",421,87,"Abnormal","HIGH",240,-210,2,3},
{"PT-004",29,"F","2026-02-20",934,6,"Normal","LOW",30,110,0,0},
{"PT-005",67,"M","2026-02-22",123,76,"DiskHernia","HIGH",80,-280,1,4},
{"PT-006",41,"F","2026-02-23",890,11,"Normal","LOW",50,20,0,1},
{"PT-007",53,"M","2026-02-24",310,91,"Spondylolisthesis","HIGH",120,-180,2,3},
{"PT-008",36,"F","2026-02-25",985,1,"Normal","LOW",20,80,0,0},
{"PT-009",72,"M","2026-02-26",89,96,"Abnormal","HIGH",310,-310,3,4},
{"PT-010",44,"F","2026-02-26",756,24,"DiskHernia","MODERATE",90,-50,1,2},
{"PT-011",38,"M","2026-02-26",921,7,"Normal","LOW",40,60,0,0},
{"PT-012",61,"F","2026-02-26",204,84,"Spondylolisthesis","HIGH",190,-240,2,3},
{"PT-013",27,"M","2026-02-27",876,12,"Normal","LOW",30,120,0,0},
{"PT-014",55,"F","2026-02-27",448,69,"DiskHernia","MODERATE",110,-100,1,3},
{"PT-015",48,"M","2026-02-27",611,38,"DiskHernia","MODERATE",70,-80,1,2},
{"PT-016",33,"F","2026-02-27",999,0,"Normal","LOW",20,100,0,0},
{"PT-017",76,"M","2026-02-28",55,98,"Abnormal","HIGH",360,-350,3,4},
{"PT-018",42,"F","2026-02-28",780,21,"Normal","LOW",50,30,0,1},
{"PT-019",59,"M","2026-02-28",315,88,"Spondylolisthesis","HIGH",140,-150,2,3},
{"PT-020",31,"F","2026-02-28",945,5,"Normal","LOW",30,90,0,0}};
typedef struct{int pi10,pt10,ll10,ss10,pr10,sp10;const char*label;}UCIRecord;
#define NUCI 10
static const UCIRecord g_uci[NUCI]={
{630,225,396,404,986,-2,"Normal"},{390,100,250,290,1144,45,"Normal"},
{688,222,500,466,1059,-35,"Normal"},{377,119,233,257,1130,216,"DiskHernia"},
{771,287,498,484,1030,65,"DiskHernia"},{880,390,817,489,1166,567,"Spondylolisthesis"},
{656,236,582,419,1240,522,"Spondylolisthesis"},{508,185,272,322,1127,321,"DiskHernia"},
{701,284,492,416,1195,654,"Spondylolisthesis"},{407,135,311,302,1312,768,"Spondylolisthesis"}};
#endif
HDRF
      ok "Fallback db_data.h written"
    fi
  fi
  sep
fi

# =============================================================================
#  PHASE 3 – Build
# =============================================================================
if [[ $RUN_ONLY -eq 0 ]]; then
  say "Phase 3 – building KarnixOS kernel"

  if [[ $USE_ISO -eq 1 ]]; then
    say "  Building ELF + ISO..."
    make iso && ok "ISO built: karnix.iso"
  else
    say "  Building ELF (direct multiboot)..."
    make build && ok "ELF built: karnix.elf"
  fi
  sep
fi

# =============================================================================
#  PHASE 4 – Launch
# =============================================================================
if [[ $BUILD_ONLY -eq 1 ]]; then
  say "Build complete (--build-only). Skipping QEMU launch."
  printf "\n  ${GRN}To run:${RST}  bash setup.sh --run-only\n\n"
  exit 0
fi

say "Phase 4 – launching QEMU simulation"
echo ""
printf "  ${GRN}QEMU controls:${RST}\n"
printf "  ${DIM}Click window     ${RST}– grab mouse / keyboard focus\n"
printf "  ${DIM}Ctrl + Alt + G   ${RST}– release (ungrab) mouse\n"
printf "  ${DIM}Ctrl + C         ${RST}– kill QEMU from this terminal\n"
echo ""
printf "  ${GRN}KarnixOS shell commands:${RST}\n"
printf "  ${DIM}help${RST}    – full command reference\n"
printf "  ${DIM}scan${RST}    – scan all patient records\n"
printf "  ${DIM}heatmap${RST} – interactive risk heatmap\n"
printf "  ${DIM}stats${RST}   – database statistics\n"
printf "  ${DIM}uci${RST}     – UCI vertebral analysis\n"
printf "  ${DIM}sel N${RST}   – select patient N (0-based)\n"
printf "  ${DIM}spine3d${RST} – launch 3D X-ray spine viewer on host\n"
printf "  ${DIM}brain${RST}   – launch Brain Tumour MRI CNN visualizer on host\n"
printf "  ${DIM}spine3dData${RST} – launch Moonday/Jester Electron interface on host\n"
printf "  ${DIM}quit${RST}    – power off the OS\n"
echo ""
sep

# Detect QEMU display backend
if [[ "$(uname)" == "Darwin" ]]; then
  DISP="-display cocoa"
else
  if [[ -n "${DISPLAY:-}" ]]; then
    DISP="-display gtk"
  else
    DISP="-display curses"
  fi
fi

QEMU="qemu-system-i386"
command -v "$QEMU" &>/dev/null || die "qemu-system-i386 not found.  Run:  brew install qemu"

# ── Spine 3D viewer – triggered by 'spine3d' command inside the OS ────────────
SPINE3D_BIN="/Users/sudharsans/Orion/spine_3d/spine3d"
BRAIN_TUMOUR_DIR="/Users/sudharsans/Documents/Karniiix/Brain_tumour"
BRAIN_TUMOUR_BIN="$BRAIN_TUMOUR_DIR/build/brain_tumour_visualizer"
MOONDAY_DIR="/Users/sudharsans/Documents/Karniiix/jester"
DEBUG_LOG="/tmp/karnix_debug_$$.log"
rm -f "$DEBUG_LOG"; : > "$DEBUG_LOG"

if [[ ! -x "$SPINE3D_BIN" ]]; then
  warn "spine3d binary not found at $SPINE3D_BIN – type 'spine3d' in OS will show error"
fi

say "3D viewer trigger active – type ${CYN}spine3d${RST} inside KarnixOS to open it"
if [[ -x "$BRAIN_TUMOUR_BIN" ]]; then
  say "Brain Tumour viewer trigger active – type ${CYN}brain${RST} inside KarnixOS to open it"
else
  warn "Brain Tumour visualizer not found at $BRAIN_TUMOUR_BIN – run Brain_tumour/setup.sh first"
fi

# Background watcher: tail the QEMU debug log; launch viewer when magic string seen
SPINE3D_DIR="$(dirname "$SPINE3D_BIN")"
SPINE3D_LOG="/tmp/spine3d_launch.log"
SPINE3D_CMD="/tmp/orion_spine3d_$$.command"
BRAIN_TUMOUR_CMD="/tmp/karnix_brain_tumour_$$.command"
MOONDAY_CMD="/tmp/karnix_moonday_$$.command"

# Pre-write a macOS .command launcher (macOS 'open' registers it with the Window Server)
cat > "$SPINE3D_CMD" << CMDEOF
#!/bin/bash
cd "$SPINE3D_DIR"
exec "$SPINE3D_BIN"
CMDEOF
chmod +x "$SPINE3D_CMD"

cat > "$BRAIN_TUMOUR_CMD" << CMDEOF
#!/bin/bash
cd "$BRAIN_TUMOUR_DIR"
exec "$BRAIN_TUMOUR_BIN" "data/brain_tumor/Testing" "models/brain_tumour_cnn.weights"
CMDEOF
chmod +x "$BRAIN_TUMOUR_CMD"

cat > "$MOONDAY_CMD" << CMDEOF
#!/bin/bash
cd "$MOONDAY_DIR"
exec npm run app
CMDEOF
chmod +x "$MOONDAY_CMD"

if [[ -f "$MOONDAY_DIR/package.json" ]]; then
  say "Moonday Electron trigger active – type ${CYN}spine3dData${RST} inside KarnixOS to run npm run app"
else
  warn "Moonday project not found at $MOONDAY_DIR"
fi

(
  _launched=0
  while IFS= read -r line; do
    if [[ "$line" == *"KARNIX_LAUNCH_SPINE3D"* ]]; then
      if [[ $_launched -eq 1 ]]; then continue; fi
      _launched=1
      printf "${GRN}[host]${RST} spine3d triggered – opening viewer...\n"
      if [[ -x "$SPINE3D_BIN" ]]; then
        # 'open' on macOS properly registers the process with the Cocoa Window Server
        open "$SPINE3D_CMD"
        printf "${GRN}[host]${RST} Launched.  If no window appears, run:  $SPINE3D_BIN\n"
      else
        printf "${YEL}[host]${RST} spine3d binary not found: $SPINE3D_BIN\n"
        printf "       Run:  cd $SPINE3D_DIR && make\n"
      fi
    fi
    if [[ "$line" == *"KARNIX_LAUNCH_BRAIN_TUMOUR"* ]]; then
      printf "${GRN}[host]${RST} brain tumour visualizer triggered – opening MRI CNN console...\n"
      if [[ -x "$BRAIN_TUMOUR_BIN" ]]; then
        open "$BRAIN_TUMOUR_CMD"
        printf "${GRN}[host]${RST} Brain Tumour visualizer launched.\n"
      else
        printf "${YEL}[host]${RST} Brain Tumour visualizer not found: $BRAIN_TUMOUR_BIN\n"
        printf "       Run:  cd $BRAIN_TUMOUR_DIR && ./setup.sh --skip-train --skip-evaluate\n"
      fi
    fi
    if [[ "$line" == *"KARNIX_LAUNCH_MOONDAY"* ]]; then
      printf "${GRN}[host]${RST} spine3dData triggered – launching Moonday with npm run app...\n"
      if [[ -f "$MOONDAY_DIR/package.json" ]]; then
        open "$MOONDAY_CMD"
        printf "${GRN}[host]${RST} Moonday Electron interface launched from $MOONDAY_DIR\n"
      else
        printf "${YEL}[host]${RST} Moonday project not found: $MOONDAY_DIR\n"
      fi
    fi
  done < <(tail -f "$DEBUG_LOG" 2>/dev/null)
) &
WATCHER_PID=$!
say "Debug watcher running (PID $WATCHER_PID)"

# Clean up watcher + tmp files on exit
trap 'kill $WATCHER_PID 2>/dev/null; rm -f "$DEBUG_LOG" "$SPINE3D_CMD" "$BRAIN_TUMOUR_CMD" "$MOONDAY_CMD"' EXIT INT TERM

sep

if [[ $USE_ISO -eq 1 ]]; then
  [[ -f karnix.iso ]] || die "karnix.iso missing – run without --run-only to build it"
  say "Booting karnix.iso via GRUB..."
  $QEMU -m 32M -vga std -no-reboot -no-shutdown -cdrom "$(pwd)/karnix.iso" -debugcon "file:$DEBUG_LOG" $DISP
else
  [[ -f karnix.elf ]] || die "karnix.elf missing – run without --run-only to build it"
  say "Booting karnix.elf directly (multiboot)..."
  $QEMU -m 32M -vga std -no-reboot -no-shutdown -kernel "$(pwd)/karnix.elf" -debugcon "file:$DEBUG_LOG" $DISP
fi
say "QEMU session ended."
