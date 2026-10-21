<!-- font: Georgia, DejaVu Serif, serif -->
# Quad Function Reference

Calling shape and behavior for each `⎕` function, for quick lookup while
you're working. A window handle (`wnum`) is a number returned by `⎕WOPEN`.
Packed colors are 24-bit `0xRRGGBB` values unless a function says
otherwise.

---

## Settable functions: `⎕IO←n` and `⎕IO n`

`⎕INTDIV`, `⎕RL`, `⎕WI`, `⎕IO`, `⎕DIG`, `⎕SEQTEMPO`, `⎕SEQPLAY`, `⎕SEQBEAT`
and `⎕SEQMETER` report a setting when used alone and change it when given a value. The value can be given two
ways:

- `⎕IO←0`, the assignment form, as for an APL system variable.
  Everything to the right of `←` is the value, as with any assignment,
  and nothing is displayed.
- `⎕IO 0`, the original form. The prior value is displayed unless the
  result is assigned or used.

Both forms return the *prior* value, so `OLD←⎕IO←0` saves the old
setting for restoring later. This differs from APL's system variables,
where `X←⎕IO←0` gives `X` the new value, `0`.

With the original form, whether `⎕IO` has an argument depends on what
follows it. A value, or a function that has a monadic form, gives it
one (`⎕IO ⍴X` sets `⎕IO` to `⍴X`). A function with only a dyadic form
doesn't (`⎕IO+1` adds 1 to `⎕IO`). The assignment form leaves nothing
to work out.

The `-q` startup option picks which forms are accepted (`aplterm -q
new`, `apl_cli -q new`):

- `both` (the default): both forms.
- `warn`: both forms, and `)ENTER` prints a note for each function
  line that uses the original form, such as `old form ⎕IO X: FN[3]`.
  Use it to find the lines to convert.
- `new`: the assignment form only. Without `←`, `⎕IO` takes no
  argument and reads like a variable: `⎕IO ⍴X` is dyadic `⍴`, and
  `⎕IO 0`, `⎕IO X` and `⎕IO ¯1` are Syntax Errors. A function that
  uses the original form gives a Syntax Error on that line when
  `)ENTER`ed. `⎕IO-1` and `⎕IO - 1` are subtraction.

The option affects only code compiled in that session: typed lines,
`)ENTER`, `⍎` and `⎕` input. Functions already in a saved workspace
keep working in every mode.

`⎕WSIZE wnum` and `⎕WNUMS 'pattern'` keep the original form in every
mode. Their argument chooses what to report rather than setting
anything, so there's no `⎕WSIZE←` or `⎕WNUMS←` form.

---

## System Functions

`⎕TS`

Returns the current timestamp as 7 numbers: year, month, day, hour,
minute, second, and a 7th field reserved for fractions of a second (ms)
but may not be implemented.

`⎕DL n`

Delays execution by `n`, flushing pending output first. The exact time
unit isn't confirmed — don't assume seconds vs. centiseconds if timing
precision matters.

`⎕INTDIV`

`⎕INTDIV←n` or `⎕INTDIV n`

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

`⎕RL←n` or `⎕RL n`

Gets or sets the random link, the state of the generator behind `?`.
Setting returns the *prior* value. Setting the same `n` repeats the same
sequence of `?` results, on any platform; `S←⎕RL` saves the current
point, and `⎕RL←S` later carries on from it. `n` is `1`–`2147483646`,
or `0` to go back to unpredictable results; anything else is
`Domain Error`. Not saved with the workspace.

`⎕WA`

Returns available workspace ("watermark") in bytes.

`⎕WI`

`⎕WI←n` or `⎕WI n`

Gets or sets the output line width. Setting returns the *prior* value.
A width below 20 is silently clamped up rather than rejected.

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

`⎕IO←n` or `⎕IO n`

Gets or sets the index origin (same setting `)ORIGIN` controls).
Setting returns the *prior* value; `Domain Error` if set to anything
other than `0` or `1`.

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
finish), returning the command's result code. Its output doesn't appear
in the APL session. See `⎕WEXEC` to wait for completion.

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

`⎕DIG←n` or `⎕DIG n`

Gets or sets `)DIGITS` print precision. Setting returns the *prior*
value; `n` is clamped to `¯17…17`. Positive means up to that many
decimal places (trailing zeros trimmed); negative means exactly that
many (trailing zeros kept, e.g. `¯2` for dollars and cents); `0` is
valid either way.

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

Whether `⎕WNUMS` has an argument depends on what follows it: a value,
or a function with a monadic form (`⎕WNUMS ,'palette*'`), gives it one;
a function with only a dyadic form (`⎕WNUMS+1`) doesn't. The same goes
for `⎕WSIZE`'s `wnum` below. Neither has a `←` form (see Settable
functions at the top).

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

`⎕WCLICK list`

Waits for a left-click in any of the windows in `list`, blocking
execution until it happens (the windows themselves keep repainting and
responding normally while you wait). Returns `handle,x,y` on a click.
`⎕WCLICK` and `⎕EVWAIT` are the same function under two names, so
`list` may also hold `¯1` to wait for the sequencer's beat at the same
time; `⎕EVWAIT` under Sequencer Functions describes the wait in full.
In short: the result is an empty vector if the wait is cancelled
(Escape, Ctrl+C) or one of the windows waited on closes first, and
these cases aren't distinguished. After Ctrl+C the rest of the line
still runs, then the interrupt is reported. Only open, non-hidden
windows are waited on — handles that aren't open and hidden windows
are skipped (a minimized window still counts, since you can restore it
and click). If nothing in `list` can be waited on, returns an empty
vector right away instead of waiting — not an error, since a window
you closed just before the call is treated the same as one closed
during the wait. `Length Error` if `list` is empty, `Rank Error` if
it's a matrix, `Domain Error` for text, a fraction, or a negative
number other than `¯1`.

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
empty means no restriction, and with the default dialog matching is
case-insensitive (`'*.JPG'` also matches lowercase files). The native
OS picker (see below) may match case exactly: there, give both,
`'*.JPG *.jpg'`. Single file, open-existing
only — no save mode, directory picker, or multi-select.

