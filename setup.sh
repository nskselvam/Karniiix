#!/usr/bin/env bash

set -euo pipefail

MODEL="${KARNIX_MODEL:-medgemma1.5:4b}"
API_URL="${KARNIX_API_URL:-http://127.0.0.1:4000}"
API_PID=
API_LOG=

if [[ ! "$MODEL" =~ ^[A-Za-z0-9][A-Za-z0-9._:/-]{0,127}$ ]]; then
    echo "KARNIX_MODEL must be a non-empty model name of at most 128 safe characters." >&2
    exit 2
fi

usage() {
    cat <<'EOF'
Usage: ./setup.sh

Sets up the local KarnixOS Patient Records API, builds the x86_64 kernel,
host visualizers, and Ollama bridge, runs backend and QEMU checks, then opens
KarnixOS in a QEMU window.

The default local development setup creates Karnix_Web/backend/.env only if
it does not exist, starts PostgreSQL, and applies migrations. Existing API
configuration is preserved. Interactive first setup securely prompts for a
password and provisions the email-less 'Hyper' account. Ollama and
KARNIX_MODEL (default: medgemma1.5:4b) must be available for chat; the model
is not downloaded automatically.

Set KARNIX_ROOT to use a checkout other than the directory containing this
script. Set KARNIX_API_URL to another HTTP localhost API URL with an explicit
port, such as http://127.0.0.1:4001. MedGemma inference runs in host Ollama,
not inside the guest.
EOF
}

