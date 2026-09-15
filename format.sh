#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

find src include tests examples \
    -type f \( -name '*.h' -o -name '*.cc' -o -name '*.cpp' \) \
    -print0 \
  | xargs -0 clang-format -i

echo "formatted."