`filter` may start with a folder to open in: `'MIDI/*.MID'` opens in
`$APLHOME/midi` showing `*.MID` files, and `'MIDI/'` alone opens there
with no restriction. The folder is everything up to the last `/` or `\`
before the first `*`, `?`, `(` or `;`, so `'MIDI/MIDI files (*.MID)'`
works too. A folder that isn't a full path (starting with `/` or a
drive letter) is under `$APLHOME`. It is looked for as typed, then in
lower case (the last folder name only, for a full path), so `MIDI`
finds `midi`. A folder that doesn't exist isn't an error: the dialog
opens where it otherwise would, with the rest of the filter applied.
Without a folder, Qt chooses where to open, usually the last folder
used.

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

Use `(0<⎕WVIS W)/W` to include minimized windows too. Keeping the list
in a variable means `⎕WNUMS` runs once, so both uses see the same
handles.

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

---

## Sequencer Functions

The sequencer plays MIDI tracks in the background, looping them while
APL keeps working, and takes a new version of a track at that track's
next loop boundary so music never stops for an update.

A track is a matrix of 2 to 5 rows, one column per note: row 1
pitches, row 2 steps (how long until the next note starts) in beats,
and optionally row 3 velocities, row 4 lengths (how long each note
sounds) in beats and row 5 channels. Pitches are MIDI note numbers
`0`–`127` (`0` is the lowest C, a real note); `¯1` is a rest. Steps
must be greater than `0` and may be fractional (`0.5`, `4÷3`); thirds,
fifths and sevenths of a beat are kept exact, so tracks never drift
apart. A beat is whatever `⎕SEQTEMPO` makes it: at `140` a step of
`0.5` lasts about 214 ms, and the same matrix plays faster or slower
as the tempo changes. All five rows are described under `⎕SEQTRACK`,
with a table of note values as steps.

**Units (`)SEQUNIT`).** Steps and lengths can be counted in units
smaller than a beat, so that the values in everyday use are whole
numbers. `)SEQUNIT 24` divides each beat into 24: counting quarter
notes, a sixteenth is `6`, a thirty-second `3` and an eighth-note
triplet `8`. Under a unit, everything in this section given in beats
is in units instead: the steps and lengths of a track (rows 2 and 4),
`⎕SEQTRACK`'s result, `⎕SEQSTATE`'s current position, loop lengths
and track positions, `⎕MIDIREAD`'s steps and lengths, `⎕MIDIINFO`'s
track lengths, and `⎕SEQRUNTO`. `⎕SEQTEMPO` is still beats per
minute, `⎕SEQGATE` still a fraction of the step, and the beat numbers
`⎕EVWAIT` reports still count beats. The unit is saved with the
workspace, since the numbers in its track matrices depend on it, and a
new workspace starts at `1`, beats (see `)SEQUNIT` in the System
Command Reference). MIDI files keep exact timing rather than units, so
a file written at one unit reads back correctly at any other.

Tracks are numbered `1`–`16` and channels `1`–`16`, whatever `⎕IO` is.
A track's channel starts as its own number (so track 10 plays on the
General MIDI drum channel unless changed) and keeps whatever channel it
was last given.

**Track lists.** `⎕SEQMUTE`, `⎕SEQLOOP`, `⎕SEQVEL`, `⎕SEQGATE`,
`⎕SEQPROG`, `⎕SEQBANK`, `⎕SEQNOTES` and `⎕SEQCLEAR` take a list of tracks and apply to all of them at the same
instant. The
other argument is one value for all of them or one per track. The
result is the previous setting for each track listed. Only `⎕SEQTRACK`
reads two numbers on the left as track and channel; to the list
functions, `1 3` is tracks 1 and 3.

**Whole numbers.** Arguments that must be whole numbers (tracks,
channels, pitches, velocities, settings) may be given as floats as long
as they're whole: `8÷2` is fine as a track number. A fraction is a
`Domain Error`. Steps, lengths and the gate are never limited to whole
numbers.

**States.** The sequencer is closed, stopped, playing, or "port lost":
the device stopped taking data, for example because its cable was
unplugged. Playback then stops and the port closes, but tracks and
settings are kept. Use `⎕SEQSTATE` to check which state it's in
without risking an error. Every function except `⎕SEQOPEN`,
`⎕SEQCLOSE`, `⎕SEQSTATE`, `⎕SEQPORTS` and `⎕SEQLOG` gives `Sequencer
Not Open` when closed.

A basic session:

```
⎕SEQOPEN 'hw:3,0,0'
M ← 2 8⍴60 62 64 65 67 65 64 62, 8⍴1
1 ⎕SEQTRACK M
⎕SEQTEMPO←100
⎕SEQPLAY←1
1 ⎕SEQTRACK M2          ⍝ takes over at the next 8-beat boundary
⎕SEQPLAY←0
⎕SEQCLOSE
```

`⎕SEQOPEN 'port'`

Opens the MIDI output port and readies the sequencer, stopped, with no
tracks, at tempo 120. `port` is either the port's system name or part
of its device name:

- On Linux the system name is the way ALSA names a raw MIDI device,
  such as `'hw:3,0,0'` (card 3, device 0, subdevice 0), or `'hw:3,0'`
  (any free subdevice) or `'hw:3'` (device 0). On Windows it's the
  device number, as text or a number.
- Anything else is a device name: the first port in `⎕SEQPORTS`'s list
  whose label contains it is opened, so `⎕SEQOPEN 'U2MIDI'` opens the
  port labelled `U2MIDI Pro / U2MIDI Pro MIDI 1`. A device name keeps
  working when card or device numbers change, as they can when USB
  devices come up in a different order, or after an unplug. The name
  is looked up afresh each time a port is opened. Only the first
  matching port is tried; to choose another port of the same device,
  give more of its label (`'MIDI 2'`), or its system name.

Upper or lower case doesn't matter in either form. In a label, any
character outside plain ASCII shows as `?` and is matched as `?`.

`⎕SEQOPEN ''` opens the port chosen under **MIDI Output Port** in
Settings, so a program needn't name a port at all. The setting keeps
the port's whole label, so it finds the same port again when its card
or device number changes, and the result is the same as for a device
name: `0` if that port isn't plugged in. With no port chosen,
`⎕SEQOPEN ''` gives `0`. A change in Settings is used from the next
`⎕SEQOPEN ''`; a port already open stays open.

The result says whether it worked, so a program can test for a missing
or busy device without being stopped by an error:

| Result | Meaning |
| --- | --- |
| `1` | opened |
| `0` | no such port (for a device name: no port's label contains it) |
| `¯1` | in use (by another program, or every subdevice taken) |
| `¯2` | no permission (Linux: not in the `audio` group). For a device name, also when no label matched but some device couldn't be read for lack of permission, since that is the likelier reason |

Test with `1=⎕SEQOPEN port`. `⎕SEQOPEN` on a sequencer that is already
open is an error, so to open it only if needed, test the state from
`⎕SEQSTATE` first: greater than `0` means open.

```
S←(,⎕SEQSTATE)[⎕IO+1]   ⍝ the state: row 1, column 2
→(S>0)/READY            ⍝ already open: skip ⎕SEQOPEN
```

After the port was lost, `⎕SEQOPEN` opens a port again (it may be a
different one) and keeps the tracks, settings and position, so
`⎕SEQPLAY←1` carries on where it stopped. Nothing opens the sequencer
automatically; a workspace's boot function can. `Sequencer Already
Open` if it's open and the port is working. `MIDI Port Lost` if a port
reopened after a loss fails again at once. `Domain Error` if `port`
isn't text (or a number, on Windows), or (Linux) starts with `hw:` but
isn't one of the forms above. `Rank Error` if it's a
character matrix.

`⎕SEQCLOSE`

Stops playback, turns off every sounding note, closes the port and
discards all tracks and settings. Works in every state, including after
the port was lost, and never gives an error. Returns the numbers of the
tracks that held something (a play-once track that has finished
included), or an empty vector if none did (including when already
closed).

`⎕SEQPORTS`

The MIDI output ports this machine has, as a character matrix with one
row per port: the port's system name (what `⎕SEQOPEN` takes), then its
label, the device's own names, which a device name given to `⎕SEQOPEN`
is matched against:

```
      ⎕SEQPORTS
