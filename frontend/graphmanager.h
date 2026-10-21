#ifndef GRAPHMANAGER_H
#define GRAPHMANAGER_H

#include <QObject>
#include <QMap>
#include <QSet>
#include <QVector>
#include <QString>
#include <QColor>
#include <QFont>
#include <QWidget>
#include <QPointer>

class GraphWindow;

// Lives on the GUI thread. Owns the window-number -> GraphWindow registry
// and is the only thing that touches GraphWindow instances directly.
//
// Constructed once at startup and pointed to by the module-level
// g_graphManager global (declared here, defined in graphmanager.cpp) --
// same pattern as main.cpp's g_terminal. graph_bridge.cpp's gw_* functions
// are the only other thing that reaches this class, always via
// QMetaObject::invokeMethod() from the engine thread -- BlockingQueuedConnection
// for calls that return a value (open/nums/size), QueuedConnection for the
// fire-and-forget Phase 2 drawing calls below. Every public slot below
// assumes that calling convention -- none of them are safe to call directly
// from a non-GUI thread.
class GraphManager : public QObject
{
    Q_OBJECT

public:
    explicit GraphManager(QObject *parent = nullptr);

    // The widget that should reclaim keyboard focus after a graphics
    // window opens (the APL terminal, in practice). Set once from
    // main.cpp right where g_terminal/g_graphManager are wired up --
    // NOT reachable via g_terminal directly, since that global is
    // declared `static` in main.cpp (internal linkage, private to that
    // translation unit) rather than `extern`-able the way g_graphManager
    // itself is. QPointer so a focus-target that outlives GraphManager
    // isn't required -- open() simply no-ops the focus-reclaim if this
    // has gone null.
    void setFocusTarget(QWidget *widget) { m_focusTarget = widget; }

    // FDIALOG's native-vs-Qt-drawn dialog toggle. Set from the Settings
    // dialog (main.cpp, APLSettingsDialog) -- defaults to false (Qt's
    // own dialog), matching what's already shipped and confirmed
    // working; the native picker is opt-in, not the default, per
    // "additive over breaking." See fileDialog()'s own comment for the
    // GNOME oversized/attached-window issue this exists to let the
    // person opt back out of, if they'd rather have native OS
    // integration (bookmarks, recent files) instead.
    void setUseNativeFileDialog(bool useNative) { m_useNativeFileDialog = useNative; }

public slots:
    // Returns the new handle (the lowest currently-unused one, starting
    // at 1 -- so closing window 1 and opening a new one reuses 1 rather
    // than counting up forever), or -1 if w/h are unusable (<=0).
    //
    // visible == false (WOPEN's optional 5th element, 0): the window is
    // fully registered -- handle, drawing, pen, tag, everything -- but
    // never shown, so no OS-level window exists yet (Qt only creates one
    // on first show; confirmed in the sandbox). x,y still applies: it's
    // recorded via move() and becomes where the window appears when
    // WCTRL 6 later shows it; -1,-1 leaves placement to the window
    // manager at that point instead. Defaults to true so nothing that
    // calls this without the flag changes behavior.
    long open(const QString &title, long x, long y, long w, long h,
              bool visible = true);

    // WILOAD. Same handle/placement conventions as open() (x,y == -1,-1
    // is system-default placement, same lowest-free-handle rule), but
    // takes an already-decoded image instead of a blank w,h fill --
    // window size is the image's own size. Returns -1 if image is null
    // (gw_load_image() is expected to have already checked this before
    // ever reaching the GUI thread, but checked again here too rather
    // than trusting the caller silently). visible means exactly what it
    // does for open() (WILOAD's optional 3rd element).
    long openImage(const QString &title, const QImage &image, long x, long y,
                   bool visible = true);

    // No-op if handle isn't currently open.
    void close(long handle);

    // Currently-open handles, in creation order (oldest first, most
    // recently opened last) -- not incidentally-whatever-QMap-does order.
    QVector<long> nums() const;

