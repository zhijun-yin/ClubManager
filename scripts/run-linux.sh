#!/usr/bin/env bash
# Build/run the same C++ project on Linux or WSL. No Windows policy changes.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd -- "$root"
action="${1:-run}"
username="${2:-admin}"
port="${3:-8090}"
case "$action" in setup|build|clean|test|init|reset|run|demo) ;; *) printf 'Unknown action: %s\n' "$action" >&2; exit 2 ;; esac
if [[ ! "$port" =~ ^[0-9]+$ ]] || ((10#$port < 1 || 10#$port > 65535)); then
    printf 'Port must be between 1 and 65535.\n' >&2; exit 2
fi
for command in cmake g++ python3; do
    if ! command -v "$command" >/dev/null; then
        printf 'Missing %s. Install build-essential cmake python3 in your Linux distribution.\n' "$command" >&2; exit 1
    fi
done
# Native Linux files avoid NTFS/WSL timestamp skew in generated Makefiles and
# keep incremental builds fast. The short hash separates different checkouts.
project_key="$(printf '%s' "$root" | sha256sum | cut -c1-16)"
build="${XDG_CACHE_HOME:-$HOME/.cache}/clubmanager/$project_key"
mkdir -p -- "$root/build" "$build"
printf '%s\n' "$build" > "$root/build/wsl-build-path.txt"
if [[ "$action" == clean ]]; then
    # CMake removes only generated targets in this checkout's Linux build.
    # Never remove the source/data directory or the preview/credential files.
    if [[ -f "$build/CMakeCache.txt" ]]; then cmake --build "$build" --target clean; fi
    exit 0
fi
sdk="$root/build/wsl-openssl"
openssl_args=()
if [[ ! -f /usr/include/openssl/evp.h || -f "$sdk/.ready" ]]; then
    triplet="$(g++ -dumpmachine)"
    if [[ ! -f "$sdk/.ready" ]]; then
        for command in apt-get dpkg-deb; do
            if ! command -v "$command" >/dev/null; then
                printf 'Install the OpenSSL development package for your distribution first.\n' >&2; exit 1
            fi
        done
        mkdir -p -- "$sdk"
        download="$(mktemp -d "$sdk/packages.XXXXXX")"
        printf 'Preparing a project-local OpenSSL SDK from the configured Ubuntu repositories.\n'
        mkdir -p -- "$sdk/apt/lists/partial" "$sdk/apt/cache/archives/partial"
        # Use a fresh, unprivileged package index: the host's index may refer to
        # superseded security releases. Do not execute system update hooks.
        if [[ ! -f /etc/apt/sources.list.d/ubuntu.sources ]]; then
            printf 'Install libssl-dev using your distribution package manager first.\n' >&2; exit 1
        fi
        apt_args=(-o "Dir::State::lists=$sdk/apt/lists" -o "Dir::Cache=$sdk/apt/cache"
                  -o Dir::Etc::Parts=- -o Dir::Etc::Main=- -o Debug::NoLocking=1
                  -o Dir::Etc::sourcelist=/etc/apt/sources.list.d/ubuntu.sources -o Dir::Etc::sourceparts=-
                  -o Acquire::Languages=none -o APT::Update::Error-Mode=any)
        apt-get "${apt_args[@]}" update
        # Download only: no sudo, package installation, maintainer scripts, or
        # system settings. apt verifies archives against its repository metadata.
        (cd -- "$download" && apt-get "${apt_args[@]}" download libssl-dev libssl3t64)
        for archive in "$download"/*.deb; do dpkg-deb -x "$archive" "$sdk"; done
        # Debian splits architecture-specific headers. Normalize this private SDK
        # to the include layout expected by OpenSSL's CMake imported target.
        cp -- "$sdk/usr/include/$triplet/openssl/"*.h "$sdk/usr/include/openssl/"
        test -f "$sdk/usr/lib/$triplet/libcrypto.so"
        test -f "$sdk/usr/include/openssl/evp.h"
        touch "$sdk/.ready"
    fi
    openssl_args=("-DOPENSSL_ROOT_DIR=$sdk/usr"
                  "-DOPENSSL_INCLUDE_DIR=$sdk/usr/include"
                  "-DOPENSSL_CRYPTO_LIBRARY=$sdk/usr/lib/$triplet/libcrypto.so"
                  "-DOPENSSL_SSL_LIBRARY=$sdk/usr/lib/$triplet/libssl.so")
    # The extracted headers/shared libraries are the only SDK build inputs.
    # Remove download indexes, archives and unused static libraries after success.
    sdk_real="$(realpath -- "$sdk")"
    if [[ "$sdk_real" != "$root/build/wsl-openssl" ]]; then
        printf 'Unexpected SDK location; refusing cache cleanup.\n' >&2; exit 1
    fi
    for unused in "$sdk/apt" "$sdk"/packages.* "$sdk/usr/lib/$triplet/"*.a; do
        [[ -e "$unused" ]] || continue
        resolved="$(realpath -m -- "$unused")"
        case "$resolved" in
            "$sdk_real/"*) rm -rf -- "$unused" ;;
            *) printf 'Cleanup target escapes the SDK: %s\n' "$resolved" >&2; exit 1 ;;
        esac
    done
fi
if [[ "$action" == setup ]]; then printf 'Linux build dependencies are ready.\n'; exit 0; fi
cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release "${openssl_args[@]}"
cmake --build "$build" --parallel 4
case "$action" in
    build) ;;
    test) ctest --test-dir "$build" --output-on-failure ;;
    init|reset) exec "$build/club" auth "$action" "$username" ;;
    demo) exec "$build/club" demo ;;
    run)
        printf '\nOpen http://127.0.0.1:%s/ in your Windows browser. Ctrl+C stops the server.\n' "$port"
        if [[ ! -f club_accounts.txt ]]; then
            printf 'First use: stop the server and run ./run-wsl.ps1 -Action init.\n'
        fi
        # Local only. External exposure requires a separate explicit deployment.
        unset CLUB_LAN CLUB_SECURE_COOKIE
        export CLUB_NO_BROWSER=1 CLUB_PORT="$port"
        exec "$build/club" gui ;;
esac