hw:3,0,0  U2MIDI Pro / U2MIDI Pro MIDI 1
```

The rows are in the order a device name is matched in, so the first row
whose label contains a name is the port that name opens. On Linux the
label is the card, device and subdevice names (repeats left out); on
Windows, the device name. Any character outside plain ASCII shows as
`?`. A device that can't be read for lack of permission (Linux: not in
the `audio` group) isn't listed. With no ports the result is a 0×0
matrix. Works whether the sequencer is open or closed, and never gives
an error.

`track ⎕SEQTRACK M`

`track,channel ⎕SEQTRACK M`

Gives `track` a new version, the matrix `M` (2 to 5 rows). A track
holds one note at a time; chords are spread across tracks. If the
track has a version playing, the new one waits and takes over at the
end of the track's current loop, so the music doesn't break and the
track stays in step with itself. If the track has nothing playing
(never used, cleared, or a play-once track that has finished), the new
version starts from its first note on the next beat. This is the same
whether or not the sequencer is playing; before the first start, at
beat 0, it takes over as soon as playback starts. Sending another
version before it takes over replaces the waiting one. Sending a
finished play-once track again plays it again from the next beat; a
restart (`⎕SEQPLAY←2`) plays it again from beat 0 with the rest.

Returns the beat at which the version takes over, counting from the
start of playback (in `)SEQUNIT`'s units, if set). That's as things stand when it returns: a restart
(`⎕SEQPLAY←2`), a `⎕SEQCLEAR` of the track or a newer version can
change it. A tempo change doesn't.

With `channel`, the track plays on that channel from this version on,
and keeps it for later versions sent without one.

```
1 ⎕SEQTRACK M2          ⍝ at beat 5.3, track 1 playing 8-beat loops
8
3 ⎕SEQTRACK B           ⍝ track 3 had nothing playing
6
2 5 ⎕SEQTRACK C         ⍝ new track 2, on channel 5 from now on
6
```

**Rows 1 and 2, the notes.** Each column is one note, in the order
they play.

- **Row 1, pitch**: a MIDI note number `0`–`127`. `60` is middle C,
  and each step of 1 is a semitone, so `72` is the C an octave above
  and `48` the one below. `¯1` is a rest: it sends nothing and takes
  up its step. `0` is a real note (the lowest C), not a rest.
- **Row 2, step**, in beats (or `)SEQUNIT`'s units): the time from this note's start to the
  next note's start; for the last column, to the end of the loop. The
  step is where the note sits in time. Without row 4 it is also how
  long the note sounds (times the track's gate, `⎕SEQGATE`, `1` by
  default), since a track sounds one note at a time: a note stops
  where the next note, or rest, begins. Every step must be at least
  1/2520 of a beat, one of the sequencer's ticks (at `)SEQUNIT 24`,
  1/105 of a unit).

The track's loop is as long as its steps add up to, `+/M[2;]` beats,
and that is where a new version takes over. Tracks of different
lengths drift in and out of step with each other and come back
together where their lengths meet (a 3-beat and a 4-beat track every
12 beats).

A beat is whatever note value you count in. Counting quarter notes,
in beats and at `)SEQUNIT 24`:

| Note | Step | At `)SEQUNIT 24` |
| --- | --- | --- |
| whole, half, quarter | `4`, `2`, `1` | `96`, `48`, `24` |
| eighth, sixteenth, thirty-second | `0.5`, `0.25`, `0.125` | `12`, `6`, `3` |
| dotted quarter, dotted eighth | `1.5`, `0.75` | `36`, `18` |
| eighth-note triplet, quarter-note triplet | `÷3`, `2÷3` | `8`, `16` |

Steps are added up exactly, so a loop of triplets is exactly as long
as its steps say and never drifts against the other tracks: thirds,
fifths, sevenths and ninths of a beat are exact, and any other
fraction is rounded only to the nearest 1/2520 of a beat, without the
rounding adding up along the loop. The same pitch twice in a row is
played twice: the first note stops just as the second starts.

`⍴` fills a matrix row by row, so the pitches come first, then the
steps:

```
⍝ eight quarter notes: a 2-bar loop in 4/4
M←2 8⍴60 62 64 60 60 62 64 60, 8⍴1
⍝ dotted eighth, sixteenth, quarter, then a quarter rest: 3 beats
M←2 4⍴67 69 71 ¯1, 0.75 0.25 1 1
⍝ triplets against eighths: two tracks, both 2 beats long
1 ⎕SEQTRACK 2 6⍴60 64 67 72 67 64, 6⍴÷3
2 ⎕SEQTRACK 2 4⍴36 43 48 43, 4⍴0.5
```

A step lasts as long as the tempo says: at `⎕SEQTEMPO←90` a beat is
2/3 of a second. A long note is a long step; a short, detached note
is a short length in row 4 or a gate below `1` (`⎕SEQGATE`), with
the step left as it is.

**Rows 3–5.** They give each note its own setting; `¯1` in any of them
means the track's setting for that note. For a rest they aren't looked at, so
anything there is accepted.

- **Row 3, velocity** `1`–`127`. It is scaled by the track's velocity
  (`⎕SEQVEL`): the note is played at its own velocity times the
  track's, divided by `100`, rounded and kept within `1`–`127`. At the
  default track velocity of `100` it plays as written; a lower track
  velocity turns the whole track down while keeping its accents.
- **Row 4, length** in beats (or `)SEQUNIT`'s units): how long the note sounds, from at least
  1/2520 of a beat up. A length longer than the step is shortened to
  the step, so the note runs into the next one, as every note does
  without row 4. `¯1` sounds the track's gate (`⎕SEQGATE`) times the
  step.
- **Row 5, channel** `1`–`16`, for that note alone. The track's own
  channel doesn't change.

```
M←3 4⍴60 62 64 65, 1 1 1 1, 100 70 70 ¯1    ⍝ accent the first note
1 ⎕SEQTRACK M
M←4 2⍴60 67, 0.5 0.5, ¯1 ¯1, 0.4 ¯1          ⍝ 60 sounds 0.4 of its 0.5
1 ⎕SEQTRACK M
```

`Length Error` if the left argument isn't 1 or 2 elements, or `M` has
fewer than 2 or more than 5 rows, or no columns. `Rank Error` if `M`
isn't a matrix, or the left argument isn't a scalar or vector. `Domain
Error` if the track or a channel isn't `1`–`16`, a pitch isn't
`0`–`127` or `¯1`, a step isn't greater than `0` (or is too short to
time: under 1/2520 of a beat), a velocity isn't `1`–`127` or `¯1`, a
length isn't at least 1/2520 of a beat or `¯1`, a velocity or channel
is a fraction, or either argument is text.

`⎕SEQCLEAR tracks`

Removes the tracks given, each at the end of its current loop (at once
if nothing is playing, or for a play-once track that has finished).
Their settings are kept for later versions. Returns those of the given
tracks that held something, a finished play-once track included, as
`⎕SEQCLOSE` does. An empty list gives an empty result. `Domain Error` if a track
isn't `1`–`16`. `Rank Error` if `tracks` is a matrix.

```
⎕SEQCLEAR 2 4           ⍝ only track 4 held anything
4
```

`tracks ⎕SEQMUTE flags`

Mutes (`1`) or unmutes (`0`) tracks, at once; muting turns off the
track's sounding notes. A muted track keeps its place, so unmuting
brings it back in time with everything else. Returns the previous
settings: a scalar if `tracks` is one, otherwise a vector. `Domain
Error` if a track isn't `1`–`16` or a flag isn't `0` or `1`. `Length
Error` if `flags` isn't one value or one per track. `Rank Error` if
either argument is a matrix.

```
1 3 ⎕SEQMUTE 1          ⍝ mute tracks 1 and 3
0 0
1 3 ⎕SEQMUTE 0 1        ⍝ unmute 1, keep 3 muted
1 1
```

`tracks ⎕SEQLOOP flags`

Sets whether tracks loop (`1`, the default) or play once and fall
silent (`0`), from each track's next loop boundary. A track that has
played once keeps its notes, silent, until a restart (`⎕SEQPLAY←2`)
plays it again with the rest; sending it again with `⎕SEQTRACK` plays
it from the next beat. Setting a finished track back to `1` doesn't
start it; the setting applies from the restart. A finished track's row
in `⎕SEQSTATE` has column 12 set, and `⎕MIDIWRITE` writes it.
Returns the previous settings. Same errors as `⎕SEQMUTE`.

```
2 ⎕SEQLOOP 0            ⍝ track 2 plays once more, then stops
1
```

`tracks ⎕SEQVEL velocities`

Sets how hard notes are struck, `1`–`127` (default `100`), for notes
started from now on. Notes with a velocity of their own (row 3 of the
track's matrix) are scaled by it instead: played at their velocity
times this, divided by `100`. Returns the previous settings. `Domain
Error` if a velocity isn't `1`–`127`. `Length Error` as for
`⎕SEQMUTE`.

```
1 3 ⎕SEQVEL 100 70      ⍝ track 3 softer
100 100
```

`tracks ⎕SEQGATE fractions`

Sets how much of each note's step it sounds, as a fraction greater
than `0` and at most `1` (default `1`, the whole step), for notes
started from now on. At `0.5` a note sounds for half its step and is
silent for the other half; shorter values make the track more
detached. The note-off is never less than 1/2520 of a beat after the
note-on. A note with a length of its own (row 4 of the track's matrix)
keeps it; the gate applies to notes with no row 4 or `¯1` there. Kept
for later versions and through `⎕SEQCLEAR`, like the other settings.
Returns the previous settings. `Domain Error` if a fraction isn't
greater than `0` and at most `1`. `Length Error` as for `⎕SEQMUTE`.

```
1 2 ⎕SEQGATE 0.5        ⍝ tracks 1 and 2 detached
1 1
1 ⎕SEQGATE 1            ⍝ track 1 legato again
0.5
```

`tracks ⎕SEQPROG programs`

Sets the program (the instrument or preset) of each track listed,
`0`–`127`, the number as sent (a synth's manual may count from 1:
its program 1 is `0` here). `¯1`, the default, is none: nothing is
sent, and the synth keeps whatever it had. A track's program is a
setting like its channel: kept for later versions and through
`⎕SEQCLEAR`, gone at `⎕SEQCLOSE`. Returns the previous settings.

The program isn't sent at once. It goes out on the track's channel at
the start of the track's next pass, just before its first note:

- for a track with nothing playing, when its new version starts, so
  `1 ⎕SEQPROG 40` and `1 ⎕SEQTRACK M` take effect together, in either
  order;
- for a track playing, at the end of its current pass (its next loop
  boundary), whether or not a new version takes over there.

It is sent only when something changed: a new program or bank, or a
new version on another channel. A new version with the same program
and channel sends nothing, since some synths cut sounding notes at a
program change. It is sent again at a restart (`⎕SEQPLAY←2`), so a
restart always sounds the same, and when the port is opened again
after it was lost (the synth may have been reset). A stop and continue
sends nothing. A muted track's program is sent like any other's.
Notes with a channel of their own (row 5 of the track's matrix) get
nothing from it. Two tracks on one channel with different programs:
whichever was sent last wins.

`Domain Error` if a program isn't `¯1`–`127`. `Length Error` as for
`⎕SEQMUTE`.

```
1 2 ⎕SEQPROG 0 32       ⍝ piano and bass on tracks 1 and 2
¯1 ¯1
```

`tracks ⎕SEQBANK banks`

Sets the bank of each track listed, `0`–`16383`, for synths with more
than 128 programs. It is sent just before the program, as bank select
controllers 0 and 32 (`⌊b÷128` and `128|b`), whenever the program is;
a bank with no program (`¯1`) sends nothing, since a bank takes effect
only at the next program change. A manual that lists banks as two
numbers (MSB and LSB) converts with `128⊥MSB LSB`. `¯1`, the default,
is none: only the program is sent. Kept like the program; returns the
previous settings. `Domain Error` if a bank isn't `¯1`–`16383`.
`Length Error` as for `⎕SEQMUTE`.

```
1 ⎕SEQBANK 128⊥1 0      ⍝ MSB 1, LSB 0
¯1
```

The settings show in `⎕SEQSTATE`'s columns 13 and 14, and
`⎕MIDIINFO`'s columns 8 and 9 (the program and bank a file's voice
starts with) pass straight on:

```
I←⎕MIDIINFO 'song.mid'
1 ⎕SEQPROG I[2;8]
1 ⎕SEQBANK I[2;9]
```

`track ⎕SEQNAME 'name'`

Names a track, for MIDI files: `⎕MIDIWRITE` writes it into the file
as the track's name, which other programs show for the track. Playback
doesn't use it. One track per call, its name as text.
`''` takes the name away. Trailing blanks are dropped (so a row of
`⎕SEQNAMES` can be given back as it is); leading ones are kept. Returns
the previous name, empty if there was none. A track's name is kept
like its program: through new versions and `⎕SEQCLEAR`, gone at
`⎕SEQCLOSE`.

A name may hold letters (either case), digits, the space, `-`, and
`. , ( ) ' & + _`. Anything else is a `Domain Error`: APL symbols,
because there is no telling how another program would show them, and
the rest of the punctuation, because some of it isn't stored as the
character it shows as (a typed `#` displays as `⎕`). Up to 64
characters.

