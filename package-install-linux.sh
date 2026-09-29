#!/bin/bash
#
# package-install-linux.sh
#
# Assemble a Linux install package: the built APLTerminal binary plus a
# ready-to-use $APLHOME skeleton (font, help data, curated fns, and a
# fixed list of workspaces), pulled from a live reference $APLHOME.
#
# Does NOT bundle Qt runtime libraries -- the target machine is expected
# to have qt6-base runtime packages installed (see BUILD.md). Revisit
# with linuxdeployqt if a fully self-contained package becomes necessary
# (e.g. for a Raspberry Pi image without apt access at deploy time).
#
# Usage: ./package-install-linux.sh [reference-aplhome] [output-basename]
#   reference-aplhome  Live $APLHOME to pull fonts/help.dat/fns/ws from.
#                       Defaults to $APLHOME env var if set.
#   output-basename    Defaults to apl-linux-install-<date>
#
# Run from the repo root (sibling of backend/ and frontend/), after
# build.sh has produced frontend/build/APLTerminal.

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$REPO_ROOT/frontend/build"

REF_APLHOME="${1:-$APLHOME}"
OUTF="${2:-apl-linux-install-$(date +%Y%m%d)}"

if [ -z "$REF_APLHOME" ]; then
    echo "Error: no reference \$APLHOME given and \$APLHOME is not set." >&2
    echo "Usage: $0 <reference-aplhome-dir> [output-basename]" >&2
    exit 1
fi
if [ ! -d "$REF_APLHOME" ]; then
    echo "Error: reference APLHOME '$REF_APLHOME' does not exist." >&2
    exit 1
fi

BINARY="$BUILD_DIR/APLTerminal"
if [ ! -f "$BINARY" ]; then
    echo "Error: $BINARY not found. Run build.sh first." >&2
    exit 1
fi

# Curated set of workspaces to ship -- explicit list, not *.ws, per the
# reference tree (continue.ws/medium.ws/mpl.ws/small.ws/music.ws). This
# is the one place to edit when trimming what goes out to end users.
WORKSPACES=(
    continue.ws
    medium.ws
    mpl.ws
    small.ws
    music.ws
)

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
PKG="$STAGE/$OUTF"
mkdir -p "$PKG/aplhome/fonts" "$PKG/aplhome/fns" "$PKG/aplhome/ws"

cp "$BINARY" "$PKG/"

# --- font -------------------------------------------------------------------
if [ -f "$REF_APLHOME/fonts/kapl.ttf" ]; then
    cp "$REF_APLHOME/fonts/kapl.ttf" "$PKG/aplhome/fonts/"
else
    echo "Warning: kapl.ttf not found under $REF_APLHOME/fonts/" >&2
fi

# --- help.dat -----------------------------------------------------------------
if [ -f "$REF_APLHOME/help.dat" ]; then
    cp "$REF_APLHOME/help.dat" "$PKG/aplhome/"
else
    echo "Warning: help.dat not found under $REF_APLHOME" >&2
fi

# --- fns: curated by extension --------------------------------------------
# *.fn only. Skips editor backups (.save/.orig, e.g. lifeiter2.fn.save,
# lifeiter.fn.orig) and stray non-.fn files (a stray mpl.ws has been seen
# sitting in fns/ on at least one reference tree -- workspaces belong in
# ws/ only and are handled separately below).
fn_count=0
for f in "$REF_APLHOME"/fns/*.fn; do
    [ -f "$f" ] || continue
    cp "$f" "$PKG/aplhome/fns/"
    fn_count=$((fn_count + 1))
done
echo "Copied $fn_count .fn file(s)"

# --- ws: explicit list only -------------------------------------------------
for w in "${WORKSPACES[@]}"; do
    src="$REF_APLHOME/ws/$w"
    if [ -f "$src" ]; then
        cp "$src" "$PKG/aplhome/ws/"
    else
        echo "Warning: workspace '$w' listed in WORKSPACES but not found at $src" >&2
    fi
done

# NOTE: settings.ini is deliberately NOT included. It holds this
# machine's personal window geometry/font size/dark-mode state
# (QSettings, IniFormat) -- the app creates a fresh one with defaults
# on first write if it's missing, so a new install should not inherit
# someone else's UI state.

# --- generate the target-machine installer ----------------------------------
# Runs ON the target machine, not here. Reads only from the package's own
# APLTerminal/aplhome/ (both already staged above); never touches this
# machine's $APLHOME. Uses `cp -n` (no-clobber) so re-running it against
# an existing install -- e.g. to add a newly shipped .fn file -- doesn't
# overwrite that install's own edited ws/fns/settings.ini.
cat > "$PKG/install.sh" <<'INSTALLER'
#!/bin/bash
#
# install.sh -- places this package's binary and $APLHOME contents on
# THIS machine. Existing files are left alone (no-clobber); delete
# specific files first if you want this install to replace them.
#
# Usage: ./install.sh [target-aplhome] [bin-dir]
#   target-aplhome  Defaults to $APLHOME if set, else $HOME/apl
#                   (matches get_aplhome_dir()'s own fallback in
#                   linux_platform.c).
#   bin-dir         Defaults to $HOME/.local/bin

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

TARGET_APLHOME="${1:-${APLHOME:-$HOME/apl}}"
BIN_DIR="${2:-$HOME/.local/bin}"

mkdir -p "$BIN_DIR"
cp "$SCRIPT_DIR/APLTerminal" "$BIN_DIR/"
chmod +x "$BIN_DIR/APLTerminal"

mkdir -p "$TARGET_APLHOME"
cp -rn "$SCRIPT_DIR/aplhome/." "$TARGET_APLHOME/"

echo "Installed APLTerminal to $BIN_DIR"
echo "Populated $TARGET_APLHOME (existing files, if any, were left untouched)"
echo ""
echo "Add to your shell profile if not already present:"
echo "  export APLHOME=$TARGET_APLHOME"
echo "  export PATH=\"$BIN_DIR:\$PATH\""
INSTALLER
chmod +x "$PKG/install.sh"

tar -czf "$OUTF.tar.gz" -C "$STAGE" "$OUTF"
echo ""
echo "Created $OUTF.tar.gz ($(du -h "$OUTF.tar.gz" | cut -f1))"
echo ""
echo "On the target machine:"
echo "  tar -xzf $OUTF.tar.gz"
echo "  cd $OUTF"
echo "  ./install.sh"
