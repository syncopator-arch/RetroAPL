<!-- font: Georgia, DejaVu Serif, serif -->
# Quad Function Reference

Calling shape and behavior for each `⎕` function, for quick lookup while
you're working. A window handle (`wnum`) is a number returned by `⎕WOPEN`.
Packed colors are 24-bit `0xRRGGBB` values unless a function says
otherwise.

---

## System Functions

`⎕TS`

Returns the current timestamp as 7 numbers: year, month, day, hour,
minute, second, and a 7th field believed to be hundredths of a second.

`⎕DL n`

Delays execution by `n`, flushing pending output first. The exact time
unit isn't confirmed — don't assume seconds vs. centiseconds if timing
precision matters.

`⎕INTDIV`

`⎕INTDIV n`

Gets or sets whether dyadic `÷` does integer division (same setting
`)INTDIV` controls). `1` means integer division: when both arguments
are integers the result is truncated toward zero, so `7÷2` is `3` and
`¯7÷2` is `¯3`. `0` means `÷` always returns a float, even for exact
quotients (`6÷3` gives `2.0`), which is the usual APL behavior. Setting
returns the *prior* value; `Domain Error` if set to anything other than
`0` or `1`. Saved with the workspace. New and cleared workspaces start
at `0`; workspaces saved before this setting existed load as `1`.
Monadic `÷` always returns a float regardless.

`⎕UCASE text`

Uppercases a character string. `Domain Error` if the argument isn't
character data. See `⎕LCASE` for the mirror.

`⎕NL`

Returns the newline character as a 1-element character value.

`⎕RL`

`⎕RL n`

Gets or sets the random link, the state of the generator behind `?`.
Setting returns the *prior* value. Setting the same `n` repeats the same
sequence of `?` results, on any platform; `S←⎕RL` saves the current
point, and `⎕RL S` later carries on from it. `n` is `1`–`2147483646`,
or `0` to go back to unpredictable results; anything else is
`Domain Error`. Not saved with the workspace. Same compiler caveat as
`⎕WNUMS` applies to `n`.

`⎕WA`

Returns available workspace ("watermark") in bytes.

`⎕WI`

`⎕WI n`

Gets or sets the output line width. Setting returns the *prior* value.
A width below 20 is silently clamped up rather than rejected. Same
compiler caveat as `⎕WNUMS` applies to `n` — pass it as a literal or a
named variable, not an unresolved expression.

`⎕VT value`

Returns the argument's internal type code. Useful for debugging, not
something to build application logic around.

`⎕CQ value`

"Convert quote" — marks the argument as a constant rather than a
variable. The exact intended use case isn't confirmed.

`⎕ARGS`

Returns the interpreter's command-line arguments as a character
matrix, one row per argument, space-padded to the widest.

`⎕IO`

`⎕IO n`

Gets or sets the index origin (same setting `)ORIGIN` controls).
Setting returns the *prior* value; `Domain Error` if set to anything
other than `0` or `1`. Same compiler caveat as `⎕WNUMS` applies to `n`.

`⎕ENV name`

Reads an environment variable given as a character string, returning
its value as a character string.

`⎕SYS n`

Dispatches an internal sub-function by number: `0` reports whether the
build has float support; `1` is meant to end the session like `)OFF`,
but currently does nothing in the windowed app — execution just
continues — so don't rely on it as an exit mechanism yet; `2` returns
the current restart mode; `4`/`5` turn `APL_CASE` translation off/on.

`⎕SI`

Returns the pending function-call line numbers from the current call
stack (state-indicator equivalent, like `)SI`), capped at 100 entries.

`⎕STRUCT dims`

Creates an empty nested ("struct") array with the shape given by
`dims`. Each cell starts out empty. Assign any value to a cell by
indexing (`S[2]←'TEXT'`, `S[3]←⍳5`) and read it back the same way
(`S[2]`). The cell then holds its own value, the way a variable name
does, and that value can be indexed and assigned into further
(`S[2][3]`, `S[2][3]←0`). Reading a cell that was never assigned is a
`Value Error`. `Rank Error` if `dims` has more elements than the
interpreter's maximum rank.

`⎕EXEC command`