```
      1 ⎕SEQNAME 'DOUBLE BASS'     ⍝ track 1 was called BASS
BASS
```

`Length Error` if `track` isn't a single number or the name is longer
than 64 characters. `Domain Error` if the track isn't `1`–`16`, the
name isn't text, or it holds any other character. `Rank
Error` if the name is a matrix.

`⎕SEQNAMES`

Every track's name, a 16-row character matrix, row 1 for track 1 and
so on, padded with blanks to the longest name. With no names it is
16×0. All 16 tracks are listed, not just those in use, since a name
can be set before a track has anything to play.

`tracks ⎕SEQNOTES flags`

Sets whether tracks report their notes to `⎕EVWAIT ¯1` (`1`) or not
(`0`, the default). A wait on `¯1` then returns when a reporting track
starts a note, saying which note (see `⎕EVWAIT`). This is how a
program keeps in step with the music: with a melody or a drum track
itself, or with a track written just for that. Reporting is separate
from muting: a muted track that reports sends nothing to the synth but
still reports, so a silent track of regular notes is a clock (quarter
notes for the beat, eighths or triplets for finer steps), and one of a
few widely spaced notes gives cues, its pitches saying which cue. To
silence a track's sound and its reports, set both. A rest never
reports.

Turning a track's reports on starts it afresh (nothing it played
before counts); turning them off forgets anything it hadn't reported
yet. Like the other track settings, the setting stays through new
versions and `⎕SEQCLEAR` and goes at `⎕SEQCLOSE`. Returns the previous
settings. Same errors as `⎕SEQMUTE`.

```
1 4 ⎕SEQNOTES 1         ⍝ the melody and a cue track report
0 0
4 ⎕SEQMUTE 1            ⍝ the cue track is never heard
0
```

`⎕SEQTEMPO`

`⎕SEQTEMPO←bpm` or `⎕SEQTEMPO bpm`

Gets or sets the tempo in beats per minute, `1`–`1000`, fractions
allowed (default `120`). Takes effect at once and keeps every track in
place. Setting returns the *prior* value (see Settable functions at
the top). `¯1` sets the default, `120`: a file with no tempo of its
own shows `¯1` in `⎕MIDIINFO`'s tempo column, and players then assume
120, so `⎕SEQTEMPO←(⎕MIDIINFO 'song.mid')[⎕IO;⎕IO+4]` works for any
file. `Domain Error` for anything else outside `1`–`1000`. `Length Error` if `bpm`
isn't a single number. `Sequencer Not Open` when closed, even when
just reporting (`⎕SEQSTATE` never fails).

`⎕SEQPLAY`

`⎕SEQPLAY←n` or `⎕SEQPLAY n`

Starts and stops playback. `0` stops: a pause, turning off sounding
notes but keeping tracks, settings and position. `1` continues from
where it stopped. `2` restarts from beat 0: every track starts again
from its first note, play-once tracks that have finished included, and
any waiting versions take over at once. A stop and continue doesn't
replay a finished play-once track. On
the first start `1` and `2` are the same. Used alone, reports `1` if playing, `0` if not; setting returns the *prior*
value. The beat keeps counting while playing even if every track is
silent. `Domain Error` if `n` isn't `0`, `1` or `2`. `MIDI Port Lost`
if starting after the port was lost; open it again with `⎕SEQOPEN`
first. `Sequencer Not Open` when closed, even when just reporting.

`⎕SEQBEAT`

`⎕SEQBEAT←n` or `⎕SEQBEAT n`

Sets whether a wait on `¯1` also reports beats (`1`) or not (`0`).
**Off by default**, from every `⎕SEQOPEN` on: until Phase 7c, beats
were always reported, so a program written to wait for beats needs
`⎕SEQBEAT←1` after opening. Used alone, reports the setting; setting
returns the *prior* value (see Settable functions at the top). Kept
when the port is opened again after it was lost. `Domain Error` if `n`
isn't `0` or `1`. `Length Error` if it isn't a single number.
`Sequencer Not Open` when closed, even when just reporting.

`⎕SEQMETER`

`⎕SEQMETER←top bottom` or `⎕SEQMETER top bottom`

The time signature `⎕MIDIWRITE` writes, so a notation program shows
the music in bars of the right size: `⎕SEQMETER←3 4` for 3/4,
`⎕SEQMETER←6 8` for 6/8. The top number is `1`–`255`, the bottom
`1`, `2`, `4`, `8`, `16` or `32`. `¯1 ¯1`, the default from every
`⎕SEQOPEN` on, is none: no time signature is written, and programs
then assume 4/4. Used alone, reports the setting; setting returns the
*prior* value (see Settable functions at the top). Kept when the port
is opened again after it was lost; gone at `⎕SEQCLOSE`.

It is for files only. Playback has no bars: tracks loop at their own
lengths whatever the meter, and `⎕SEQTEMPO` still counts beats, which
the file takes as quarter notes. So in 6/8 a bar is 3 beats, and six
eighth notes at a dotted quarter of 60 a minute are steps of `0.5` at
`⎕SEQTEMPO←90`. A loop whose length isn't a whole number of bars ends
part-way through a bar in the file.

The note value the steps count in matters too. A file stores each step
as a fraction of a quarter note, so a pulse of `÷3` is written as
eighth-note triplets, and triplets don't fill bars such as 7/8 (3½
quarter notes): a notation program then pads each bar with rests and
plays it that way. Count such a pulse as a plain note value and change
the tempo to match, so it sounds the same: steps of `÷3` at
`⎕SEQTEMPO←140` are the same speed as sixteenths (`0.25`, or `6` at
`)SEQUNIT 24`) at `105`, three quarters of the tempo. Seven of them
then make a bar of 7/16, fourteen a bar of 7/8.

`⎕MIDIINFO`'s file row gives a file's time signature in columns 6 and
7, `¯1 ¯1` if it has none, so it passes straight across:

```
⎕SEQMETER←(⎕MIDIINFO 'song.mid')[⎕IO;⎕IO+5 6]
```

`Domain Error` for any other numbers, or text. `Length Error` if it
isn't two numbers. `Rank Error` for a matrix. `Sequencer Not Open`
when closed, even when just reporting.

`⎕SEQSTATE`

Reports everything about the sequencer as a matrix, never giving an
error. Column 1 says what each row is: `0` for the sequencer itself, a
track number for a track. The sequencer row comes first, then one row
per track in use (holding notes, a new version waiting, or a play-once
track that has finished), in track order. A version counts as waiting until the beat it takes over at has
been reached, so a track sent before the first start shows as waiting
(column 2 `0`, column 8 `1`) until playback starts. When closed, only
the sequencer row is there.

| Column | Sequencer row | Track row |
| --- | --- | --- |
| 1 | `0` | track number |
| 2 | state: `2` playing, `1` stopped, `0` closed, `¯1` port lost (greater than `0` means open) | notes in the playing version, or the finished one (`0` if only a waiting one) |
| 3 | current beat (in `)SEQUNIT`'s units, if set) | the track's channel (with only a waiting version, the one it will start on); notes with a channel of their own (row 5) aren't shown here |
| 4 | tempo | mute (`0`/`1`) |
| 5 | number of track rows | loop (`0`/`1`) |
| 6 | beats reported (`⎕SEQBEAT`, `0`/`1`) | velocity |
| 7 | how late the last notes were reported, in milliseconds (see `⎕EVWAIT`) | loop length in beats (or `)SEQUNIT`'s units) |
| 8 | `0` | new version waiting (`0`/`1`) |
| 9 | `0` | position within the playing version's current loop, in beats or `)SEQUNIT`'s units (`0` when finished) |
| 10 | `0` | notes reported (`⎕SEQNOTES`, `0`/`1`) |
| 11 | `0` | gate (`⎕SEQGATE`) |
| 12 | `0` | finished: `1` for a play-once track that has played through. It keeps its notes, silent, until a restart plays it again, a new version, `⎕SEQCLEAR` or `⎕SEQCLOSE`. Its loop length and notes are in columns 7 and 2; column 8 is `0` |
| 13 | `0` | program (`⎕SEQPROG`, `¯1` none): the setting, which applies from the track's next pass |
| 14 | `0` | bank (`⎕SEQBANK`, `¯1` none), likewise |

More columns and more kinds of row may be added later, so pick rows by
column 1 rather than by position (other than the sequencer row being
first):

```
S ← ⎕SEQSTATE
S[1;2]                ⍝ state
(S[;1]>0)/S[;1]       ⍝ tracks in use
3∊S[;1]               ⍝ is track 3 in use?
S[S[;1]⍳3;]           ⍝ track 3's row
```

`⎕EVWAIT list`

Waits for the next event from any source in `list` and returns what
happened. A source is a graphics window number (a left click in that
window) or `¯1`, the sequencer (notes from the tracks `⎕SEQNOTES` set
to report, and beats if `⎕SEQBEAT` is on). `⎕WCLICK` is the same
function under its graphics name: the two behave identically. The
result is numbers only, and different kinds of event give different
lengths. A click starts with the window number, which is never
negative; anything else starts with its source, then the kind of
event, then whatever that kind reports.

| Result | Meaning |
| --- | --- |
| `w x y` | a left click in window `w` at `x,y` |
| `¯1 1 B` | a beat: `B` is the beat number (only with `⎕SEQBEAT←1`) |
| `¯1 2 S` | the music stopped during the wait: `S` is the new state, as in `⎕SEQSTATE` (`1` stopped, `0` closed, `¯1` port lost) |
| `¯1 3 N t p i k …` | notes: `N` notes of four numbers each (see below) |
| empty | nothing in `list` could be waited on; a window waited on closed; Escape; Ctrl+C |

**Notes.** When reporting tracks start notes at the same moment (a
chord spread over several tracks), they come together in one result,
in track order, four numbers each: the track, the pitch, the note's
index (its column in the track's matrix, counting rests as columns,
in `⎕IO`, so `M[;i]` is the note) and a count, normally `1`. The
program decides which matter: `(E[⎕IO+2],4)⍴3↓E` makes them a matrix,
one row per note. The index starts again with each new version of a
track.

Notes are not lost while the program is busy. Each reporting track
keeps its latest note not yet reported, and the count says how many
notes the track started since the last one reported, that one
included. A wait with notes outstanding returns them all at once, in
one result (so their order is lost), however long ago they started.
A program that keeps up always sees `1`; one that falls behind gets
the latest note with a larger count and never falls further behind.
A count over `1` means the program wasn't waiting when those notes
started: it was busy, or handling a click, or not calling `⎕EVWAIT`
for a while. A count of `1` means nothing was missed, not that the
note was reported as it started: a wait called late, but before the
track's next note, returns at once with that note and a count of `1`.
How late it was shows in column 7 of `⎕SEQSTATE`'s first row: the
milliseconds from the start of the newest note in the last result to
the moment it was reported, at the current tempo. A wait that was
already waiting gives well under a millisecond (the system's own
delay in sending the note; about 0.2–0.5 on Linux); a value much
larger than that means the program came back late, and shows how
stale its notes were.
It stays until the next notes are reported, so a loop being tuned can
show it after each wait (`⎕←E,(⎕SEQSTATE)[1;7]`). Beats aren't
measured; `0` until the first notes after `⎕SEQOPEN`.
This holds even when the music has stopped: notes outstanding are
returned before the wait skips the sequencer. A restart
(`⎕SEQPLAY←2`) forgets them.

**Beats** are different: with `⎕SEQBEAT←1`, each call waits for the
next beat from now, and beats aren't saved up while APL is busy. The
beat number counts whole beats from `0` at `⎕SEQOPEN` and at a restart
(`⎕SEQPLAY←2`), and isn't reset when a track loops, so a jump shows
beats that went by while the program was busy (`1237` after `1234`
means two were missed), and `8|B` gives the place in an 8-beat pattern
started at beat 0. A beat that falls at the same moment as reported
notes isn't reported: the notes end that wait. Right after
`⎕SEQPLAY←1` the first beat returned is usually `1`: beat `0` is
already under way.

Events arrive the moment their notes go out. Clicks aren't saved up
either: a click with no wait waiting on its window is dropped.

Only sources that can produce something are waited on: windows that
are open and not hidden (a minimized window counts), and the sequencer
when it has notes outstanding, or while it's playing with beats on or
with a reporting track that has notes. If none of `list` qualifies,
the result is empty at once, so `⎕EVWAIT ¯1` never hangs when nothing
is playing or nothing reports. (A reporting track made only of rests
still counts, and a wait on it alone lasts until Escape or Ctrl+C.) A
window waited on closing ends the wait with an empty result, even if
other sources are still waited on; the next wait skips it. A stop is
reported as an event instead (`¯1 2 S`), and later waits skip the
sequencer until it plays again.

Once the music stops, every call on `¯1` alone returns at once, so a
loop must test the result. `2↑E` turns an empty result into `0 0`, so
one test covers both "stopped" and "nothing to wait for". A function
drawing a frame for each note of track 1, its pitch choosing the
picture:

```
L:E←⎕EVWAIT ¯1
→(∨/¯1 3≠2↑E)/0        ⍝ not notes: leave
N←(E[⎕IO+2],4)⍴3↓E     ⍝ one row per note: track, pitch, index, count
DRAW N[⎕IO;⎕IO+1]      ⍝ the first row's pitch
→L
```

Waiting on the sequencer and a window `W` together:

```
L:E←⎕EVWAIT ¯1,W
→(0=⍴E)/0              ⍝ Escape, W closed, or nothing to wait for
→(¯1≠E[⎕IO])/CLK       ⍝ a click in W
→(3≠E[⎕IO+1])/0        ⍝ the music stopped
STEP (E[⎕IO+2],4)⍴3↓E  ⍝ notes
→L
CLK:MARK E[⎕IO+1 2]    ⍝ the click's x,y
→L
```

A track sent just after an event that's not yet playing starts on the
next beat. A new version of a playing track still waits for that
track's loop to end; if the event returned was on the loop boundary,
that boundary has already passed and the version takes over one loop
later, so send variations during the loop rather than on its last
note.

Escape, pressed in the terminal or in a graphics window, ends a wait
with an empty result and the program carries on; notes that came
meanwhile are still outstanding for the next wait. With nothing
waiting, Escape does nothing; Ctrl+C is the only key that interrupts.
Ctrl+C during a wait ends it with an empty result within about a
twentieth of a second, the rest of the line still runs, and then the
interrupt is reported and the function suspended, as for any line:
after `X←⎕EVWAIT ¯1`, `X` is empty. Another wait later on the same
line returns empty at once. Closing the main window during a wait ends
it (`¯1 2 0` if the sequencer was waited on). `Length Error` if `list`
is empty, `Rank Error` if it's a matrix, `Domain Error` for text, a
fraction, or a negative number other than `¯1`.

```
1 ⎕SEQNOTES 1
0
⎕EVWAIT ¯1              ⍝ track 1 starts pitch 62, column 2
¯1 3 1 1 62 2 1
⎕SEQBEAT←1
⎕EVWAIT ¯1,W            ⍝ next event, or a click in W
3 120 45
```

---

## MIDI Files

`⎕MIDIREAD` and `⎕MIDIINFO` read Standard MIDI Files (`.mid`), such as
MuseScore and Cakewalk export, into arrays a program can send straight
to the sequencer. They don't need the sequencer open. `⎕MIDIWRITE`
writes the sequencer's tracks to a file, at the end of this section. A file name is
used as given if it's an absolute path (starting with `/`, or a drive
letter and `:`); anything else is looked for in the `midi` folder
under `$APLHOME`, as `⎕WILOAD` looks in `images`. No extension is
added.

The APL keyboard types capitals, and file names are usually in lower
case, so a name is tried exactly as given and then, if there's no such
file, again with its letters in lower case: `'SONG.MID'` finds
`song.mid`. For an absolute path only the file's own name is lowered,
not the folders. A file whose name mixes capitals and lower case (`My
Song.mid`) has to be named exactly, in quotes as it is on disk.
`)MIDI` lists the folder, with names spelled as they are on disk.

Tracks are numbered in the order the file has them, from 1, whatever
`⎕IO` is. A track can hold chords, and the sequencer's tracks play one
note at a time, so each track is split into **voices** that each play
one note at a time, numbered from 1: first by channel, then within a
channel each note goes to the lowest-numbered voice whose last note
has ended. A chord's notes go to voices 1, 2, 3 …, lowest note first,
and a note held while other notes move above it keeps its voice. A
track with one line of notes is voice 1. (A line played legato on a
keyboard, each note held a little past the next one's start, comes
out as two voices taking turns.)

A file written by `⎕MIDIWRITE` records each track's loop length, so
it comes back exactly as long as it went in. Any other file has no
loop length, so a track's length is taken from where the file says it
ends (or its last note, if that's later), moved up to the next point
on the track's own grid: the largest step, at most a beat, that all
its notes start on. MuseScore ends a track a moment after its last
note sounds; this puts the end back where the music ends. A loop from
elsewhere that ends in a rest not on its notes' grid (quarter notes on
beats 0, 1 and 2, then a rest to 3.5) comes back a little longer (4
beats); change its last step if that matters. The same goes for a
`⎕MIDIWRITE` file whose notes were changed in another program so they
run past the recorded length.

`'file' ⎕MIDIREAD track`

`'file' ⎕MIDIREAD track,voice`

One voice of one track of the file (voice 1 if none is given), as a
5-row matrix in `⎕SEQTRACK`'s form: pitches, steps (beats, or
`)SEQUNIT`'s units), velocities, lengths (likewise) and channels. Send
it as it is:

```
      1 ⎕SEQTRACK 'song.mid' ⎕MIDIREAD 2