if (($# > 0)); then
    echo "This script does not accept options." >&2
    usage >&2
    exit 2
fi

HOST_OS="$(uname -s)"
HOST_ARCH="$(uname -m)"
if [[ "$HOST_OS" != "Darwin" ]]; then
    echo "This setup script currently supports macOS only." >&2
    exit 1
fi
if [[ "$HOST_ARCH" != "arm64" && "$HOST_ARCH" != "x86_64" ]]; then
    echo "Unsupported macOS CPU architecture: $HOST_ARCH" >&2
    exit 1
fi

if ! command -v brew >/dev/null 2>&1; then
    for brew_path in /opt/homebrew/bin/brew /usr/local/bin/brew; do
        if [[ -x "$brew_path" ]]; then
            export PATH="$(dirname "$brew_path"):$PATH"
            break
        fi
    done
fi

if ! command -v brew >/dev/null 2>&1; then
    echo "Homebrew was not found. Install it from https://brew.sh, then rerun ./setup.sh." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="${KARNIX_ROOT:-$SCRIPT_DIR}"
if [[ ! -f "$REPO_ROOT/os/karnix64/Makefile" ]]; then
    echo "KarnixOS x86_64 kernel source not found at: $REPO_ROOT" >&2
    echo "Set KARNIX_ROOT to the repository directory and rerun ./setup.sh." >&2
    exit 1
fi
REPO_ROOT="$(cd "$REPO_ROOT" && pwd -P)"
BACKEND_DIR="$REPO_ROOT/Karnix_Web/backend"

if [[ ! -f "$BACKEND_DIR/package.json" || ! -f "$BACKEND_DIR/package-lock.json" ]]; then
    echo "Karnix Web backend package files were not found at: $BACKEND_DIR" >&2
    exit 1
fi

if [[ ! "$API_URL" =~ ^http://(localhost|127\.0\.0\.1):([0-9]{1,5})/?$ ]]; then
    echo "KARNIX_API_URL must be HTTP on localhost or 127.0.0.1, with an explicit port and no path." >&2
    exit 2
fi
API_PORT="${BASH_REMATCH[2]}"
if ((10#$API_PORT < 1 || 10#$API_PORT > 65535)); then
    echo "KARNIX_API_URL port must be between 1 and 65535." >&2
    exit 2
fi
API_URL="${API_URL%/}"

cleanup() {
    if [[ -n "$API_PID" ]]; then
        kill "$API_PID" 2>/dev/null || true
        wait "$API_PID" 2>/dev/null || true
    fi
    if [[ -n "$API_LOG" ]]; then
        rm -f "$API_LOG"
    fi
    unset LOCAL_PASSWORD LOCAL_CONFIRMATION 2>/dev/null || true
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

echo "KarnixOS x86_64 setup"
echo "Host: $HOST_OS ($HOST_ARCH)"
echo "Repository: $REPO_ROOT"
echo "Ollama model for interactive chat: $MODEL"
echo "Patient Records API: $API_URL"
echo

packages_to_install=()
for package in x86_64-elf-gcc limine qemu nasm xorriso cmake nlohmann-json \
    ollama sdl2 sdl2_ttf node postgresql@16; do
    if ! brew list --versions "$package" >/dev/null 2>&1; then
        packages_to_install+=("$package")
    fi
done

if ((${#packages_to_install[@]} > 0)); then
    echo "Installing missing Homebrew packages: ${packages_to_install[*]}"
    brew install "${packages_to_install[@]}"
else
    echo "Required Homebrew packages are installed."
fi

for command_name in x86_64-elf-g++ x86_64-elf-ld limine \
    qemu-system-x86_64 nasm xorriso cmake ollama curl node npm; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Required command is unavailable after dependency setup: $command_name" >&2
        exit 1
    fi
done

NODE_MAJOR="$(node -p 'Number(process.versions.node.split(".")[0])')"
if ((NODE_MAJOR < 20)); then
    echo "The Karnix API requires Node.js 20 or newer (found $(node --version))." >&2
    exit 1
fi

if [[ ! -f "$BACKEND_DIR/.env" ]]; then
    echo "Creating a local-development API configuration."
    DB_USER="$(id -un)"
    if [[ ! "$DB_USER" =~ ^[A-Za-z0-9_-]+$ ]]; then
        echo "Cannot safely configure the local PostgreSQL role '$DB_USER'." >&2
        echo "Create $BACKEND_DIR/.env with your DATABASE_URL and rerun setup." >&2
        exit 1
    fi
    JWT_SECRET="$(openssl rand -hex 32)"
    (
        umask 077
        cat > "$BACKEND_DIR/.env" <<EOF
NODE_ENV=development
HOST=127.0.0.1
PORT=${API_PORT}
DATABASE_URL=postgresql://${DB_USER}@127.0.0.1:5432/karnix_auth
DATABASE_SSL=false
JWT_SECRET=${JWT_SECRET}
JWT_ISSUER=karnix-auth-api
JWT_AUDIENCE=karnix-web
CORS_ORIGIN=http://localhost:3000
BCRYPT_ROUNDS=12
ACCESS_TOKEN_TTL=15m
ACCESS_TOKEN_MAX_AGE_SECONDS=900
REFRESH_TOKEN_TTL_DAYS=7
AUTH_RATE_LIMIT=10
BODY_LIMIT_BYTES=16384
TRUST_PROXY=false
COOKIE_SECURE=false
OLLAMA_BASE_URL=http://127.0.0.1:11434
KARNIX_MODEL=${MODEL}
EOF
    )
    chmod 600 "$BACKEND_DIR/.env"

    POSTGRES_PREFIX="$(brew --prefix postgresql@16)"
    export PATH="$POSTGRES_PREFIX/bin:$PATH"
    echo "Starting the local PostgreSQL 16 service."
    brew services start postgresql@16
    attempt=0
    until pg_isready -h 127.0.0.1 -p 5432 >/dev/null 2>&1; do
        sleep 1
        attempt=$((attempt + 1))
        if ((attempt >= 30)); then
            echo "PostgreSQL did not become ready on 127.0.0.1:5432." >&2
            exit 1
        fi
    done
    if ! createdb -h 127.0.0.1 -U "$DB_USER" karnix_auth 2>/dev/null; then
        if ! psql -h 127.0.0.1 -U "$DB_USER" -d karnix_auth \
            -Atqc 'SELECT 1' >/dev/null 2>&1; then
            echo "Could not create or connect to local database 'karnix_auth'." >&2
            echo "Check PostgreSQL, or create $BACKEND_DIR/.env with a working DATABASE_URL." >&2
            exit 1
        fi
    fi
fi

echo
echo "Installing backend dependencies and applying database migrations..."
(
    cd "$BACKEND_DIR"
    npm ci
    npm test
    npm run migrate
)

if [[ -t 0 && -t 1 ]]; then
    if (cd "$BACKEND_DIR" && npm run --silent local-account -- --exists hyper); then
        echo "The local 'Hyper' account is already present; leaving its password unchanged."
    else
        echo
        echo "Create the email-less local Patient Records account 'Hyper'."
        echo "Use a password of at least 10 characters with uppercase, lowercase, and a number."
        read -r -s -p "Password for Hyper: " LOCAL_PASSWORD
        printf '\n'
        read -r -s -p "Confirm password: " LOCAL_CONFIRMATION
        printf '\n'
        if [[ "$LOCAL_PASSWORD" != "$LOCAL_CONFIRMATION" ]]; then
            echo "Password confirmation did not match." >&2
            exit 1
        fi
        if (( ${#LOCAL_PASSWORD} < 10 )); then
            echo "Password must be at least 10 characters." >&2
            exit 1
        fi
        printf '%s\n%s\n' "$LOCAL_PASSWORD" "$LOCAL_CONFIRMATION" |
            (cd "$BACKEND_DIR" && npm run --silent local-account -- --create Hyper)
        unset LOCAL_PASSWORD LOCAL_CONFIRMATION
    fi
else
    echo "No terminal available; skipping interactive creation of the local 'Hyper' account."
fi

if curl --silent --show-error --fail "$API_URL/ready" >/dev/null 2>&1; then
    echo "A ready Karnix API is already listening at $API_URL."
else
    API_LOG="$(mktemp)"
    (
        cd "$BACKEND_DIR"
        export HOST=127.0.0.1 PORT="$API_PORT"
        exec node src/server.js
    ) >"$API_LOG" 2>&1 &
    API_PID=$!
    attempt=0
    until curl --silent --show-error --fail "$API_URL/ready" >/dev/null 2>&1; do
        if ! kill -0 "$API_PID" 2>/dev/null; then
            cat "$API_LOG" >&2
            echo "Karnix API failed to start." >&2
            exit 1
        fi
        sleep 1
        attempt=$((attempt + 1))
        if ((attempt >= 30)); then
            cat "$API_LOG" >&2
            echo "Karnix API did not become ready at $API_URL/ready." >&2
            exit 1
        fi
    done
    echo "Karnix Patient Records API is ready at $API_URL."
fi

echo
echo "Building host apps, running backend tests, and checking KarnixOS..."
KARNIX_API_URL="$API_URL" make -C "$REPO_ROOT/os/karnix64" \
    host-apps check check-desktop

if [[ ! -t 0 || ! -t 1 ]]; then
    echo
    echo "Setup and checks passed, but no interactive terminal is available."
    echo "Rerun ./setup.sh in a terminal to launch KarnixOS with the API bridge."
    exit 0
fi

if ! curl --silent --show-error --fail \
    http://127.0.0.1:11434/api/tags >/dev/null 2>&1; then
    echo "Ollama is not running at 127.0.0.1:11434." >&2
    echo "Start the Ollama app or run 'ollama serve', then rerun ./setup.sh." >&2
    exit 1
fi
if ! ollama show "$MODEL" >/dev/null 2>&1; then
    echo "The configured model '$MODEL' is not installed in Ollama." >&2
    echo "Install it explicitly with: ollama pull \"$MODEL\"" >&2
    exit 1
fi

echo
echo "Launching the KarnixOS text desktop in QEMU."
echo "Press 6 for Patient Records; sign in with an existing Karnix account."
echo "Press 4 or 5 for the host visualizers."
KARNIX_API_URL="$API_URL" make -C "$REPO_ROOT/os/karnix64" run