    // WNUMS, filtered by tag. pattern ending in '*' is a prefix match
    // (everything before the '*'); otherwise it's an exact match --
    // deliberately just that, no interior/repeated wildcards, no
    // escaping. A window with no tag set (empty string) matches only
    // an explicit empty pattern under exact-match rules -- a plain
    // consequence of ordinary string comparison, not special-cased for
    // "untagged" as its own concept. Same creation-order vector as the
    // niladic nums() above, just pre-filtered.
    QVector<long> nums(const QString &pattern) const;

    // handle == -1: desktop dimensions as [width, height] -- same
    // sentinel convention open()'s x,y already uses for "no explicit
    // value given," reused here rather than a separate has-handle flag.
    // Any other handle: that window's current backing-image
    // [width, height] if it's currently open, or an empty vector if it
    // isn't (empty, not [0,0], so a real failure is distinguishable from
    // a legitimately tiny window). 0 is deliberately just an ordinary
    // (always-invalid) handle here, not a second way to ask for desktop
    // size.
    //
    // Hidden windows: size is reported normally (the backing image is
    // just as real as a shown window's). Position is reported as -1,-1
    // if the window has never been placed -- opened hidden with -1,-1
    // and never moved or shown since (see GraphWindow::isPlaced()) --
    // rather than Qt's meaningless 0,0 default for an unplaced widget.
    // -1,-1 is the same value WOPEN/WILOAD read as "default placement,"
    // so WSIZE's x,y still round-trips correctly into a later open.
    QVector<long> size(long handle) const;

    // WPIXEL. points is a flattened x,y stream (2 longs per point,
    // points.size() a multiple of 2 -- grfuncs.c/graph_bridge.cpp's job
    // to guarantee that, not this method's). Returns one packed 24-bit
    // color per input point, in order -- same 0xRRGGBB packing
    // unpackColor() already uses, confirmed to mirror it exactly rather
    // than assumed (Format_RGB32's always-0xFF alpha byte just gets
    // masked off, never carries real data). A point outside the
    // window's current bounds gets -1 in its slot rather than failing
    // the whole call. Returns an EMPTY vector if handle isn't currently
    // open at all -- same "empty means failure" convention size() uses
    // -- which is how a wholly bad handle is distinguished from a
    // legitimate (if all-out-of-bounds) result. Fully synchronous, same
    // category as size() -- no waiting on user input.
    QVector<long> pixel(long handle, const QVector<long> &points) const;

    // No-op if handle isn't currently open.
    void setTitle(long handle, const QString &title);