Runs an external command asynchronously (doesn't wait for it to
finish), returning the command's result code. See `⎕WEXEC` to wait for
completion.

`⎕WEXEC command`

Same as `⎕EXEC`, but waits for the command to finish before returning.

`⎕CD path`

Changes (and/or reports) the current directory, returning the
resulting current directory as a character string. Behavior for an
empty-string argument isn't documented — don't assume it means "just
report current dir" without checking.

`⎕LC`

Returns the current function's line count.

`⎕WSID`

Returns the current workspace name.

`subfunc,param ⎕CTRL`

Window/session control, dispatched by the first element. The only
sub-function defined so far is `1`, main terminal window sizing:
`1,mode`, where `mode` of `0`/`1`/`2` minimizes/restores/maximizes the
window. `Length Error` if fewer than 2 elements are given; `Domain Error`
if the first element isn't `1`, the argument is float-typed, or `mode`
isn't `0`, `1`, or `2`.

`⎕AV`

Returns a 256-element character vector. **Non-standard content, worth
knowing:** unlike a traditional APL atomic vector, this one is simply
bytes `0` through `255` in literal numeric order — don't assume
APL-glyph positions here match another APL's `⎕AV`.

`⎕LCASE text`

Lowercases a character string; mirror of `⎕UCASE`.

`⎕DIG`

`⎕DIG n`

Gets or sets `)DIGITS` print precision. Setting returns the *prior*
value; `n` is clamped to `¯17…17`. Positive means fixed decimal places
(trailing zeros kept); negative means up to that many decimal places
(trailing zeros trimmed); `0` is valid either way.

---

## File Functions

File handle numbers (`fnum`) are your own choice — any integer you
pick — not one the interpreter hands back the way `⎕WOPEN` returns a
window number. There's no built-in way to ask for "a free one"; that
bookkeeping is on you if you need it. Up to 10 files can be open at
once — opening an 11th returns `File Table Full`.

`fnum ⎕FOPEN 'filename'`

Opens a file for read/write (binary mode) under your chosen handle
number. Returns `1` if newly opened successfully; returns `0` in two
different cases that aren't distinguished — the handle number was
already in use, or the open itself failed (bad path, permissions,
etc.). Check `⎕FNUMS` first if you need to tell these apart.
`File Table Full` if 10 files are already open.

`⎕FCLOSE fnum`

Closes an open file by handle number. Returns `1` on success.
`File Tie Error` if that handle isn't currently open.

`fnum ⎕FREAD count`

`fnum ⎕FREAD`

