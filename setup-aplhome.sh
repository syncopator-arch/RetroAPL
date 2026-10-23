#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) Keith Doyle
#
# setup-aplhome.sh
#
# Create or update an $APLHOME folder from this repository, and -- if
# the program has been built -- install it in $APLHOME/bin and add
# RetroAPL to the desktop's applications menu.
#
# Usage: ./setup-aplhome.sh [target-folder]
#   target-folder  Defaults to $APLHOME if set, else ~/aplhome.
#
# Two kinds of files are handled differently:
#   Program files -- bin/retroapl, fonts/kapl.ttf, help.dat,
#       bin/retroapl-icon.png (the menu entry's icon),
#       resources/*.md. These belong to RetroAPL. They're copied if
#       missing and replaced if this repository has a different
#       version, so re-running setup after an update (and rebuild)
#       brings them up to date.
#   User files -- ws/, fns/, images/, midi/.
#       Starting examples only. Copied if missing, never replaced,
#       so your own workspaces and functions are never overwritten.
#
# Run from anywhere; paths are resolved relative to this script.

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$REPO_ROOT/aplhome"
BINARY="$REPO_ROOT/frontend/build/retroapl"
TARGET="${1:-${APLHOME:-$HOME/aplhome}}"

if [ ! -d "$SRC" ]; then
    echo "Error: $SRC not found -- run this from a RetroAPL checkout." >&2
    exit 1
fi
if [ ! -f "$REPO_ROOT/fonts/kapl.ttf" ]; then
    echo "Error: $REPO_ROOT/fonts/kapl.ttf not found." >&2
    exit 1
fi

mkdir -p "$TARGET"
TARGET="$(cd "$TARGET" && pwd)"     # full path, for the menu entry and APLHOME line

echo "Setting up \$APLHOME in: $TARGET"
mkdir -p "$TARGET/bin" "$TARGET/fonts" "$TARGET/resources" \
         "$TARGET/ws" "$TARGET/fns" "$TARGET/images" "$TARGET/midi"

added=0; updated=0; kept=0

# Program file: copy if missing, replace if different.
install_program_file() {
    local src="$1" dest="$2"
    if [ ! -e "$dest" ]; then
        cp "$src" "$dest"
        echo "  added    ${dest#$TARGET/}"
        added=$((added + 1))
    elif ! cmp -s "$src" "$dest"; then
        cp "$src" "$dest"
        echo "  updated  ${dest#$TARGET/}"
        updated=$((updated + 1))
    fi
}

# User file: copy only if missing.
install_user_file() {
    local src="$1" dest="$2"
    if [ ! -e "$dest" ]; then
        cp "$src" "$dest"
        echo "  added    ${dest#$TARGET/}"
        added=$((added + 1))
    else
        kept=$((kept + 1))
    fi
}

# --- program files ---------------------------------------------------------
install_program_file "$REPO_ROOT/fonts/kapl.ttf" "$TARGET/fonts/kapl.ttf"
# The menu icon: the simple design, which stays readable at the sizes
# the desktop shows it (the dock's 48-64 pixels); the full art if that
# isn't there.
for icon in retroapl-icon-small.png retroapl-icon.png; do
    if [ -f "$REPO_ROOT/docs/images/$icon" ]; then
        install_program_file "$REPO_ROOT/docs/images/$icon" "$TARGET/bin/retroapl-icon.png"
        break
    fi
done
[ -f "$SRC/help.dat" ] && install_program_file "$SRC/help.dat" "$TARGET/help.dat"
for f in "$SRC"/resources/*; do
    [ -f "$f" ] && install_program_file "$f" "$TARGET/resources/$(basename "$f")"
done

have_binary=0
if [ -f "$BINARY" ]; then
    install_program_file "$BINARY" "$TARGET/bin/retroapl"
    chmod +x "$TARGET/bin/retroapl"
    have_binary=1
    # The program was called APLTerminal before V106. Its old copy is a
    # program file, so it goes, now that the new one is in place.
    if [ -e "$TARGET/bin/APLTerminal" ]; then
        rm -f "$TARGET/bin/APLTerminal"
        echo "  removed  bin/APLTerminal (the program is now bin/retroapl)"
    fi
fi

# --- user files ------------------------------------------------------------
for dir in ws fns images midi; do
    for f in "$SRC/$dir"/*; do
        [ -f "$f" ] && install_user_file "$f" "$TARGET/$dir/$(basename "$f")"
    done
done

echo ""
echo "Done: $added added, $updated updated, $kept existing user file(s) left as they were."

# --- applications menu entry -------------------------------------------------
# Sets APLHOME itself, so launching from the menu works whether or not
# APLHOME is set in your shell profile (the desktop doesn't read ~/.bashrc).
if [ "$have_binary" = 1 ]; then
    APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
    DESKTOP="$APPS/retroapl.desktop"
    mkdir -p "$APPS"
    {
        echo "[Desktop Entry]"
        echo "Type=Application"
        echo "Name=RetroAPL"
        echo "Comment=APL interpreter in the style of the IBM 2741 terminal"
        echo "Exec=env \"APLHOME=$TARGET\" \"$TARGET/bin/retroapl\""
        echo "Path=$TARGET"
        [ -f "$TARGET/bin/retroapl-icon.png" ] && echo "Icon=$TARGET/bin/retroapl-icon.png"
        # Ties the program's windows to this entry under X11, however
        # it was started, so the dock shows them as RetroAPL with this
        # icon. Their WM_CLASS is the program's name, retroapl, which
        # matches this file's name anyway; this says it outright.
        echo "StartupWMClass=retroapl"
        echo "Terminal=false"
        echo "Categories=Development;"
    } > "$DESKTOP"
    echo ""
    echo "RetroAPL is installed in $TARGET/bin and in your applications menu."
else
    echo ""
    echo "The program hasn't been built yet (no $BINARY)."
    echo "Run ./build.sh, then run this script again to install it"
    echo "in $TARGET/bin and add it to your applications menu."
fi

# --- APLHOME reminder --------------------------------------------------------
if [ "$APLHOME" != "$TARGET" ]; then
    echo ""
    echo "To run RetroAPL from a terminal, add this line to your ~/.bashrc:"
    echo ""
    echo "  export APLHOME=\"$TARGET\""
fi