    // WCTRL. opcode: 0=minimize, 1=restore, 2=front (raise), 3=back
    // (lower), 4=move (uses x,y; every other opcode ignores them),
    // 5=hide, 6=show.
    //
    // Hidden windows (5/6, added with WOPEN/WILOAD's visibility flag):
    //   - 5 is plain QWidget::hide(). Confirmed in the sandbox that
    //     hide() does NOT send a closeEvent, so WA_DeleteOnClose never
    //     fires and the window (handle, image, pen, tag, position)
    //     survives intact.
    //   - 6 goes through reveal(), the same show + focus-reclaim sequence
    //     a visible WOPEN uses, so showing later never steals focus any
    //     more than opening visibly does (and shares the same Mutter
    //     map-time limitation described in registerWindow()). No-op on a
    //     window that isn't hidden, so a redundant show doesn't trigger a
    //     pointless focus reclaim. A window hidden while minimized comes
    //     back NORMAL, not minimized -- reveal() re-creates its native
    //     window first (GraphWindow::recreateNativeWindow()).
    //     Originally decided the other way ("comes back how it was," Qt's
    //     default); reversed after real GNOME testing showed that path
    //     leaves Qt believing a visibly-normal window is minimized, which
    //     then breaks WCTRL 0. See reveal()'s own comment.
    //   - 0 and 1 are no-ops on a hidden window. Both are showXxx() calls
    //     underneath, and the sandbox confirmed either one would make a
    //     hidden window appear -- 6 is meant to be the only way to show
    //     one, so "restore" can't quietly act as a second "show."
    //   - 2, 3 and 4 need no guard: raise()/lower() do nothing visible on
    //     a hidden window, and move() just records the position, which is
    //     where 6 will later show it (all confirmed in the sandbox). 4
    //     also marks the window placed (see size()).
    //
    // Plain
    // QWidget-level calls, same category/precedent as main.cpp's own
    // sys_ctrl() for the terminal window (showMinimized/showNormal), plus
    // raise()/lower()/move() for the three opcodes CTRL has no equivalent
    // of. move()'s x,y is the same upper-left coordinate system
    // registerWindow() already places new windows in. No-op if handle
    // isn't currently open, or if opcode is out of range (grfuncs.c
    // already validates the opcode; out-of-range here is defensive only).
    // Deliberately no un-minimize side effect on front/back -- raise()/
    // lower() only reorder stacking, they don't restore a minimized
    // window, and that's intentional: each opcode does exactly one thing.
    //
    // KNOWN LIMITATION, confirmed via manual testing (not fixable here):
    // opcode 1 (restore) is a silent no-op under full GNOME Shell
    // (Activities/top bar/taskbar) specifically -- distinct from plain
    // Mutter, which restores correctly. GNOME Shell deliberately refuses
    // to let an application un-minimize itself programmatically, on X11
    // sessions as well as Wayland -- confirmed by ruling out a
    // showMinimized()/showNormal() timing race (fails even with a
    // multi-second gap between calls) and ruling out the window just
    // being stacked behind something else (not reachable via alt-tab
    // either). Manually restoring via the taskbar/Activities works every
    // time -- the window is never actually lost, GNOME Shell is just
    // declining the app's own request. No client-side workaround found;
    // this is the same category of platform restriction as the
    // Wayland xdg-shell protocol having no un-minimize request at all
    // (see gw_ctrl()'s header comment in graph_bridge.h), just enforced
    // by GNOME Shell's own policy layer even on X11. CONFIRMED PRESENT
    // under GNOME Shell; NOT present under plain Mutter (no shell) or
    // openbox, both re-tested directly. Windows behavior not yet
    // checked.
    void ctrl(long handle, long opcode, long x, long y);

    // WTAG. Sets handle's tag -- plain, unconditional assignment; an
    // empty string is a legitimate value here (it just means the
    // window matches nothing via nums(pattern) below until re-tagged),
    // not a distinct "clear" branch -- same shape/convention as
    // setTitle(). No-op if handle isn't currently open.
    void setTag(long handle, const QString &tag);

    // WTAGOF. Returns handle's current tag, or an empty string if
    // handle isn't currently open OR is open but untagged -- these are
    // deliberately not distinguished, same "nothing to report"
    // convention pen()/size()/pixel() already use.
    QString tagOf(long handle) const;

    // PEN. No-op if handle isn't open -- matches setTitle()'s existing
    // convention for an unknown handle. width is clamped >=1 inside
    // GraphWindow::setPen(), not here. width == -1 is a sentinel meaning
    // "leave the current width unchanged" -- lets a color-only PEN call
    // change color without a round trip (the set path is fire-and-
    // forget, so the engine thread can't read the current width back
    // itself to re-supply it). r == -1 is the same sentinel, now also
    // covering color -- "leave color unchanged, set width only" (PEN's
    // newer size-only set case; g,b are -1 too by construction but only
    // r is checked). The two sentinels are independent, not paired --
    // either, both, or neither can apply on a given call. Resolved here,
    // not in GraphWindow: the current width/color are simply read off
    // the window and re-supplied to GraphWindow::setPen() unchanged, so
    // GraphWindow's own setter stays a plain unconditional "set both"
    // with no sentinel awareness of its own.
    void setPen(long handle, int r, int g, int b, int width);

    // PEN query. Returns the window's current pen as [r, g, b, width]
    // (4 elements), or an empty vector if handle isn't currently open --
    // same "empty means failure" convention size()/pixel() already use.
    QVector<long> pen(long handle) const;

    // POLY. ops is a flattened p,x,y triple stream (3 longs per point,
    // ops.size() a multiple of 3 -- grfuncs.c/graph_bridge.cpp's job to
    // guarantee that, not this method's). p == -1 is move (pen up, no
    // draw, just repositions the window's tracked pen position);
    // anything else draws a line from the window's current pen position
    // to (x,y) using its current PEN color+width, strict 2-state per the
    // simplified (non-inline-pen-switch) decode. No-op if handle isn't
    // open.
    void poly(long handle, const QVector<long> &ops);

