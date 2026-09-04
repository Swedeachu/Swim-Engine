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

DEPENDENCY_CACHE="$ROOT/.cache/cpm"

if [[ ! -d "$DEPENDENCY_CACHE" ]] || ! compgen -G "$DEPENDENCY_CACHE/cpm/CPM_*.cmake" > /dev/null; then
    echo "Dependency cache is not bootstrapped. Run scripts/build-linux-clean.sh once with network access before using a soft build." >&2
    exit 1
fi

cd "$ROOT"
echo "[Swim] Soft Linux build: $PRESET"
echo "[Swim] Dependency downloads are disabled; cached CPM sources will be reused."

cmake --preset "$PRESET" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
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