Reads from an open file. With no right argument ("line mode"), reads
one line (up to the interpreter's line-size limit) with trailing
`\n`/`\r` stripped. With `count`, reads exactly that many bytes raw —
no line-ending handling. Returns whatever was actually read (possibly
fewer bytes than requested, at EOF). `File Tie Error` if the handle isn't
open; `WS Full` if a large read can't allocate. **Passing multiple file
numbers doesn't give you a per-file result** — only the first is read.

`⎕FNUMS`

Returns currently open file handle numbers. Order is not guaranteed
(unlike `⎕WNUMS`, which is explicitly in creation order).

`fnum ⎕FSEEK pos`

Repositions a file. `pos ≥ 0` is an absolute position from the start of
the file; `pos < 0` counts back from end-of-file — `pos = ¯1` seeks to
the literal end of file, `pos = ¯2` seeks to one byte before EOF, and
so on. Use `¯1` for "seek to EOF," not `0`. Returns the resulting
absolute file position. `File Tie Error` if the handle isn't open.

`⎕FTELL fnum`

`⎕FTELL fnums`

Returns the current file position for one or more handles, in the
order given; a handle that isn't currently open returns `¯1` in its
slot rather than failing the whole call. An empty argument returns
without producing any per-handle result.

`fnum ⎕FWRITE data`

Writes `data` to an open file as a single record; the record's length
comes from `data`'s own shape. Returns `1` if the write succeeded, `0`
on a write error. `File Tie Error` if the handle isn't open.

`fnum ⎕FCREATE 'filename'`

Same as `⎕FOPEN` but creates the file if it doesn't exist, and
truncates it if it does. Same caller-chosen-handle convention and
ambiguous-`0`-return caveat as `⎕FOPEN`.

`⎕FDIR path`

Lists a directory, returning a character matrix, one row per entry,
space-padded to the widest entry's width. `Length Error` if the path
string itself is too long; `WS Full` if the listing is unusually
large. Ctrl+C mid-listing returns `Interrupt` cleanly rather than
hanging.

`'old' ⎕FNAME 'new'`

Renames a file. Both names are length-capped (`Length Error` if either
is too long). Returns the raw result code from the underlying rename —
conventionally `0` for success, but treat the exact convention as
unconfirmed if it matters for your program.

`⎕FDEL 'filename'`

Deletes a file. Name length is capped (`Length Error` if too long).
Returns the raw result code from the underlying delete — same
return-value caveat as `⎕FNAME`.

`⎕FEOF fnum`

Returns `1` if the file is at end-of-file, `0` otherwise. `File Tie Error`
if the handle isn't open.

`fnum ⎕FAPPEND 'filename'`

Same as `⎕FOPEN` but writes go to the end of the file (creates it if
needed). Same caller-chosen-handle convention and ambiguous-`0`-return
caveat as `⎕FOPEN`/`⎕FCREATE`.

`⎕FFLUSH fnum`

Flushes any buffered writes for the file to disk. Returns `1` on
success, `0` on failure. `File Tie Error` if the handle isn't open.

---

## Graphics Functions

A string argument (title, tag, filename, text, pattern) that isn't text
gives `Domain Error` — which catches a window handle written on the
wrong side, e.g. `wnum ⎕WTAG 'tag'`.

`'title' ⎕WOPEN x,y,w,h`

`'title' ⎕WOPEN x,y,w,h,vis`

Opens a new graphics window and returns its handle (`≥1`). `title` may
be empty. `x,y` place the window; `¯1 ¯1` lets the window manager choose
a default position. `w` and `h` must both be greater than `0`, or you
get `Domain Error`. Handles are reused — closing window 1 and opening a
new one gives you 1 again, they aren't a running count. A newly opened
window may occasionally not grab keyboard focus right away, depending
on your window manager.

The optional 5th element `vis` is `1` (shown, the default) or `0`
(hidden). A hidden window is a full window in every other respect —
same handle numbering, and every drawing, pen, font, tag, pixel-read
and save function works on it — it just isn't on screen until
`wnum ⎕WCTRL 6` shows it. `x,y` is where it will appear when shown
(`¯1 ¯1` lets the window manager choose then). `Domain Error` if `vis`
is anything other than `0` or `1`.

`⎕WCLOSE wnum`

Closes one or more windows (`wnum` can be a vector of handles) and
returns the current window list, same as `⎕WNUMS`. Closing a handle
that isn't open is a silent no-op, not an error.

`wnum ⎕PEN ¯1`

`wnum ⎕PEN width`

`wnum ⎕PEN r,g,b`

`wnum ⎕PEN r,g,b,width`

Sets or queries a window's pen (the color and width used by `⎕POLY`,
`⎕WPOINTS`, `⎕FPOLY`, and `⎕WTEXT`). `wnum` may be a list of handles
for any of the three *set* forms above, applying the same change to
each — `¯1` (query) only works on a single handle.

`¯1` queries the window's current color and width, returning
`r,g,b,width`; querying a closed window returns an empty vector rather
than an error. A single number other than `¯1` sets width only, color
unchanged. `r,g,b` sets color only, width unchanged. `r,g,b,width`
sets both. Setting on a handle that isn't open silently no-ops for
that handle. Width is always clamped to at least 1.

`Length Error` if the argument's length isn't 1, 3, or 4, or if `¯1`
(query) is given more than one window handle.

`wnum ⎕POLY p,x,y,p,x,y,...`

Draws connected line segments from a flat stream of `p,x,y` triples
(count must be a multiple of 3). `p = ¯1` moves the pen to `(x,y)`
without drawing; any other `p` draws a line from the current pen
position to `(x,y)` in the window's current `⎕PEN` color and width,
then updates the pen position. A draw with no prior move starts from
`(0,0)`. `Length Error` if the argument length isn't a multiple of 3.

`⎕WNUMS`

`⎕WNUMS 'pattern'`

