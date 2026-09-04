#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRESET="linux-release"

if [[ "${1:-}" == "--debug" ]]; then
    PRESET="linux-debug"
elif [[ -n "${1:-}" && "${1:-}" != "--release" ]]; then
    echo "Usage: $0 [--debug|--release]" >&2
    exit 2
fi

DEPENDENCY_CACHE_ROOT="$ROOT/.cache"
PHYSX_SHORT_WORKTREE="$ROOT/build/.px"
LINUX_BUILD_DIRECTORIES=(
    "$ROOT/build/linux-release"
    "$ROOT/build/linux-debug"
)

cd "$ROOT"
echo "[Swim] Clean Linux build: $PRESET"

# A Windows build from the same checkout may have left the legacy .px junction.
# If cmd.exe is reachable through WSL, remove that link without traversing its
# target first. For the current implementation .px is a normal Git worktree and
# the rm below handles it directly.
if command -v cmd.exe >/dev/null 2>&1 && command -v wslpath >/dev/null 2>&1; then
    PHYSX_SHORT_WORKTREE_WINDOWS="$(wslpath -w "$PHYSX_SHORT_WORKTREE")"
    cmd.exe /d /c "rmdir \"$PHYSX_SHORT_WORKTREE_WINDOWS\"" >/dev/null 2>&1 || true
fi

echo "[Swim] Removing shared PhysX worktree/legacy junction: $PHYSX_SHORT_WORKTREE"
rm -rf -- "$PHYSX_SHORT_WORKTREE"

for BUILD_DIRECTORY in "${LINUX_BUILD_DIRECTORIES[@]}"; do
    echo "[Swim] Removing Linux build tree: $BUILD_DIRECTORY"
    rm -rf -- "$BUILD_DIRECTORY"
done

echo "[Swim] Removing complete repository dependency cache: $DEPENDENCY_CACHE_ROOT"
rm -rf -- "$DEPENDENCY_CACHE_ROOT"

for REMOVED_PATH in "$PHYSX_SHORT_WORKTREE" "$DEPENDENCY_CACHE_ROOT" "${LINUX_BUILD_DIRECTORIES[@]}"; do
    if [[ -e "$REMOVED_PATH" || -L "$REMOVED_PATH" ]]; then
        echo "[Swim] ERROR: Clean-build reset left generated state behind at '$REMOVED_PATH'." >&2
        exit 1
    fi
done

echo "[Swim] Clean state verified. Pulling every pinned dependency from scratch."
cmake --preset "$PRESET" -DFETCHCONTENT_FULLY_DISCONNECTED=OFF
cmake --build --preset "$PRESET" --parallel

# The Linux foundation configure builds every test suite whose dependencies it
# has (the legacy renderer/PhysX suites are simply not compiled there), so the
# same single test program validates this build too.
echo "[Swim] Building and running the Swim test suite."
cmake --build --preset "$PRESET" --target SwimTests --parallel
TEST_BINARY="$ROOT/build/$PRESET/SwimTests"
if [[ ! -x "$TEST_BINARY" ]]; then
    echo "[Swim] ERROR: SwimTests built but '$TEST_BINARY' is missing." >&2
    exit 1
fi
"$TEST_BINARY"