```

A gap after a note is a length shorter than its step, not a rest. A
voice that starts after the beginning of the track starts with a rest
(pitch `¯1`, with `¯1` in rows 3–5). The steps add up to the track's
length, so all the voices of a track loop together. Steps and lengths
are the file's, not rounded to the sequencer's ticks (`⎕SEQTRACK` does
that). Each note's channel is the one in the file (row 5), so it plays
there whatever the track's own channel is.

`Domain Error` for a track or voice the file doesn't have (`⎕MIDIINFO`
says which it has; a track with no notes has no voices), `File Not
Found` if there's no such file, `Bad MIDI File` if it isn't a MIDI file
this can read (damaged, or SMPTE timing, which is rare).

`⎕MIDIINFO 'file'`

What the file holds, for deciding what to read: a matrix of 9 columns,
one row for the file and then one for each voice of each track. As in
`⎕SEQSTATE`, the first column says what a row is: `0` for the file,
otherwise the track number. Columns are numbered here from 1; index
them with `⎕IO` in mind.

The file row, first:

| Column | What it holds |
|---|---|
| 1 | `0` (the file row) |
| 2 | the file's format: `0` (everything in one track), `1` (several tracks, the usual), `2` (rare) |
| 3 | how many tracks the file has |
| 4 | ticks per beat (the file's time resolution) |
| 5 | the tempo in beats per minute, from the first tempo in the file; `¯1` if it has none (players then assume 120) |
| 6, 7 | the time signature's top and bottom numbers (`3 4` for 3/4); `¯1 ¯1` if none |
| 8, 9 | `0` |

Then a row for each voice of each track, tracks in file order and
voices in order within each:

| Column | What it holds |
|---|---|
| 1 | the track number, from 1 |
| 2 | the voice number within the track, from 1; `0` for a track with no notes |
| 3 | how many notes the voice has (rests not counted) |
| 4 | the track's length in beats, or `)SEQUNIT`'s units (the same on every voice row of a track): what the voice's steps add up to, so its voices loop together |
| 5 | the channel the voice plays on, `1`–`16` (a voice is always one channel); `¯1` for a track with no notes |
| 6, 7 | the voice's lowest and highest pitch; `¯1` for a track with no notes |
| 8 | the program (instrument, `0`–`127` as sent) on that channel when the voice starts; `¯1` if the file sets none |
| 9 | the bank on that channel when the voice starts, `0`–`16383`: 128 times bank select controller 0 plus controller 32, a missing one counting as `0`; `¯1` if the file sets neither (most files) |

A track with no notes (one holding only the tempo, say) still has a
row, so every track shows: voice `0`, no notes, its length, and `¯1` in
the last five columns.

```
      ⎕MIDIINFO 't5000m3.mid'
    0    1    2  480  120    4    4    0    0
    1    1  140   35    2   41   71    0   ¯1
    2    1  140   35    3   29   47    0   ¯1
