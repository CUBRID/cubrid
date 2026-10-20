#!/usr/bin/env bash
# Standalone dependency producer. This does not build or install CUBRID itself.
set -euo pipefail
source_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
state=${CUBRID_3RDPARTY_STATE:-$source_root/.thirdparty}
cmake -S "$source_root/3rdparty/standalone" -B "$state/build" -G Ninja \
  -DCMAKE_INSTALL_PREFIX="$state/prefix" \
  -DCMAKE_C_COMPILER="${CUBRID_3RDPARTY_CC:-/usr/bin/gcc}" \
  -DCMAKE_CXX_COMPILER="${CUBRID_3RDPARTY_CXX:-/usr/bin/g++}" \
  -DCUBRID_3RDPARTY_JOBS="${CUBRID_3RDPARTY_JOBS:-8}"
# Outer concurrency 1 prevents independent make -jN jobs multiplying the budget.
CCACHE_DIR="${CCACHE_DIR:-$state/ccache}" cmake --build "$state/build" --parallel 1
