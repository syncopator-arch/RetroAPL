#!/bin/bash
#
# build.sh -- build RetroAPL on Linux. Runs frontend/build.sh, which
# builds all three parts (sequencer, interpreter, Qt front end).
#
# Usage: ./build.sh [build|clean|check|run|help]
#
# Then, the first time and after each rebuild: ./setup-aplhome.sh

exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/frontend/build.sh" "$@"