    // WPOINTS. data is a flattened row stream, ncols wide per row (2 or
    // 3 -- caller-determined from the APL arg's shape, same as
    // grfuncs.c already does for other shape-driven decisions). ncols==2
    // rows are (x,y), plotted in the window's current pen color. ncols==3
    // rows are (x,y,packedColor), the 3rd column overriding color for
    // just that point -- current pen *size* still applies. No-op if
    // handle isn't open.
    void points(long handle, const QVector<long> &data, int ncols);

    // WCLEAR. packedColor is 24-bit RGB (0xRRGGBB), same packing as
    // WPOINTS' per-point override column. No-op if handle isn't open.
    void clear(long handle, long packedColor);

    // FPOLY. ops is a flattened x,y pair stream (2 longs per vertex,
    // ops.size() a multiple of 2 -- grfuncs.c/graph_bridge.cpp's job to
    // guarantee that, not this method's). Fills the polygon those
    // vertices form (implicitly closed, last vertex back to first) using
    // the window's current PEN color, under Qt::OddEvenFill -- correct
    // for self-intersecting shapes (e.g. a figure-8) with no special-
    // casing needed. No border stroke: painter's pen is explicitly set
    // to Qt::NoPen rather than left at QPainter's default (which would
    // otherwise draw a 1px black outline nobody asked for). No-op if
    // handle isn't open.
    void fpoly(long handle, const QVector<long> &ops);

    // WTEXT. Draws `text` in the window's current PEN color, in the
    // window's current WFONT size (pixels -- see GraphWindow::
    // fontPixelSize()), via the shared textFont() helper below. (x,y) is
    // Qt's native drawText() baseline, not a top-left anchor, taken
    // as-is. Text antialiasing is explicitly turned on for this call --
    // independent of WPOINTS' deliberately-off antialiasing for bulk
    // rect plotting, since infrequent text and bulk point plotting have
    // different quality/cost tradeoffs. No-op if handle isn't open.
    void text(long handle, const QString &text, long x, long y);

    // WFONT query. Returns the window's current font size in pixels, or
    // -1 if handle isn't currently open (real sizes are always >=1, so
    // -1 is unambiguous -- same return convention open()'s -1 failure
    // uses for a single scalar result, rather than an empty QVector for
    // a value that's only ever one number).
    long fontSize(long handle) const;

    // WFONT set. pixelSize == -1 is the "reset to default" sentinel
    // (GraphWindow::kDefaultFontPixelSize), resolved here rather than in
    // GraphWindow -- same division of labor as setPen()'s -1 sentinels.
    // Anything else is taken as-is; grfuncs.c has already rejected
    // values < 1 other than -1. Returns the PRIOR size (same
    // set-returns-prior convention sysfuncs.c's IO/INTDIV/WI/DIG use),
    // or -1 if handle isn't open, in which case nothing is changed.
    long setFontSize(long handle, int pixelSize);

    // WFONT face set. spec is a comma-separated fallback list of family
    // names; each is trimmed and empty entries dropped. The whole spec
    // "DEFAULT" (case-insensitive, once trimmed) clears the list, back to
    // the app's default face -- recognized only as the entire spec; inside
    // a list it's just a name that won't be found (harmless, since Qt's
    // own last resort is the default face anyway). Size untouched.
    // Returns the window's size (unchanged, so also the prior size), or
    // -1 if handle isn't open, in which case nothing is changed.
    long setFontFace(long handle, const QString &spec);

    // WFONT face query. The family Qt actually resolves the window's
    // current text font to (QFontInfo), in Qt's own spelling -- shows a
    // silent substitution, or which entry of a fallback list won. Empty
    // QString if handle isn't open, same convention as tagOf().
    QString fontFace(long handle) const;