With no argument, returns the currently open window handles, in the
order they were opened. `¯1↑⎕WNUMS` is a handy way to get the handle
just opened by `⎕WOPEN` — but only right after a successful open, since
a failed open leaves nothing new to find.

With a `pattern` argument, filters that list by tag (see `⎕WTAG`): a
trailing `*` is a prefix match against everything before it, no
trailing `*` is an exact match. No interior wildcards, no escaping.
Empty vector, not an error, if nothing matches.

Pass `pattern` as a literal or a named variable (`⎕WNUMS 'palette*'`,
`⎕WNUMS pat`) — not as the unresolved result of an operator expression
(`⎕WNUMS ,'palette*'`). The latter currently compiles as if no argument
were given at all. Same caveat applies to `⎕WSIZE`'s `wnum` argument
below, and to `⎕WI`/`⎕IO`/`⎕DIG` in System Functions.

`wnum ⎕WCLEAR color`

Fills a window with a single packed `0xRRGGBB` color. `Length Error` if
either argument is empty; silently no-ops if the window isn't open.

`'title' ⎕WTITLE wnum`

Sets a window's title bar text. `Length Error` if either argument is
empty; silently no-ops if the window isn't open.

`wnum ⎕WPOINTS <point data>`

Plots individual points from an N×2 or N×3 array (or an equivalent flat
vector for a single point). N×2 rows are `(x,y)`, plotted in the
window's current `⎕PEN` color and size; N×3 rows are `(x,y,packedColor)`,
overriding color per point — size always comes from the current pen.
Points are drawn as small filled squares, not circles. `Domain Error` if
row width isn't 2 or 3.

`⎕WSIZE`

`⎕WSIZE wnum`

With no argument, returns `x,y,width,height` for the desktop (`x,y`
always `0,0`); with a window handle, returns that window's current
`x,y` position on the desktop followed by its `width,height`. Always 4
elements. Feeding this `x,y` back into a later `⎕WOPEN` reopens a
window in the same spot. `Domain Error` if the handle given isn't
currently open — `0` is not shorthand for "the desktop," it's just an
invalid handle like any other.

A hidden window reports its real `width,height`. Its `x,y` is where it
will appear when shown — or `¯1 ¯1` if it was opened hidden at
`¯1 ¯1` and hasn't been moved or shown since, meaning it has no
position yet. `¯1 ¯1` still means "default placement" if fed back into
`⎕WOPEN`.

`wnums ⎕WCTRL opcode`

`wnum ⎕WCTRL 4,x,y`

Window-state control. Opcodes `0`–`3`, `5` and `6` apply to every
handle in `wnums`: `0` minimizes, `1` restores, `2` brings to front,
`3` sends to back, `5` hides, `6` shows. Opcode `4` moves a single
window to `x,y` (same coordinate system as `⎕WOPEN`'s placement
argument and `⎕WSIZE`'s result); `wnums` must be exactly one handle
for `4`. Handles that aren't open are silently ignored. Returns
`wnums` unchanged — assign the result back to the same variable
(`W←W ⎕WCTRL 1`) to suppress APL's normal auto-display of an unused
result on its own line.

Hiding (`5`) keeps the window and everything drawn on it — it just
takes it off screen; drawing on it while hidden works normally. `6` is
the only way to show a hidden window: `0` and `1` do nothing to a
hidden window, and `6` does nothing to one that isn't hidden. `4`
works on a hidden window too, setting where `6` will show it — so
`W ⎕WCTRL 4,x,y` followed by `W ⎕WCTRL 6` shows it at `x,y`. A window
hidden while minimized comes back normal, not minimized. Use `⎕WVIS` to check which
state a window is in.

**Restore (`1`) doesn't work under GNOME Shell** (Activities/top bar
desktops) — it refuses to let an application un-minimize its own
window; click its taskbar/Activities entry instead. Other window
managers restore normally.

`Domain Error` if the opcode isn't `0`–`6`. `Length Error` for the
wrong number of elements (1 for `0`–`3`, `5` and `6`; 3 for `4`), or
if `4` is given more than one window handle.

`wnum ⎕FPOLY x,y,x,y,...`

