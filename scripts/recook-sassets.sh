#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ASSETS="$ROOT/Assets"
BUILD_ROOT="$ROOT/build"
COOKER=""

die()
{
    echo "[Swim] ERROR: $*" >&2
    exit 1
}

find_cooker()
{
    local candidate
    local candidates=(
        "$BUILD_ROOT/linux-release/SwimAssetCooker"
        "$BUILD_ROOT/linux-debug/SwimAssetCooker"
    )

    for candidate in "${candidates[@]}"; do
        if [[ -x "$candidate" ]]; then
            COOKER="$candidate"
            return 0
        fi
    done

    if [[ -d "$BUILD_ROOT" ]]; then
        candidate="$(find "$BUILD_ROOT" -path '*/Assets' -prune -o -type f -name SwimAssetCooker -perm -111 -print -quit 2>/dev/null || true)"
        if [[ -n "$candidate" ]]; then
            COOKER="$candidate"
            return 0
        fi
    fi

    return 1
}

try_build_cooker()
{
    local build_directory
    local candidates=(
        "$BUILD_ROOT/linux-release"
        "$BUILD_ROOT/linux-debug"
    )

    command -v cmake >/dev/null 2>&1 || return 1

    for build_directory in "${candidates[@]}"; do
        if [[ ! -f "$build_directory/CMakeCache.txt" ]]; then
            continue
        fi

        echo "[Swim] SwimAssetCooker is not built; building it in: $build_directory"
        if cmake --build "$build_directory" --target SwimAssetCooker --parallel; then
            find_cooker && return 0
        fi
    done

    return 1
}

[[ -d "$ASSETS" ]] || die "Repository asset root does not exist: $ASSETS"

if ! find_cooker; then
    try_build_cooker || die "Could not find or build SwimAssetCooker. Configure/build a Linux tree first, for example build/linux-release."
fi

echo "[Swim] Asset cooker: $COOKER"
echo "[Swim] Removing previous cooked output: $ASSETS/Cooked"
rm -rf -- "$ASSETS/Cooked"

echo "[Swim] Fully recooking repository assets."
"$COOKER" "$ASSETS"

[[ -d "$ASSETS/Cooked" ]] || die "SwimAssetCooker succeeded but did not create $ASSETS/Cooked"

SASSET_COUNT="$(find "$ASSETS/Cooked" -type f -name '*.sasset' -print | wc -l | tr -d '[:space:]')"
echo "[Swim] Fresh repository cook contains $SASSET_COUNT .sasset files."
# Development builds read the repository's Assets/ directly (SWIM_DEVELOPMENT_ASSET_ROOT), so
# nothing is copied next to the executables. Packaged runs (SWIM_DEPLOY_ASSETS=ON) copy Assets/
# when SwimEngine is built.
echo "[Swim] Sasset recook completed successfully."