    // TWID. Width in pixels `text` would occupy if drawn by text() in
    // this window right now -- same textFont() helper, same QString (the
    // caller converts it exactly the way gw_text() does), measured
    // against the window's own backing image as the paint device, so the
    // number can't drift from what text() actually draws. Horizontal
    // advance (where the next text would start), not ink bounds.
    // Rounded to the nearest whole pixel. Returns -1 if handle isn't
    // currently open.
    long textWidth(long handle, const QString &text) const;

    // WFILL. Flood fill ("paint bucket") from a single (x,y) seed point,
    // replacing every 4-connected pixel matching the seed pixel's
    // starting color with the window's current PEN color. tol is a
    // per-channel tolerance, 0-255 scale (same as every other color value
    // in this module) -- a candidate pixel qualifies if every one of its
    // r,g,b channels is within tol of the SEED pixel's own original
    // color, always compared against that fixed starting color, never
    // neighbor-to-neighbor (standard bucket-fill definition; avoids
    // drifting across a gradient one small step at a time). Negative tol
    // is clamped to 0 here (exact match) -- same permissive-clamp spirit
    // as setPen()'s width clamp; grfuncs.c doesn't do this clamping
    // itself. 4-connected, not 8 -- confirmed by direct testing against a
    // real drawn shape with a shallow diagonal edge to actually stay
    // contained (see grfuncs.c's WFILL case comment for the full
    // reasoning and the test result). No-op if handle isn't open, or if
    // x,y is outside the window's current bounds. Uses a plain
    // vector<bool>-backed visited set sized to the image's pixel count,
    // not a scanline-optimized fill -- matches this module's existing
    // simplicity-over-raw-performance choices (pixel()/WPIXEL already
    // does per-pixel QImage::pixel() calls in a loop); revisit only if a
    // large fill's performance actually becomes a real problem.
    void fill(long handle, long x, long y, long tol);

    // WBLIT. Copies a rectangle of src's backing image into dst's at
    // (dx,dy), then repaints dst. sw == -1 (and sh == -1) means "the
    // whole source image" -- an internal sentinel only, used by
    // grfuncs.c's 1- and 3-element forms; the APL-level 7-element form
    // rejects negative sizes before ever reaching here, so it can't
    // collide with a real value.
    //
    // Plain opaque copy (QPainter::CompositionMode_Source): destination
    // pixels are replaced outright, nothing blended -- every backing
    // image is Format_RGB32 with no alpha, so there's nothing to blend
    // anyway, and naming the mode keeps that true even if an image ever
    // did carry alpha. No transparency/color key here by design; a keyed
    // variant would be its own function (see
    // apl_graphics_other_ideas_notes_v1.md's WBLIT section).
    //
    // Clipping: the source rectangle is intersected with src's bounds
    // first, shifting the destination point by however much was clipped
    // off the left/top so the pixels that remain still land where they
    // would have; QPainter then clips against dst's bounds. Any part off
    // either image is silently dropped, including negative dx,dy.
    //
    // src == dst is allowed: the source rectangle is copied out
    // (QImage::copy()) before painting, so overlapping regions come out
    // right (e.g. scrolling a graph within one window).
    //
    // Hidden/shown doesn't matter for either side. Only dst is
    // repainted. Neither window's pen, font or tag is touched. No-op if
    // either handle isn't open, or if the rectangle is empty after
    // clipping.
    void blit(long dst, long src, long dx, long dy,
              long sx, long sy, long sw, long sh);

    // WIRELOAD's reload branch. image is already decoded (see
    // gw_reload_image()'s header comment) -- this only has to check the
    // target window and, if it fits, swap it in. Same-dimensions-only,
    // no resize path: returns -2 if handle isn't currently open, -3 if
    // image's size doesn't match the window's current backing image
    // size, or handle itself on success. Compared against image's raw
    // (pre-conversion) size -- convertToFormat() never changes
    // width/height, but this keeps the check reading exactly what the
    // caller's file actually contained. On a match, the backing QImage
    // is swapped for image.convertToFormat(Format_RGB32) -- same
    // invariant GraphWindow's own image-based constructor already
    // enforces, so a reload's source bit depth/color mode never matters,
    // only its pixel dimensions (see graphwindow.h's constructor
    // comment) -- and the window is repainted via flush(), same as
    // every other backing-image mutation in this class.
    //
    // Unlike every other slot in this class, failure here is reported
    // via a real return value rather than a silent no-op -- WIRELOAD is
    // a value-returning call the APL side is expected to check, not a
    // fire-and-forget setter like setTitle()/setTag()/fill(); see
    // grfuncs.c's case 18 comment for the full reasoning.
    long reloadImage(long handle, const QImage &image);