Fills a polygon from a flat `x,y` vertex stream (vector or N×2 matrix —
only the total count matters), at least 3 vertices, filled in the
current `⎕PEN` color with no border stroke. The polygon closes itself —
don't repeat the first point as the last. Self-intersecting shapes
(like a figure-8) fill correctly with no special handling needed.
`Length Error` if the count is odd or under 3 vertices.

`⎕WCLICK wnums`

Waits for a left-click in any of the given windows, blocking execution
until it happens (the windows themselves keep repainting and
responding normally while you wait). Returns `handle,x,y` on a click;
returns an empty vector if the wait is cancelled (Escape, Ctrl+C) or one
of the armed windows closes first — these two cases aren't
distinguished. Only open, non-hidden windows are waited on — handles
that aren't open and hidden windows are skipped (a minimized window
still counts, since you can restore it and click). If none of the
windows given can be clicked, returns an empty vector right away
instead of waiting — not an error, since a window you closed just
before the call is treated the same as one closed during the wait.
`Length Error` if no handles are given.

`wnum ⎕WPIXEL <x,y point list>`

Reads back pixel colors for a flat `x,y` stream (N×2, same convention
as `⎕WPOINTS`), returning one packed `0xRRGGBB` per point. A point
outside the window's bounds returns `¯1` for that entry (packed colors
are never negative, so this is unambiguous) without failing the whole
call — only an unopened handle does that, with `Domain Error`.
`Length Error` if the count is odd. Fully synchronous — no waiting on
user input, unlike `⎕WCLICK`.

`'string' ⎕WTEXT wnum,x,y`

Draws text into a graphics window in the window's current `⎕PEN` color, 
using the window's current `⎕WFONT` face and size (the app's default 
face at size 14 unless changed). `(x,y)` is the text's
*baseline*, not its top-left corner, so the text sits above `y`.
Silently no-ops if the window isn't open. `Length Error` if the right
argument has fewer than 3 elements. **APL glyphs (`⎕`, `⍴`, `⍳`, etc.)
don't render correctly here** — they come out as garbled, mostly
accented Latin characters. Plain ASCII text is fine.

`'filename' ⎕WILOAD x,y`

`'filename' ⎕WILOAD x,y,vis`

Opens a new window whose entire content is an image loaded from disk,
sized exactly to the image. An absolute path is used as given;
anything else is resolved against an `images` folder under `$APLHOME`.
`x,y` places it, with `¯1 ¯1` for default placement, same as `⎕WOPEN`.
The window's title becomes the filename; any transparency in the
source image is discarded. Ordinary drawing calls (`⎕POLY`,
`⎕WPOINTS`, `⎕WCLEAR`, etc.) work normally on it afterward.
`Can't Load Image` if the file can't be loaded.

The optional 3rd element `vis` works exactly as `⎕WOPEN`'s: `1`
(shown, the default) or `0` (loaded hidden, shown later with
`wnum ⎕WCTRL 6` at `x,y`, or wherever the window manager chooses for
`¯1 ¯1`). `Domain Error` if `vis` isn't `0` or `1`.

`'tag' ⎕WTAG wnum`

Sets one or more windows' tag: a free-form label, independent of a
window's title, used later for lookup via `⎕WNUMS 'pattern'` or
reverse-lookup via `⎕WTAGOF`. `wnum` may be a list of handles — the
same tag is applied to each. An empty tag string is a legitimate value
— it's how a window ends up untagged. Duplicate tags across different
windows are expected and fine (e.g. tagging several related windows
`'palette'` and finding them all at once via `⎕WNUMS 'palette*'`).
Silently no-ops for any handle in the list that isn't currently open.
A window's tag is cleared automatically when it closes — a reused
handle never inherits an old tag.

`⎕WTAGOF wnum`

Returns `wnum`'s current tag, or an empty string if the window isn't
currently open, or is open but untagged — these two cases aren't
distinguished. The reverse of `⎕WTAG`: useful after `⎕WCLICK` returns a
bare handle and you want to know which window it actually was.

`wnum ⎕WFILL x,y`

`wnum ⎕WFILL tol,x,y`

