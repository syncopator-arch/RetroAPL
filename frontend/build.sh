#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) Keith Doyle
#
# build.sh -- build RetroAPL on Linux: the sequencer library, the
# interpreter (backend) and the Qt front end, in that order.
#
# Usage: ./build.sh [build|clean|check|run|help]
#   (none) or build  Build all three parts.
#   clean            Remove all build output: frontend/build, and the
#                    backend's and sequencer's generated files.
#   check            Build, then run the sequencer's and the
#                    interpreter's regression tests.
#   run              Run the program just built, with $APLHOME set
#                    (see setup-aplhome.sh).
#   help             Show this list.
#
# The repository root has a build.sh that just runs this one, so
# ./build.sh works from either place. Run from anywhere: paths are
# resolved relative to this script.
#
# Layout (sibling directories):
#   backend/    makefile -> libapl.a, apl_cli
#   sequencer/  makefile -> libseq.a
#   frontend/   CMakeLists.txt, this script -> build/APLTerminal
#
# The font isn't touched here: setup-aplhome.sh installs it in
# $APLHOME/fonts, the only place the program loads it from.

set -e  # Exit on error

FRONTEND_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$FRONTEND_DIR/.." && pwd)"
BUILD_DIR="$FRONTEND_DIR/build"
BACKEND_DIR="$REPO_ROOT/backend"
SEQ_DIR="$REPO_ROOT/sequencer"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_status() {
    echo -e "${GREEN}==>${NC} $1"
}

print_error() {
    echo -e "${RED}Error:${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}Warning:${NC} $1"
}

check_dependencies() {
    print_status "Checking dependencies..."

    # Check for a C compiler (backend and sequencer)
    if ! command -v gcc &> /dev/null; then
        print_error "gcc not found"
        echo "Install with: sudo apt install build-essential"
        exit 1
    fi

    # Check for make
    if ! command -v make &> /dev/null; then
        print_error "make not found"
        echo "Install with: sudo apt install build-essential"
        exit 1
    fi

    # Check for CMake
    if ! command -v cmake &> /dev/null; then
        print_error "CMake not found"
        echo "Install with: sudo apt install cmake"
        exit 1
    fi

    # Check for Qt6, where the package manager can tell us. Elsewhere
    # CMake's own "Could not find Qt6" message does the job.
    if command -v dpkg &> /dev/null; then
        if ! dpkg -s qt6-base-dev &> /dev/null; then
            print_error "Qt6 development packages not found"
            echo "Install with: sudo apt install qt6-base-dev qt6-base-dev-tools"
            exit 1
        fi
    fi

    # Check the source tree is laid out as expected
    for d in "$BACKEND_DIR" "$SEQ_DIR"; do
        if [ ! -f "$d/makefile" ]; then
            print_error "No makefile in $d"
            echo "Expected backend/, sequencer/ and frontend/ side by side."
            exit 1
        fi
    done

    print_status "All dependencies OK"
}

clean_build() {
    print_status "Cleaning frontend build directory..."
    rm -rf "$BUILD_DIR"

    print_status "Cleaning backend build output..."
    make -C "$BACKEND_DIR" clean

    print_status "Cleaning sequencer build output..."
    make -C "$SEQ_DIR" clean

    print_status "Clean complete"
}

build_libraries() {
    # The sequencer library first (the backend's own make would also
    # build it, but only for its default target, not for libapl.a).
    print_status "Building the MIDI sequencer library (libseq.a)..."
    make -C "$SEQ_DIR" libseq.a

    # The backend's default target: libapl.a for the front end, and
    # apl_cli, the command-line interpreter the tests use.
    print_status "Building the interpreter (libapl.a, apl_cli)..."
    make -C "$BACKEND_DIR"

    for lib in "$SEQ_DIR/libseq.a" "$BACKEND_DIR/libapl.a"; do
        if [ ! -f "$lib" ]; then
            print_error "The build did not produce $lib"
            exit 1
        fi
    done
}

build_project() {
    build_libraries

    print_status "Building the Qt front end..."
    cmake -S "$FRONTEND_DIR" -B "$BUILD_DIR"
    cmake --build "$BUILD_DIR" -j"$(nproc)"

    print_status "Build complete!"
    echo "Program: $BUILD_DIR/APLTerminal"
    echo "Next, if you haven't yet: $REPO_ROOT/setup-aplhome.sh"
    echo "(run it again after each rebuild to update the installed copy)."
}

run_checks() {
    build_libraries

    print_status "Sequencer tests..."
    make -C "$SEQ_DIR" check

    print_status "Interpreter tests..."
    make -C "$BACKEND_DIR" check

    print_status "All tests passed"
}

run_application() {
    if [ ! -f "$BUILD_DIR/APLTerminal" ]; then
        print_error "APLTerminal not found. Build first."
        exit 1
    fi

    # The default folder is setup-aplhome.sh's default.
    export APLHOME="${APLHOME:-$HOME/aplhome}"
    if [ ! -f "$APLHOME/fonts/kapl.ttf" ]; then
        print_warning "No font in $APLHOME/fonts -- run $REPO_ROOT/setup-aplhome.sh first."
    fi

    print_status "Running APLTerminal (APLHOME=$APLHOME)..."
    exec "$BUILD_DIR/APLTerminal"
}

show_help() {
    echo "RetroAPL build script"
    echo ""
    echo "Usage: $0 [command]"
    echo ""
    echo "Commands:"
    echo "  (none)    - Check dependencies, build sequencer + backend + front end"
    echo "  clean     - Remove all build output (all three parts)"
    echo "  check     - Build the libraries and run their regression tests"
    echo "  run       - Run the program just built"
    echo "  help      - Show this help message"
    echo ""
}

# Main script logic
case "${1:-build}" in
    build)
        check_dependencies
        build_project
        ;;
    clean)
        clean_build
        ;;
    check)
        check_dependencies
        run_checks
        ;;
    run)
        run_application
        ;;
    help|-h|--help)
        show_help
        ;;
    *)
        print_error "Unknown command: $1"
        show_help
        exit 1
        ;;
esac