    // WISAVE. Returns the target window's current backing image BY
    // VALUE if handle is currently open (QImage is copy-on-write, this
    // isn't a real copy), or a null QImage if it isn't -- same
    // null/empty-signals-failure convention size()/pixel() already use.
    // Deliberately does NOT write anything to disk itself -- that
    // happens back on the engine thread, after this call returns, so a
    // slow write never stalls the GUI thread; see gw_save_image()'s own
    // header comment for the full reasoning. BlockingQueuedConnection
    // (like size()), not fire-and-forget: POLY/WPOINTS/WCLEAR/FPOLY all
    // mutate the backing image via queued (non-blocking) calls, so
    // grabbing the image synchronously here guarantees any draws already
    // queued ahead of this one have actually run first, rather than
    // risking a half-finished frame.
    QImage imageForSave(long handle) const;

    // WVIS. One state code per handle, in the order given: 0 = hidden,
    // 1 = shown, 2 = minimized, -1 = not currently open (house "not
    // open" sentinel; never fails the whole call, same per-entry
    // approach pixel()'s -1 for an out-of-bounds point takes). Hidden
    // takes priority over minimized: a window hidden while minimized
    // keeps Qt's minimized flag (confirmed in the sandbox) but reports 0,
    // since it isn't on screen in any form. An empty handles vector gives
    // an empty result -- an ordinary answer here (⎕WVIS ⎕WNUMS with no
    // windows open), not a failure. Fully synchronous, same category as
    // pixel()/size().
    QVector<long> visState(const QVector<long> &handles) const;

    // FDIALOG. Modal, native "open existing file" dialog -- confirmed
    // via a standalone threading spike (see GRAPHICS_NOTES.md) that a
    // modal QFileDialog::exec() invoked this way (cross-thread
    // BlockingQueuedConnection into a slot that runs exec() itself)
    // behaves correctly: the GUI thread's own event loop keeps running
    // for the whole wait, only the engine thread is paused -- the same
    // property the pre-Phase-7a click() relied on, here achieved via
    // QFileDialog's own internal nested event loop. (WCLICK's wait has
    // been in the event hub, evhub.c, on the engine thread since 7a.)
    //
    // Parented to m_focusTarget (the terminal widget) when set -- same
    // pointer open()/openImage() already use for their own post-open
    // focus-reclaim, reused here rather than tracking a second pointer
    // for the same purpose. Makes the dialog properly modal to the
    // application instead of parentless.
    //
    // Returns the selected file's full path, or an empty string on
    // Cancel/Escape -- deliberately not distinguished from each other,
    // same "nothing to report" convention as tagOf()'s own empty-string
    // case and WCLICK's empty-vector case. No separate success flag: a
    // real accepted path can never be empty, so the empty string alone
    // is the complete signal.
    //
    // dir: the folder to open in, already resolved and checked to exist
    // by grfuncs.c (split off the front of FDIALOG's filter, Oct 21,
    // 2026), or empty to leave the starting folder to Qt, which is what
    // every call did before dir existed.
    QString fileDialog(const QString &title, const QString &dir,
                       const QString &filter);

private slots:
    // Connected to every GraphWindow's clicked() signal at open() time.
    // Reports every left click to the event hub (evhub_click()), waited
    // for or not: the hub drops it unless a WCLICK is waiting on that
    // window (Phase 7a). Clicks never queue.
    void onWindowClicked(int handle, int x, int y);
    // Connected to each GraphWindow's closing() signal at open() time --
    // this is the OS-level close (title-bar button) path. WA_DeleteOnClose
    // on GraphWindow means the widget itself is already being torn down
    // by Qt by the time this runs; all we do here is drop the registry
    // entry, and report the close to the event hub, which ends a WCLICK
    // waiting on that window.
    void onWindowClosing(int handle);
    // Connected to each GraphWindow's escapePressed() at open() time
    // (Phase 7b): Escape with a graphics window active ends a wait in
    // progress (evhub_escape()), as Escape in the terminal does.
    void onWindowEscape();

private:
    // Shared by open()/openImage(): the lowest currently-unused handle,
    // starting at 1. Must run before the GraphWindow itself is
    // constructed (its constructor takes the handle as a parameter), so
    // this can't live inside registerWindow() below -- it has to happen
    // first, in each caller.
    int nextFreeHandle() const;