Flood fill ("paint bucket") from a single point, replacing every
connected pixel matching that point's starting color with the window's
current `⎕PEN` color. With just `x,y`, matching is exact — fine
against this module's own drawing functions, which all draw with hard
edges. `tol` (first, not last — detected by the argument having 3
elements instead of 2) allows each color channel to differ from the
starting point's color by up to `tol` (0-255 scale) and still count as
a match — useful against a `⎕WILOAD`ed photo or anything else with
soft edges, where an exact match would rarely fill more than a single
pixel. Silently no-ops if `x,y` is outside the window, or the window
isn't open. `Length Error` if the argument's length isn't 2 or 3.

`'filename' ⎕WIRELOAD wnum`

`'filename' ⎕WIRELOAD ¯1`

Reloads a new image into an already-open window in place, same
dimensions only — there's no resize. Path resolution matches `⎕WILOAD`
(`$APLHOME/images/` unless absolute). Works on any open window, not
just ones originally opened via `⎕WILOAD` — a plain `⎕WOPEN`'d window,
or one with `⎕POLY`/`⎕WPOINTS`/etc. already drawn on it, is a valid
target too, as long as the new image's size matches. `¯1` in place of
`wnum` is a query, not a reload: it skips reloading anything and
instead reports the file's `width,height` (2 elements) without
touching any window — useful for checking whether a file will fit
before attempting a real reload, or for building a list of compatible
files.

**Unlike other window-targeting calls, failure here does not raise an
error** — it returns a negative number instead, so a program can test
the result rather than being aborted (closing the window via its
title bar at the wrong moment shouldn't crash a running program).
On a real reload, success returns `wnum` itself, not the filename.
`¯1` = the file couldn't be decoded (bad path, corrupt file,
unsupported format); `¯2` = `wnum` isn't currently an open window;
`¯3` = the file decoded fine, but its dimensions don't match the
window's current size. Any negative result means it failed; the exact
value only matters if you care which of the three happened. In query
mode, a decode failure returns an empty vector instead — "nothing to
report" rather than a raised error, same convention `⎕WCLICK`/`⎕WNUMS`
use for their own empty-result cases.

`'filename' ⎕WISAVE wnum`

`'filename' ⎕WISAVE wnum,¯1`

Saves a window's current backing image to a file — the mirror of
`⎕WILOAD`. Format is inferred from `filename`'s extension; writable
formats are PNG, JPEG/JPG, BMP, TIFF, WEBP, ICO/ICNS/CUR, PBM/PGM/PPM,
XBM/XPM, and WBMP. **Not symmetric with `⎕WILOAD`**: GIF, SVG/SVGZ, and
TGA can be loaded but not saved back out — pick a different extension
for those. Path resolution matches `⎕WILOAD` (`$APLHOME/images/`
unless absolute).

With just `wnum`, fails if the target file already exists — the
conservative default. Adding `,¯1` allows overwriting silently
instead. Any other second value, or more than 2 elements, is
`Length Error`.

As with `⎕WIRELOAD`, `wnum` not being currently open, or the file
already existing without the `¯1` override, does **not** raise an
error — both return a negative number instead (`¯2` and `¯3`
respectively) so a program can test for either condition. An actual
disk write failure (permissions, disk full, an unwritable format per
the list above) is the one failure here that *does* raise an error:
`Can't write save file`.

`'title' ⎕FDIALOG 'filter'`

Opens a modal "open existing file" dialog. `title` may be empty.
`filter` is Qt's native filter syntax (e.g. `'Images (*.png *.jpg)'`);
empty means no restriction, and matching is case-insensitive
(`'*.JPG'` also matches lowercase files). Single file, open-existing
only — no save mode, directory picker, or multi-select.

Returns the chosen file's full path, or an empty string on
Cancel/Escape — not an error. Uses Qt's own dialog by default (movable,
follows Dark Mode); switch to the native OS picker via Settings → "Use
native file picker."

`'' ⎕WFONT wnum`

`'' ⎕WFONT wnum,size`

`'face' ⎕WFONT wnum`

`'face' ⎕WFONT wnum,size`

`'?' ⎕WFONT wnum`

Sets or queries the font `⎕WTEXT` draws with in a window, which is
also what `⎕TWID` measures with. Each window has its own setting; a
new window starts with the app's default face at size 14.