```

Here the file is format 1 with 2 tracks, 480 ticks per beat, 120 beats
per minute, in 4/4. Each track is one voice of 140 notes, 35 beats
long; track 1 is on channel 2, pitches 41 to 71, and track 2 on
channel 3, 29 to 47; both start with program 0, and neither sets a
bank. (Until Oct 18, 2026 the matrix had 8 columns, without the bank.)

**Voices.** A track can hold chords, and each sequencer track plays one
note at a time, so a track is split into voices (see above). How many
voices a track has is how many voice rows it has: the most notes it
ever sounds at once on a channel, summed over its channels (a line
played legato, each note held a little past the next, counts as two).
For track `T`, with `I←⎕MIDIINFO 'song.mid'`:

```
+/(I[;1]=T)∧I[;2]>0                ⍝ how many voices track T has
+/(⍳I[1;3])∘.=(I[;2]>0)/I[;1]      ⍝ the same for every track (⎕IO←1)
```

A voice with only a few notes (a single note where a chord briefly has
one more note than usual) may not be worth sending to a track of its
own.

If there's no such file the result has no rows (shape `0 9`) rather
than an error, so `0=1↑⍴⎕MIDIINFO 'song.mid'` asks whether a file is
there. `Bad MIDI File` if it's there but can't be read as one.

To pick out the voices worth reading:

```
I←1 0↓⎕MIDIINFO 'song.mid'      ⍝ the track and voice rows
I←(I[;2]>0)⌿I                   ⍝ without the tracks that have no notes
```

Row `K` of `I` is then sent with `K ⎕SEQTRACK 'song.mid' ⎕MIDIREAD
I[K;1 2]`, as long as there are no more than 16 rows (the sequencer's
tracks).

`'file' ⎕MIDIWRITE tracks`

`'file' ⎕MIDIWRITE ¯1`

Writes sequencer tracks to a MIDI file, for MuseScore, Cakewalk and the
like, or to read back later with `⎕MIDIREAD`. `¯1` writes every track
that has something; otherwise list the tracks. The sequencer must be
open (`Sequencer Not Open` if it isn't); it doesn't need to be playing.

Each track goes in as it would sound now: one pass of the version
playing (or, if nothing is playing on the track yet, the one waiting
to start; or a play-once track's that has finished), with each note's velocity scaled by the track's `⎕SEQVEL`,
its length from row 4 of its matrix or the track's `⎕SEQGATE`, and its
own channel or the track's. Rests are silence. A muted track is
written like any other. To write a longer arrangement than one pass,
send the track a longer matrix first (`M,M,M,M` repeats it).

Sequencer track 3 is the file's track 3: tracks below the highest one
written that weren't asked for, or have nothing, are written empty, so
`⎕MIDIREAD` finds each track under its own number. The tempo,
`⎕SEQTEMPO`'s, goes at the start of track 1, with the time signature,
`⎕SEQMETER`'s (none if it is `¯1 ¯1`: programs then assume 4/4), and
the key signature C major (the file has no key of its own; C major is
what other programs write for music with no key, and stops them
guessing one). Each track listed that has a name (`⎕SEQNAME`) gets it
as the track's name. Each
track's bank and program (`⎕SEQBANK`, `⎕SEQPROG`) go at its start, on
its channel: the ones heard with the version written, so a program
set while the track plays, still waiting for the end of the pass, isn't
the one written yet; for a track waiting to start, the ones it will
start with. A track with no program gets none in the file. Each track ends at its
loop length, which is also recorded exactly for `⎕MIDIREAD` (in a
"sequencer-specific" event that other programs skip). The file keeps
the sequencer's 2520 ticks per beat, so the timing is exact, triplets
included. Not written: tempo changes made while playing.

The file is written with its name in lower case, so `'SONG.MID'`
writes `song.mid`, the way other programs expect (MuseScore's file
selector on Linux doesn't show `.MID` files), and `⎕MIDIREAD 'SONG.MID'`
finds it again. An absolute path is used as given except for the
file's own name; anything else goes in the `midi` folder under
`$APLHOME`. An existing file is replaced, as `⎕FCREATE` does;
`0=1↑⍴⎕MIDIINFO 'song.mid'` tells you first whether there is one.

```
      'song.mid' ⎕MIDIWRITE ¯1
