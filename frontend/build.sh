#!/bin/bash

# Build and test script for APL Terminal
# Usage: ./build.sh [clean|run|test]
#
# Expected layout (per CMakeLists.txt):
#   .../backend/    <- makefile, produces libapl.a
#   .../frontend/   <- CMakeLists.txt, this script, kapl.ttf
# This script must live in frontend/ (sibling of backend/).

set -e  # Exit on error

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
BACKEND_DIR="$(cd "$PROJECT_DIR/../backend" && pwd)"

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

    # Check for a C compiler (backend build)
    if ! command -v gcc &> /dev/null && ! command -v cc &> /dev/null; then
        print_error "No C compiler found (gcc or cc)"
        echo "Install with: sudo apt install build-essential"
        exit 1
    fi

    # Check for make (backend build)
    if ! command -v make &> /dev/null; then
        print_error "make not found"
        echo "Install with: sudo apt install build-essential"
        exit 1
    fi

    # Check for Qt6
    if ! dpkg -l | grep -q qt6-base-dev; then
        print_error "Qt6 development packages not found"
        echo "Install with: sudo apt install qt6-base-dev qt6-base-dev-tools"
        exit 1
    fi

    # Check for CMake
    if ! command -v cmake &> /dev/null; then
        print_error "CMake not found"
        echo "Install with: sudo apt install cmake"
        exit 1
    fi

    # Check backend directory is where we expect it
    if [ ! -f "$BACKEND_DIR/makefile" ] && [ ! -f "$BACKEND_DIR/Makefile" ]; then
        print_error "Backend makefile not found at $BACKEND_DIR"
        echo "Expected layout: backend/ and frontend/ as sibling directories"
        exit 1
    fi

    print_status "All dependencies OK"
}

# Resolve $APLHOME using the same priority as get_aplhome_dir() in
# linux_platform.c: $APLHOME, else $HOME/apl, else current directory.
resolve_aplhome() {
    if [ -n "$APLHOME" ]; then
        echo "$APLHOME"
    elif [ -n "$HOME" ]; then
        echo "$HOME/apl"
    else
        echo "."
    fi
}

# APLTerminalWidget::loadAPLFont() only ever checks $APLHOME/fonts/kapl.ttf
# (its other fallback paths are dead/commented-out code) -- so that is the
# only location that matters for the font to load at runtime.
ensure_font_deployed() {
    local aplhome
    aplhome="$(resolve_aplhome)"
    local dest="$aplhome/fonts/kapl.ttf"

    if [ -f "$dest" ]; then
        return 0
    fi

    # Canonical repo copy, per buildtree.txt: ../fonts/kapl.ttf relative
    # to frontend/. Also check frontend/ itself in case it was dropped
    # there directly.
    local src=""
    if [ -f "$PROJECT_DIR/../fonts/kapl.ttf" ]; then
        src="$PROJECT_DIR/../fonts/kapl.ttf"
    elif [ -f "$PROJECT_DIR/kapl.ttf" ]; then
        src="$PROJECT_DIR/kapl.ttf"
    fi

    if [ -n "$src" ]; then
        mkdir -p "$aplhome/fonts"
        cp "$src" "$dest"
        print_status "Deployed kapl.ttf to $dest"
    else
        print_warning "kapl.ttf not found (checked ../fonts/ and frontend/)"
        echo "Please ensure the font file is in \$APLHOME/fonts/ (currently: $aplhome/fonts/)"
    fi
}

clean_build() {
    print_status "Cleaning frontend build directory..."
    rm -rf "$BUILD_DIR"

    print_status "Cleaning backend build artifacts..."
    (cd "$BACKEND_DIR" && make clean)

    print_status "Clean complete"
}

build_backend() {
    print_status "Building backend (libapl.a)..."
    (cd "$BACKEND_DIR" && make libapl.a)

    if [ ! -f "$BACKEND_DIR/libapl.a" ]; then
        print_error "Backend build did not produce libapl.a"
        exit 1
    fi

    print_status "Backend build complete: $BACKEND_DIR/libapl.a"
}

build_project() {
    build_backend

    print_status "Building APL Terminal frontend..."

    # Create build directory if it doesn't exist
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    # Run CMake
    print_status "Running CMake..."
    cmake ..

    # Build
    print_status "Compiling..."
    make -j"$(nproc)"

    # Copy font to build directory if it exists
    # NOTE: this matches the executable-relative convention build.sh has
    # historically used. INSTALL.md/PORTING_PLAN.md instead describe the
    # font living at $APLHOME/fonts/kapl.ttf. These two conventions have
    # not yet been reconciled against the actual font-loading code -- do
    # not treat this as confirmed correct.
    if [ -f "$PROJECT_DIR/kapl.ttf" ]; then
        cp "$PROJECT_DIR/kapl.ttf" "$BUILD_DIR/"
        print_status "Copied kapl.ttf to build directory"
    fi

    print_status "Build complete!"
    echo "Executable: $BUILD_DIR/APLTerminal"
}

run_application() {
    if [ ! -f "$BUILD_DIR/APLTerminal" ]; then
        print_error "APLTerminal executable not found. Build first."
        exit 1
    fi

    print_status "Running APL Terminal..."
    cd "$BUILD_DIR"
    ./APLTerminal
}

run_tests() {
    print_status "Running tests..."
    echo ""
    echo "Manual Test Procedure:"
    echo "1. Launch the application"
    echo "2. Type 'O' (should see Circle symbol if font loaded)"
    echo "3. Press Backspace ONCE (cursor moves left, character stays)"
    echo "4. Type '|' (should compose to Rotate symbol)"
    echo ""
    echo "Expected behavior:"
    echo "  - Single backspace: cursor moves, no deletion, ready for overstrike"
    echo "  - Double backspace: normal delete operation"
    echo "  - Overstrike compose: Circle + Bar -> Rotate symbol"
    echo ""

    read -p "Press Enter to launch application for testing..."
    run_application
}

show_help() {
    echo "APL Terminal Build Script"
    echo ""
    echo "Usage: $0 [command]"
    echo ""
    echo "Commands:"
    echo "  (none)    - Check dependencies, build backend + frontend"
    echo "  clean     - Remove frontend build dir and backend build artifacts"
    echo "  run       - Run the application"
    echo "  test      - Show test instructions and run application"
    echo "  help      - Show this help message"
    echo ""
}

# Main script logic
case "${1:-build}" in
    clean)
        clean_build
        ;;
    run)
        run_application
        ;;
    test)
        run_tests
        ;;
    build)
        check_dependencies
        build_project
        ;;
    help)
        show_help
        ;;
    *)
        print_error "Unknown command: $1"
        show_help
        exit 1
        ;;
esac