`size` is in pixels, and is the font's overall design size rather than
the height of any one letter: at size 14 a capital letter is about 10
pixels tall, and lines of text need about 16 or 17 pixels between
baselines, varying a little by face. `¯1` restores the default size.

An empty left argument leaves the face alone, so `'' ⎕WFONT wnum`
returns the current size and `'' ⎕WFONT wnum,size` just changes it.

Any other left argument sets the face, keeping the current size unless
one is given. Use a font's full family name as `)FONT` lists it — case
and extra spaces don't matter, but it isn't matched by leading letters
the way a `)FONT` pattern is. Several names separated by commas are
tried in order, the first one installed being used (e.g.
`'APL385 UNICODE, DEJAVU SANS MONO'`), which helps a program run on
machines with different fonts. `'DEFAULT'` restores the default face.
A name that isn't installed is not an error: a substitute font is used
without warning.

`'?'` returns the name of the face actually in use, spelled the way
the system spells it — the way to check whether a name was found, or
which one of a list was chosen. Compare it using `⎕UCASE` if needed.

Every form that sets something returns the *previous* size, so
`old←'' ⎕WFONT w,24` and later `'' ⎕WFONT w,old` puts it back. If the
window isn't open, the result is an empty vector (an empty string for
`'?'`) and nothing is changed.

`Domain Error` if `size` is below 1 (other than `¯1`), if the face is
only commas and spaces, or if the left argument isn't text. `Length
Error` if the right argument isn't 1 or 2 elements, if `'?'` is given
a size, or if the face is 256 characters or longer.

`'string' ⎕TWID wnum`

Returns the width in pixels `string` would take if drawn by `⎕WTEXT`
in that window right now, using its current `⎕WFONT` face and size.
Nothing is drawn. The width runs from where the text starts to where
following text would start, so it can be used to place text end to end
or to center a label: `s ⎕WTEXT w,(cx-(s ⎕TWID w)÷2),y` centers `s` on
`cx`. Measure a whole string rather than adding up its pieces — some
letter pairs are drawn closer together, so the pieces can add up to a
pixel or so more.

An empty string gives 0. Returns an empty vector if the window isn't
open. `Rank Error` if `string` is a matrix. `Length Error` if the right
argument isn't exactly one window handle, or if `string` is longer than
the interpreter's input line.

`⎕WVIS wnums`

Returns each window's visibility state, one number per handle, in the
order given: `0` hidden, `1` shown, `2` minimized, `¯1` not open. The
result is always a vector, even for a single handle. A window hidden
while minimized reports `0`. A handle that isn't open gets `¯1` in its
place rather than causing an error, and an empty argument gives an
empty result — so `⎕WVIS ⎕WNUMS` works even with no windows open, and
`⎕WVIS ⎕WNUMS 'pal*'` checks just the tagged ones.

To get just the shown windows with a given tag, keep the handle list in
a variable and compress it:

```
W←⎕WNUMS 'pal*'
(1=⎕WVIS W)/W
```

Use `(0<⎕WVIS W)/W` to include minimized windows too. Assign
`⎕WNUMS`'s result to a variable first rather than using it inside a
larger expression — see the caveat under `⎕WNUMS` above.

`dst ⎕WBLIT src`

`dst ⎕WBLIT src,dx,dy`

`dst ⎕WBLIT src,dx,dy,sx,sy,sw,sh`

Copies pixels from window `src` into window `dst`. With just `src`, the
whole of `src` is copied to `dst`'s top-left corner; `src,dx,dy` puts
it at `dx,dy` instead; the 7-element form copies only the `sw`×`sh`
rectangle starting at `sx,sy` in `src`. The copied pixels simply
replace what was there — there's no transparency. Either window can be
shown or hidden, so the usual pattern is to draw a frame in a hidden
window and then `WBLIT` it to a shown one in a single step. `src` and
`dst` may be the same window, even with overlapping rectangles (handy
for scrolling). Returns `dst`.

Any part of the rectangle that falls outside either window is simply
left out, including negative `dx,dy`. If either handle isn't open, the
call does nothing. `Length Error` if the right argument doesn't have
1, 3 or 7 elements; `Domain Error` if `sw` or `sh` is negative.