1 2 3
```

Returns the tracks it wrote from the sequencer (not the empty ones put
in to fill a gap). `Domain Error` for a track outside 1–16 (`¯1` only
on its own), `Length Error` for an empty list, `Can't open file` if
the file can't be written (no such folder, no permission).

---

## Sequencer Test Functions

`⎕SEQRUNTO beat` and `⎕SEQLOG` exist in every build but work only with
the sequencer's test backend, which runs on a simulated clock and
writes to a log instead of a device. The regression tests turn it on
with `apl_cli -S`: every `⎕SEQOPEN` then opens the test backend,
whatever port it names (a device name too: nothing is looked up), and
`⎕SEQPORTS` returns a fixed list of three test ports instead of the
machine's, so a test reads the same wherever it runs. With a real port
open, `⎕SEQRUNTO` and `⎕SEQLOG` give `Domain Error`.

`⎕SEQRUNTO` instantly plays everything before `beat` and holds there,
so a version sent while held exactly on a loop boundary takes over at
that boundary. Returns the beat it holds at (to the nearest 1/2520).
Under `)SEQUNIT`, `beat` and the result are in its units.
A beat at or before the current one does nothing. `Domain Error` if
the sequencer isn't playing (it would never get there) or `beat` is
negative.

`⎕SEQLOG` returns the log since the last `⎕SEQLOG` (or since the open)
as a character matrix, one line per MIDI message, and empties it, so
each call shows only what's new:
beat, exact tick, simulated time since open, message
(`8.000 t20160 4000.000ms ON  ch1 60 v100`). The log is the
sequencer's own, so it counts beats and ticks whatever `)SEQUNIT` is. Nothing new gives a 0×0
matrix. It still works after `⎕SEQCLOSE`, so the closing note-offs
can be read.