    // First half of what used to be registerAndShow(), split in two when
    // hidden windows were added: registry bookkeeping, signal
    // connections, placement, and the no-focus-stealing widget hints --
    // everything a window needs to exist and be drawable, shown or not.
    // win must already be fully constructed (image/geometry/handle all
    // set). Returns the handle. Does NOT show the window; open()/
    // openImage() follow this with reveal() unless opening hidden.
    long registerWindow(GraphWindow *win, long x, long y);

    // Second half of the old registerAndShow(): the show + deferred
    // focus-reclaim sequence. Shared by open()/openImage() (visible
    // case) and ctrl() opcode 6, so showing a window later behaves
    // exactly like opening it visibly -- and a future fix to this
    // sequence (like the isMinimized() guard already here, added after
    // WOPEN was found to silently un-minimize an intentionally-minimized
    // terminal window) only has to happen in one place. Also marks the
    // window placed: once shown, the window manager has given it a real
    // position even if the caller never did.
    void reveal(GraphWindow *win);

    // Shared unpack for WCLEAR's/WPOINTS' packed-color convention --
    // plain 0xRRGGBB right-shift/mask. Does not go through QColor(QRgb),
    // so the alpha-byte trap noted for PEN (QRgb is 0xAARRGGBB) doesn't
    // apply here.
    static QColor unpackColor(long packed);

    // Shared by text() and textWidth(): the one place the font WTEXT
    // draws with is built, so TWID's measurement and WTEXT's rendering
    // can't drift apart. The window's WFONT family list if it has one
    // (QFont::setFamilies()), otherwise the application default family
    // (what text()'s old painter.font() resolved to); sized in pixels
    // from the window's current WFONT setting.
    static QFont textFont(const GraphWindow *win);

    // Shared by close()/onWindowClosing() -- both teardown paths need to
    // drop the handle from m_creationOrder the same way they already both
    // drop it from m_windows (see that struct's comment). No-op if handle
    // isn't present.
    void removeFromCreationOrder(int handle);

    QMap<int, GraphWindow*> m_windows;

    // Handles in the order their windows were opened, oldest first --
    // independent of m_windows' key-sorted iteration order, which is an
    // incidental byproduct of QMap and was never a creation-order
    // guarantee. Appended to in open() right after a handle is chosen;
    // erased by value in both close() and onWindowClosing(). nums()
    // returns this directly. See apl_graphics_other_ideas_notes_v1.md's
    // WNUMS section for the fuller reasoning (why handle number and
    // recency can disagree, why this needs its own structure rather than
    // deriving order from m_windows).
    QVector<int> m_creationOrder;

    QPointer<QWidget> m_focusTarget;

    // FDIALOG's native-vs-Qt-drawn toggle -- see setUseNativeFileDialog()'s
    // own comment. Plain bool, no marshaling needed: only ever written
    // from the GUI thread (the Settings dialog) and only ever read from
    // the GUI thread (fileDialog(), itself always invoked via
    // BlockingQueuedConnection from the engine thread) -- same
    // single-thread-owns-it reasoning as m_focusTarget just above, not a
    // new pattern.
    bool m_useNativeFileDialog = false;
};

// Set by main.cpp at startup (alongside g_terminal), before the engine
// thread starts. Null until then -- every gw_* bridge function checks for
// null the same way qt_puts()/qt_pchar() etc. check g_terminal.
extern GraphManager *g_graphManager;

#endif // GRAPHMANAGER_H
