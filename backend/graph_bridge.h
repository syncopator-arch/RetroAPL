#ifndef GRAPH_BRIDGE_H
#define GRAPH_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*
 * Bridge between the (C) APL engine thread and the (C++/Qt) graphics
 * front end. See graph_bridge.cpp for the actual marshaling.
 *
 * All string arguments are null-terminated char*, already extracted from
 * APL string values by grfuncs.c (same convention it already uses
 * elsewhere via LSCRATCH).
 *
 * Two calling conventions are in play here, not one:
 *
 *   - gw_open/gw_close/gw_nums/gw_size/gw_title (Phase 1) block the
 *     calling (engine) thread via Qt::BlockingQueuedConnection until the
 *     GUI thread has actually done the work -- safe to use the return
 *     value/output params immediately afterward, same as any other
 *     synchronous call.
 *
 *   - gw_pen/gw_poly/gw_points/gw_clear (Phase 2) are fire-and-forget via
 *     Qt::QueuedConnection instead -- none of them have a result the
 *     engine thread needs back, and drawing ops don't need to block the
 *     interpreter on the GUI thread catching up. Callers must NOT assume
 *     the work is done by the time the call returns. Any buffer passed
 *     in (ops/data below) is only guaranteed valid for the duration of
 *     the call itself -- graph_bridge.cpp copies out of it before
 *     returning, so callers are free to reuse/overwrite that buffer
 *     (e.g. LSCRATCH) right after.
 *
 *     Fire-and-forget is bounded, though: at most a fixed number of these
 *     calls (currently 64, shared across all of them) can be waiting on
 *     the GUI thread at once. Past that, the call waits until the GUI
 *     catches up -- so a tight drawing loop can't bury Ctrl+C/Escape/
 *     close-box handling under an unbounded backlog. If an interrupt
 *     (ABORTSIG) is flagged while waiting, the call is dropped instead.
 *     See postDrawing() in graph_bridge.cpp. This applies to gw_pen,
 *     gw_poly, gw_points, gw_clear, gw_fpoly, gw_text, gw_fill and
 *     gw_blit.
 *
 * gw_nums()/gw_size() write into caller-provided int32_t buffers (meant
 * to be LSCRATCH directly, no cast needed -- LSCRATCH's real declared
 * type is int32_t[], and push_val()'s third parameter is int32_t* too).
 * gw_poly()/gw_points() likewise take int32_t* input arrays for the same
 * reason -- grfuncs.c decodes APL args into LSCRATCH-typed buffers
 * throughout, and int32_t is what actually holds those values correctly;
 * see the WNUMS/WSIZE note in an earlier draft of this header about why
 * long* here would be a real bug on 64-bit builds (8-byte stride written,
 * 4-byte stride read back).
 */

/* WOPEN. Returns the assigned window handle (>=1), or -1 if w/h are
 * unusable (<=0). x,y == -1,-1 is the "system default placement" sentinel
 * -- passed through as-is; graph_manager.cpp is what interprets it.
 * long params here are fine even though get_data() returns int32_t --
 * widening on the way in is always safe, unlike the narrowing case above.
 *
 * visible: 1 = shown (what every WOPEN did before this parameter
 * existed), 0 = opened hidden -- fully registered and drawable, but not
 * shown until WCTRL 6 (see GraphManager::open()). grfuncs.c reads it from
 * WOPEN's optional 5th right-arg element and passes 1 when that's absent,
 * so the APL-level shape stays backward compatible. This C signature
 * itself did change (added parameter) -- acceptable because grfuncs.c is
 * its only caller and was updated in the same change. */
long gw_open(const char *title, long x, long y, long w, long h, long visible);

/* WILOAD, 'filename' WILOAD x,y. Loads an image file directly into a
 * brand-new window -- the loaded image *becomes* the window's entire
 * backing bitmap (no compositing onto anything else, no transparency
 * question at all -- see apl_graphics_other_ideas_notes_v1.md's Image
 * Load section). Window size comes from the decoded image itself, not
 * from the caller, unlike gw_open()'s w,h. title doubles as the window's
 * title bar text and (unlike gw_open()) has no separate role of its
 * own -- there's no room for a third caller-supplied string alongside
 * filename and x,y, so grfuncs.c just passes the filename itself as
 * title; that decision is made at the grfuncs.c layer, not duplicated
 * here.
 *
 * Unlike every other function in this header, decoding the image
 * happens directly on the CALLING (engine) thread, before any GUI-
 * thread hop at all -- QImage decode/load isn't GUI-thread-bound, so
 * there's no reason to cross threads just to read and decode a file.
 * Only the actual window creation crosses threads, via
 * Qt::BlockingQueuedConnection, same as gw_open(). This is why a bad
 * path/corrupt file/unsupported format can be reported back
 * synchronously without the GUI thread ever getting involved.
 *
 * x,y == -1,-1 is the same system-default-placement sentinel gw_open()
 * uses. Returns the new window's handle (>=1) on success, or -1 if the
 * image couldn't be loaded/decoded.
 *
 * visible: same meaning as gw_open()'s (1 = shown, 0 = hidden), read by
 * grfuncs.c from WILOAD's optional 3rd right-arg element, defaulting to 1.
 * Same caller-updated-in-the-same-change note as gw_open(). */
long gw_load_image(const char *filename, long x, long y, long visible);

/* WCLOSE, one handle at a time -- grfuncs.c loops over the APL arg vector
 * itself and calls this per element, matching the old code's shape.
 * No-op if handle isn't currently open. */
void gw_close(long handle);

/* WNUMS. Fills buf with up to maxCount currently-open handles
 * (caller-provided space -- pass LSCRATCH and its real element capacity,
 * not just LSCRATCH) and returns the count actually written. If more
 * windows are open than maxCount, the excess are silently omitted from
 * this call's result rather than overflowing the buffer -- extremely
 * unlikely to matter in practice, but this is a real caller-provided
 * bound now rather than an unchecked write. Reflects OS-level closes as
 * well as WCLOSE. */
long gw_nums(int32_t *buf, long maxCount);

/* WNUMS, filtered by tag pattern. Same buffer/maxCount convention as
 * gw_nums() -- fills buf with up to maxCount matching handles, in
 * creation order, returns the count actually written. pattern ending
 * in '*' is a prefix match (against everything before the '*');
 * otherwise it's an exact match. See GraphManager::nums(QString)'s
 * own comment for the untagged/empty-pattern edge case. */
long gw_nums_matching(const char *pattern, int32_t *buf, long maxCount);

/* WSIZE. handle == -1 is the "no right arg given" sentinel (grfuncs.c's
 * MONAD_OPT case with re == NULL, i.e. WSIZE called niladically) ->
 * desktop dimensions (screen). Same sentinel convention WOPEN already
 * uses for x,y ("system default placement"), reused here rather than
 * adding a separate has-handle flag. Any other handle must be a real,
 * currently-open window number; returns that window's current
 * backing-image width,height. 0 is deliberately NOT special-cased as
 * desktop size -- it's just an ordinary (never valid) window number, same
 * as any other handle nothing has opened -- avoids a confusing collision
 * with some future graphics operator that might work on "window 0."
 *
 * Writes 4 elements: buf[0]=width, buf[1]=height, buf[2]=x, buf[3]=y.
 * x,y is the window's current upper-left position, in the exact same
 * coordinate system WOPEN's/WILOAD's own x,y placement argument uses
 * (GraphManager::size() reads it via win->pos(), the direct inverse of
 * registerWindow()'s win->move()) -- so a later ⎕WOPEN using WSIZE's
 * own x,y output reopens in the same spot. A hidden window that has
 * never been placed reports x,y as -1,-1 (see GraphManager::size()),
 * which a later ⎕WOPEN reads as default placement. For the desktop case, x,y is
 * always 0,0 (the desktop has no position of its own; that's just this
 * coordinate system's fixed origin). Always writes all 4 elements
 * regardless of outcome (0,0,0,0 on failure). Returns 1 on success, 0 if
 * a real handle was given but isn't currently open -- unlike Phase 1's
 * version (which had no failure path at all, since a niladic desktop-size
 * query can't fail), callers must check this return value now whenever
 * handle != -1. */
long gw_size(long handle, int32_t *buf);

/* WTITLE. No-op if handle isn't currently open. */
void gw_title(long handle, const char *title);

/* WTAG, 'tag' ⎕WTAG wnum. Sets handle's tag -- a free-form label,
 * independent of title, used later via gw_nums_matching()/gw_tagof().
 * Plain assignment; an empty tag is a legitimate value (it just makes
 * the window match nothing via gw_nums_matching() until re-tagged),
 * not a distinct "clear" operation at this layer either. No-op if
 * handle isn't currently open. Same Phase 1 blocking family as
 * gw_title() -- side-effect-only doesn't put this in the fire-and-
 * forget Phase 2 category, matching gw_title()'s own precedent. */
void gw_tag(long handle, const char *tag);

/* WTAGOF, ⎕WTAGOF wnum. Writes handle's current tag into buf,
 * null-terminated and truncated to bufsize -- an empty string if
 * handle isn't currently open OR is open but untagged (deliberately
 * not distinguished, same "nothing to report" convention
 * gw_pen_query() etc. already use). No separate success/failure
 * return -- the empty-string result already carries that. */
void gw_tagof(long handle, char *buf, long bufsize);

/* WCTRL, one handle at a time -- grfuncs.c loops over le itself for the
 * bulk opcodes and calls this once per handle, same shape as gw_close().
 * opcode: 0=minimize, 1=restore, 2=front (raise), 3=back (lower),
 * 4=move (x,y used; ignored for every other opcode -- pass 0,0),
 * 5=hide, 6=show. 0/1 are no-ops on a hidden window and 6 is a no-op on
 * one that isn't hidden -- see GraphManager::ctrl()'s header comment.
 * Blocking (Qt::BlockingQueuedConnection), same Phase 1 family as
 * gw_close()/gw_title()/gw_tag() -- this is window-state management, not
 * a tight-drawing-loop call, so there's no performance reason to make it
 * fire-and-forget the way gw_clear()/gw_poly() etc. are. No-op if handle
 * isn't currently open, or if opcode is out of range (grfuncs.c already
 * validates the opcode before ever calling this; the out-of-range case
 * here is defensive only, same as sys_ctrl()'s own default: break). */
void gw_ctrl(long handle, long opcode, long x, long y);

/* PEN, wnum PEN r,g,b,width. Sets the window's current pen -- drives both
 * POLY's line width and WPOINTS' point size. width == -1 is a sentinel
 * meaning "leave the current width unchanged, set color only" -- same
 * house convention as WOPEN's -1,-1 placement default and WSIZE's -1
 * desktop-size case (real widths are always >=1, so -1 is unambiguous).
 * r == -1 is the same sentinel, now also covering color -- "leave the
 * current color unchanged, set width only" (grfuncs.c's PEN re-length-1,
 * non-query-value case; g,b are -1 too by construction, but only r is
 * actually checked here). The two sentinels are independent, not
 * paired -- a call can use either, both, or neither. No-op if handle
 * isn't currently open. Fire-and-forget (see calling-convention note
 * above). */
void gw_pen(long handle, long r, long g, long b, long width);

/* PEN query, wnum PEN <length-1 dummy re>. Reads back the window's
 * current pen as buf[0]=r, buf[1]=g, buf[2]=b, buf[3]=width. Fully
 * synchronous -- same calling convention as gw_size()/gw_pixel()
 * (Qt::BlockingQueuedConnection), NOT gw_pen()'s fire-and-forget set
 * path: pen state only exists on the GUI-thread GraphWindow object, so
 * this has to actually wait for the GUI thread rather than fire-and-
 * forget. Returns 1 on success, 0 if handle isn't currently open at all
 * -- same "handle not open" return convention gw_size()/gw_pixel() use;
 * buf is left untouched (all zero) in that case. Note grfuncs.c's PEN
 * case maps that 0 to an empty-vector *result*, not an error, unlike
 * WSIZE/WPIXEL's own callers -- a deliberate choice made at that layer,
 * not something this function's return convention dictates either way. */
long gw_pen_query(long handle, int32_t *buf);

/* POLY, wnum POLY <p,x,y triples>. ops points to a flattened p,x,y
 * stream, count int32_t values long (a multiple of 3 -- grfuncs.c's job
 * to ensure; this function does not itself re-validate that beyond what
 * a simple /3 truncation handles). p == -1 is move (pen up); anything
 * else draws with the window's current pen from its last position to
 * (x,y). No-op if handle isn't currently open. Fire-and-forget; ops is
 * only read for the duration of this call, safe to reuse the buffer
 * (e.g. LSCRATCH) immediately after this returns. */
void gw_poly(long handle, const int32_t *ops, long count);

/* WPOINTS, wnum WPOINTS <point data>. data points to a flattened row
 * stream, count int32_t values long, ncols wide per row (2 or 3 --
 * grfuncs.c determines this from the APL arg's shape and passes it
 * through; any other ncols is a no-op here). ncols==2 rows are (x,y) in
 * the current pen color; ncols==3 rows are (x,y,packedColor) with the
 * 3rd column overriding color for that point only -- current pen *size*
 * still applies either way. No-op if handle isn't currently open.
 * Fire-and-forget; data is only read for the duration of this call. */
void gw_points(long handle, const int32_t *data, long count, long ncols);

/* WCLEAR, wnum WCLEAR color. Fills the window's backing image with the
 * given 24-bit packed RGB color (0xRRGGBB) and repaints. No-op if handle
 * isn't currently open. Fire-and-forget. */
void gw_clear(long handle, long packedColor);

/* FPOLY, wnum FPOLY <x,y pairs>. ops points to a flattened x,y stream,
 * count int32_t values long (a multiple of 2 -- grfuncs.c's job to
 * ensure; this function does not itself re-validate that beyond what a
 * simple /2 truncation handles). Fills the polygon formed by those
 * vertices (implicitly closed, last back to first) using the window's
 * current pen color, under the odd-even fill rule -- correct for
 * self-intersecting shapes (e.g. a figure-8) with no special-casing
 * needed. No separate outline stroke. No-op if handle isn't currently
 * open. Fire-and-forget; ops is only read for the duration of this call,
 * safe to reuse the buffer (e.g. LSCRATCH) immediately after this
 * returns. */
void gw_fpoly(long handle, const int32_t *ops, long count);

/* WCLICK, wnums WCLICK. handles points to `count` window numbers to wait
 * on (count >= 1 -- grfuncs.c's job to ensure). Blocks the ENGINE thread
 * via Qt::BlockingQueuedConnection, same as Phase 1's gw_open() etc. --
 * but for an indefinite, user-paced amount of time rather than an
 * instant GUI round trip. The GUI thread's own event loop keeps running
 * the whole time (windows keep repainting/resizing normally); only APL
 * execution is paused, exactly as any running statement already pauses
 * it. See apl_graphics_mouse_input_notes_v1.md for the full design
 * (including why this is safe/expected for a single-threaded
 * interpreter, and how cancellation -- Ctrl+C, Escape, or an armed
 * window closing -- is handled on the GUI side).
 *
 * On a real left-click in one of the given windows: writes
 * buf[0]=handle, buf[1]=x, buf[2]=y and returns 1.
 * On cancel (Ctrl+C, Escape, ABORTSIG already set) or an armed window
 * closing before any click arrives: leaves buf untouched and returns 0.
 * These are deliberately not distinguished from each other -- callers
 * only ever need "did a click happen." Only open, non-hidden windows in
 * the list are armed; handles that aren't open and hidden windows are
 * skipped. If nothing in the list can be clicked, returns 0 immediately
 * instead of waiting -- see GraphManager::click() for why that's a 0
 * return and not an error. */
long gw_click(const int32_t *handles, long count, int32_t *buf);

/* WVIS, ⎕WVIS wnums. handles points to `count` window numbers (count may
 * be 0). Writes one state code per handle into buf, in order: 0 = hidden,
 * 1 = shown, 2 = minimized, -1 = not currently open -- buf must have room
 * for count int32_t values, and must NOT overlap handles (grfuncs.c uses
 * two separate regions of g_gscratch; unlike gw_pixel(), no in-place
 * aliasing is relied on). Never fails as a whole: a handle that isn't
 * open just gets -1 in its own slot. Returns the number of entries
 * written (always count). Blocking (Qt::BlockingQueuedConnection), same
 * family as gw_pixel()/gw_size(), so it's ordered correctly after any
 * earlier WCTRL 5/6 or WOPEN. If the graphics manager doesn't exist at
 * all, every entry is -1 (nothing is open). */
long gw_vis(const int32_t *handles, long count, int32_t *buf);

/* WPIXEL, wnum WPIXEL <x,y point list>. points is a flattened x,y
 * stream, count int32_t values long (a multiple of 2 -- grfuncs.c's job
 * to ensure), N x2 shape matching WPOINTS' own convention. Fully
 * synchronous -- no waiting on user input, same calling convention as
 * gw_size() (Qt::BlockingQueuedConnection), NOT gw_click(). Writes one
 * packed 24-bit color (0xRRGGBB, same packing WCLEAR/WPOINTS' override
 * column already use) per input point into buf, in order -- buf must
 * have room for count/2 int32_t values. A point outside the window's
 * current bounds writes -1 for that one entry rather than failing the
 * whole call (packed colors are always non-negative, so -1 is an
 * unambiguous per-point sentinel). Returns 1 on success, 0 if handle
 * isn't currently open at all -- that's the one thing that still fails
 * the whole call, same as gw_size()'s existing handle-not-open
 * convention; buf is left untouched in that case. */
long gw_pixel(long handle, const int32_t *points, long count, int32_t *buf);

/* WTEXT, 'string' WTEXT wnum,x,y. Draws text in the window's backing
 * image using the window's current pen color, in the window's current
 * WFONT face and size (pixels; see gw_font()/gw_font_face() below -- a
 * window that never calls WFONT gets the app default face at a 14px
 * default size, matching the old 14pt at 72 dpi). No color override. (x,y) is Qt's native drawText()
 * baseline, not a top-left anchor, taken as-is. No-op if handle isn't
 * currently open. Fire-and-forget (Phase 2 calling convention, see
 * above); text is only read for the duration of this call, safe to
 * reuse the buffer (e.g. LSCRATCH) immediately after this returns. */
void gw_text(const char *text, long handle, long x, long y);

/* WFONT query, '' WFONT wnum. Returns the window's current font size in
 * pixels, or -1 if handle isn't currently open (real sizes are always
 * >=1, so -1 is unambiguous). Qt::BlockingQueuedConnection, same family
 * as gw_pen_query() -- font state only exists on the GUI-thread
 * GraphWindow object. Returns the value directly rather than via a
 * caller buffer + 1/0 flag the way gw_pen_query()/gw_size() do: this is
 * only ever one number, same shape as gw_open()'s own scalar-or--1
 * result. */
long gw_font_query(long handle);

/* WFONT set, '' WFONT wnum,size. size is in pixels; size == -1 is the
 * "reset to default" sentinel -- passed through as-is, GraphManager::
 * setFontSize() is what interprets it (same division of labor as
 * gw_pen()'s -1 sentinels). Returns the window's PRIOR size, or -1 if
 * handle isn't currently open (nothing changed in that case). Blocking,
 * NOT gw_pen()'s fire-and-forget set path, because the prior value has
 * to come back. Ordering against earlier fire-and-forget WTEXT calls is
 * still correct: Qt delivers queued and blocking-queued invocations to
 * the GUI thread in posting order, so a WTEXT issued before a WFONT
 * change draws at the old size. */
long gw_font(long handle, long size);

/* WFONT face set, 'face' WFONT wnum[,size]. spec is the trimmed, non-
 * empty face string grfuncs.c extracted: a comma-separated fallback list
 * of family names, or the whole spec "DEFAULT" (any case) to reset to the
 * app's default face -- passed through as-is, GraphManager::
 * setFontFace() is what splits/trims it and recognizes DEFAULT (same
 * division of labor as gw_font()'s -1 size sentinel). Leaves the size
 * alone. Returns the window's current (= prior, since only the face
 * changed) size, or -1 if handle isn't currently open (nothing changed
 * in that case). Blocking, same family as gw_font(). */
long gw_font_face(long handle, const char *spec);

/* WFONT face query, '?' WFONT wnum. Writes the family name Qt actually
 * resolved for the window's current face (QFontInfo -- not necessarily
 * what was asked for, if a name isn't installed or a fallback list was
 * given) into buf, null-terminated and truncated to bufsize -- same
 * convention as gw_tagof(). Empty string if handle isn't currently open.
 * Blocking, same family as gw_tagof(). */
void gw_font_face_query(long handle, char *buf, long bufsize);

/* TWID, 'string' TWID wnum. Width in pixels the string would occupy if
 * drawn by WTEXT in that window right now -- same QString conversion as
 * gw_text() (fromLatin1, so APL glyphs are measured exactly as the
 * garbled Latin-1 characters WTEXT currently draws, not as the glyphs
 * the terminal shows) and the same font GraphManager::text() uses.
 * Horizontal advance, rounded to a whole pixel. Returns -1 if handle
 * isn't currently open. Blocking, same family as gw_font_query(). */
long gw_text_width(const char *text, long handle);

/* WFILL, wnum WFILL x,y or wnum WFILL tol,x,y. Flood fill ("paint
 * bucket") from a single (x,y) seed point, replacing every 4-connected
 * pixel matching the seed pixel's starting color with the window's
 * current PEN color. tol is a per-channel tolerance (0-255 scale, same
 * as every other color value in this module) -- a candidate pixel
 * qualifies if every one of its r,g,b channels is within tol of the
 * SEED pixel's own original color, always compared against that fixed
 * starting color, never neighbor-to-neighbor (standard bucket-fill
 * definition; avoids drifting across a gradient one small step at a
 * time). tol < 0 is clamped to 0 (exact match) inside
 * GraphManager::fill(), same permissive-clamp spirit as PEN's width
 * clamp in GraphWindow::setPen() -- not grfuncs.c's job. 4-connected,
 * not 8 -- confirmed by direct testing against a real drawn shape with
 * a shallow diagonal edge to actually stay contained (see grfuncs.c's
 * case 17 comment for the full reasoning); 8-connected leaks through
 * the diagonal-only gaps a non-antialiased 1px line's own staircase
 * steps leave. No-op if handle isn't currently open, or if x,y is
 * outside the window's current bounds. Fire-and-forget -- a drawing
 * op, same category as gw_poly()/gw_fpoly()/gw_points()/gw_clear(). */
void gw_fill(long handle, long x, long y, long tol);

/* WBLIT, dst WBLIT src[,dx,dy[,sx,sy,sw,sh]]. Copies a rectangle of
 * src's backing image into dst's at (dx,dy) -- plain opaque copy, no
 * blending, no transparency. sw == sh == -1 means "the whole source
 * image" (grfuncs.c passes that, with sx,sy = 0,0, for the 1- and
 * 3-element forms; the 7-element form's real sizes are never negative).
 * Clipped silently against both images; src == dst is safe (overlap is
 * handled). No-op if either handle isn't currently open. Fire-and-forget
 * (Qt::QueuedConnection), same Phase 2 family as gw_poly()/gw_clear():
 * no result the engine thread needs back, and queued calls still run in
 * posting order, so a frame drawn into a hidden buffer is always complete
 * before a WBLIT posted after it copies it. See GraphManager::blit(). */
void gw_blit(long dst, long src, long dx, long dy,
             long sx, long sy, long sw, long sh);

/* WIRELOAD (reload branch), 'filename' WIRELOAD wnum -- refresh an
 * already-open window's backing image in place, same dimensions only (no
 * resize path; see apl_graphics_other_ideas_notes_v1.md's WIRELOAD
 * section for why that's explicitly out of scope). Unlike every other
 * gw_* call in this header, failure here is NOT reported by aborting the
 * calling APL statement -- grfuncs.c returns one of three negative codes
 * to the caller instead of SET_ERR'ing, a deliberate exception decided in
 * conversation specifically because a window can legitimately disappear
 * (title-bar close) at a moment outside the program's control, and
 * treating that as fatal was judged worse than letting the program test
 * for it. This function's job is only to report WHICH of the three
 * happened; grfuncs.c decides what value that becomes on the APL side.
 *
 * Decode-then-hop, same structure as gw_load_image(): filename is
 * decoded directly on the calling (engine) thread first (QImage decode
 * isn't GUI-thread-bound), so a decode failure never touches the GUI
 * thread. Only the size check + swap-in crosses threads, via
 * Qt::BlockingQueuedConnection -- the window's CURRENT size only exists
 * on the GUI thread, so this is one hop (compare, swap, repaint), not a
 * separate query-then-swap round trip.
 *
 * Returns handle on success. On failure: -1 = decode/format failure
 * (same detection gw_load_image() itself uses); -2 = handle is not
 * currently an open window; -3 = the file decoded fine, but its
 * dimensions don't match the window's current backing image size. The
 * loaded image is converted to Format_RGB32 before being swapped in,
 * same as GraphWindow's own image-based constructor -- so a reload's
 * source bit depth/color mode never matters, only its pixel
 * dimensions. */
long gw_reload_image(long handle, const char *filename);

/* WIRELOAD (query branch), 'filename' WIRELOAD ¯1 -- reports a candidate
 * file's pixel dimensions without touching any window at all, so a
 * caller can check compatibility before attempting a real reload (or
 * build a filtered list of fit-checked files). Never crosses to the GUI
 * thread -- there's no window target to check against, so this is
 * cheaper than gw_reload_image(), not just a variant of it. Returns 1
 * and fills buf[0]=width, buf[1]=height on success; 0 (buf left
 * untouched) if the file doesn't decode -- same detection
 * gw_load_image()/gw_reload_image() already use for their own decode
 * step, just exposed directly here since there's no window-creation or
 * window-swap step to follow it. */
long gw_query_image(const char *filename, int32_t *buf);

/* WISAVE, 'filename' WISAVE wnum or 'filename' WISAVE wnum,¯1 -- dump
 * wnum's current backing image to a file. The second (optional) re
 * element picks the overwrite policy: omitted means FAIL if the target
 * already exists (the conservative default); ¯1 means overwrite
 * silently. Format is inferred from filename's extension by
 * QImage::save() itself -- no separate format argument, same "no room
 * for a third argument" constraint gw_load_image()/gw_reload_image() are
 * already under.
 *
 * Two genuinely different failure categories here, reported two
 * different ways -- see grfuncs.c's case 19 comment for the full
 * reasoning:
 *   -2 = handle is not currently an open window
 *   -3 = the file already exists and overwrite wasn't requested
 *   -1 = the disk write itself actually failed (permissions, disk full,
 *        bad path) -- a genuine environmental failure, which grfuncs.c
 *        turns into SET_ERR(ERR_SAVE) rather than returning it to the
 *        caller as a value, unlike -2/-3
 * Returns handle on success.
 *
 * The file-exists check (when overwrite isn't requested) happens
 * directly on the calling (engine) thread -- QFileInfo::exists() has
 * nothing to do with any window's state, same reasoning
 * gw_query_image()'s decode-only check already uses, so there's no
 * reason to involve the GUI thread just for that.
 *
 * The window's backing image is fetched via ONE
 * Qt::BlockingQueuedConnection call (GraphManager::imageForSave()) that
 * does nothing but grab it by value (QImage is copy-on-write, this
 * isn't a real copy) and hand it back -- deliberately NOT the call that
 * writes to disk. The actual file write happens AFTER that call
 * returns, back on this (engine) thread, not inside the GUI-thread
 * lambda, so a slow disk write never stalls the GUI thread. Grabbing the
 * image synchronously still guarantees any draws already queued ahead of
 * this call (POLY/WPOINTS/WCLEAR/FPOLY are all fire-and-forget
 * Qt::QueuedConnection) have actually completed first, avoiding a
 * half-finished frame -- same ordering guarantee gw_size() already
 * relies on. */
long gw_save_image(long handle, const char *filename, long overwrite);

/* FDIALOG, 'title' FDIALOG 'filter'. Opens a native, modal "open
 * existing file" dialog. NOT window-scoped -- no wnum anywhere in this
 * call, deliberately: a file dialog isn't tied to any particular
 * GraphWindow, same "two functions, two shapes" reasoning that already
 * kept sysfuncs.c's CTRL and this module's own WCTRL as separate
 * functions rather than forcing a shared shape.
 *
 * title may be empty (Qt supplies its own default). filter is passed
 * through verbatim in Qt's own native filter-string syntax (e.g.
 * "Images (*.png *.jpg);;All Files (*)") -- not translated or
 * validated at this layer; an empty filter means "no restriction."
 * Single-file, open-existing-file mode only -- no save mode, no
 * directory picker, no multi-select in this version; a save mode, if
 * it ever happens, is expected to be its own function rather than an
 * opcode grafted onto this one, same reasoning as above.
 *
 * Needs Qt::BlockingQueuedConnection, same family as gw_size()/
 * gw_tagof() -- but unlike every other function in this header, the
 * block here can last an arbitrary, user-controlled amount of time
 * (however long the dialog stays open), not a near-instant GUI-thread
 * hop. Confirmed via a standalone threading spike (see
 * GRAPHICS_NOTES.md) that a modal QFileDialog::exec() called from
 * inside a BlockingQueuedConnection-invoked slot behaves correctly: the
 * GUI thread's own event loop keeps running for the whole wait (other
 * windows stay responsive/repainting), only the engine thread is
 * paused -- same property gw_click() already relies on, just arrived
 * at via QFileDialog's own internal nested event loop rather than the
 * purpose-built QEventLoop gw_click() needs (a plain modal dialog has
 * no armed-handle-list concept to poll). The spike used
 * QFileDialog::DontUseNativeDialog to make Cancel/Accept scriptable
 * under an offscreen test display -- the native dialog (what this
 * function actually uses) is assumed to share the same underlying
 * exec()-based modality but hasn't been separately confirmed against a
 * real display; worth a quick manual check once there's a real screen
 * to test against.
 *
 * Writes the selected file's full path into buf, null-terminated and
 * truncated to bufsize -- same convention gw_tagof() already uses. On
 * Cancel/Escape, buf is set to an empty string -- not a failure code,
 * same "nothing to report" convention as gw_click()'s empty-vector
 * result and gw_tagof()'s empty-string case; the two aren't
 * distinguished. No separate return value here (unlike gw_size()'s 1/0
 * success flag) -- an empty buf IS the complete signal, since a real
 * accepted path can never be empty. */
void gw_file_dialog(const char *title, const char *filter, char *buf, long bufsize);

#ifdef __cplusplus
}
#endif

#endif /* GRAPH_BRIDGE_H */