On the test backend, a wait with `¯1` in its list (`⎕EVWAIT` or
`⎕WCLICK`) first takes any events posted with `⎕EVPOST` (below); if
none ends it, it plays at once up to and including the wait's event
(the next reported note, or beat) and returns it, holding just past
it, so a wait never takes real time and gives the same result on every
run. Right after `⎕SEQPLAY←1` it returns what happens at beat `0`,
since the simulated clock hasn't moved. If nothing comes within 10000
beats (a reporting track made only of rests, say), the wait gives
`Domain Error` instead of waiting forever.

`⎕EVPOST kind,values` feeds window events to `⎕WCLICK`/`⎕EVWAIT` in `apl_cli`,
the command-line front end the regression tests use, which has no real
windows to click (its windows are a table that remembers their state
but draws nothing). Each event goes to the waits that come after it,
in order: a wait takes posted events until one ends it, and leaves the
rest for the next wait. The first number says which event:

| `kind,values` | Event |
| --- | --- |
| `1,w,x,y` | a left click in window `w` at `x,y` |
| `2,w` | window `w` closed by the user |
| `3` | Escape |
| `4` | Ctrl+C |

Returns how many events are now waiting. A wait with nothing posted to
end it, and nothing from the sequencer to wait for, gives `Domain
Error` instead of waiting forever. In
the GUI `⎕EVPOST` always gives `Domain Error`; so does an unknown kind,
a fraction, or a full queue (64 events). `Length Error` for the wrong
number of values, `Rank Error` for a matrix.
