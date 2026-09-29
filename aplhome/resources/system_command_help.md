<!-- font: Georgia, DejaVu Serif, serif -->
# System Command Reference

Calling shape and behavior for each `)` system command, for quick lookup
while you're working. Commands are typed on their own line, starting
with a right parenthesis.

Command names are matched by their leading letters: any word that
begins with a command's short form is accepted, and extra letters are
ignored. The short form is given with each entry — `)CL` is enough for
`)CLEAR`, for example. That also means a mistyped word can still run a
command: `)CLOSE` is taken as `)CLEAR`. Commands with no short form
listed have to be typed in full.

System commands are meant to be typed at the prompt. Putting one on a
line inside a function isn't supported yet — some do nothing there,
and some could damage the workspace.

Workspaces live in `$APLHOME/ws/` as `name.ws`, and function source
files in `$APLHOME/fns/` as `name.fn`.

`)LIB`, `)FNSL`, `)FNS`, `)VARS`, `)SYMBOLS` and `)FONT` can take a name
pattern to narrow what they list. A pattern isn't a regular expression. Plain
letters match the start of a name: `)FNS G` lists functions starting
with G. `*` matches any run of characters: `)FNS *TEST` lists functions
with TEST anywhere in the name. Two letters joined by a dash match any
one letter in that range: `)LIB A-D` lists workspaces starting with A
through D. Case is ignored, and only the first word after the command
is used.

---

## Workspace

`)CLEAR`

`)CLEAR size`

Discards everything in the active workspace and starts a new, empty
one named `Clear WS`, then displays that name. Index origin goes back
to `1`, `)INTDIV` to `OFF`, and any boot function is removed; the
current `)WIDTH` and `)DIGITS` settings carry over. With `size`, the
new workspace is also resized to that many bytes. Short form `)CL`.

`)LOAD`

`)LOAD name`

Replaces the active workspace with the saved workspace `name`, then
displays when it was saved. With no name, loads `continue`. The
workspace comes back at the size it was saved at, along with its own
index origin, `)WIDTH`, `)DIGITS`, `)INTDIV` and boot function
setting. Loading doesn't run the boot function — that only
happens when the interpreter starts up. If the file isn't found, is
damaged, or won't fit, the active workspace is left as it was — except
that a file found to be cut short partway through loading leaves a
clear workspace instead.

`)SAVE`

`)SAVE name`

Writes the active workspace to disk under its current name (see
`)WSID`), then displays the time it was saved. With a name, the
workspace is renamed to `name` first and saved under that. A workspace
still named `Clear WS` is saved as `continue`. Fails with an SI error
if any function is running or suspended — clear the state indicator
first (see `)SI` and `)PURGE`). There's no "save changes?" prompt
anywhere else, so save before `)OFF` if you want to keep your work.
Note however, that function source files are stored separately, so
an `)OFF` without `)SAVE` won't lose those, just any updates that may
have been compiled into the workspace with `)ENTER`.  A simple `)ENTER`
on the function name will compile it into a current workspace.

`)WSID`

`)WSID name`

With no name, displays the active workspace's name. With a name,
renames the workspace (without saving it) and displays the old name.
Short form `)WS`.

`)LIB`

`)LIB pattern`

Lists the saved workspaces in `$APLHOME/ws/`, optionally only those
whose names match `pattern`.

`)RESIZE size`

Changes the active workspace to `size` bytes, keeping its contents.
Fails with an SI error if any function is running or suspended, and
with a WS Full error if `size` wouldn't leave room for what's already
in the workspace. Displays the workspace name when done. Short form
`)RES`.

`)BOOT`

`)BOOT fname`

Sets the function that runs automatically when this workspace is
loaded at interpreter startup, and displays the previous setting.
`)BOOT` on its own removes the boot function. The setting is saved
with the workspace. Short form `)BO`.

`)OFF`

Ends the session and closes the interpreter. Nothing is saved.

---

## Functions and Variables

`)FNS`

`)FNS pattern`

Lists the functions in the active workspace in alphabetical order,
optionally only those whose names match `pattern`.

`)VARS`

`)VARS pattern`

Lists the variables in the active workspace in alphabetical order,
optionally only those whose names match `pattern`. Short form `)VAR`.

`)ERASE name name ...`

Removes the named functions and variables from the active workspace.
Fails with an SI error if a name belongs to a function that's running
or suspended. Short form `)ERA`.

`)ENTER fname fname ...`

Reads each function from its source file `$APLHOME/fns/fname.fn` (the
file name is always lower case) and defines it in the active
workspace, replacing any existing function or variable with the same
name. If a file isn't found, its full path is displayed along with the
error. Fails with an SI error if the function is currently running or
suspended. Short form `)ENT`.

`)EDIT fname`

Opens the function's source in the editor. A name is required. Short
form `)ED`.

`)LIST fname`

`)LIST fname n`

`)LIST fname n m`

Displays the source file `$APLHOME/fns/fname.fn` with line numbers,
header line first as line 0. With one number, displays just that line;
with two, lines `n` through `m`. A range that runs backwards is a
Syntax Error; a line past the end displays a line such as
`No line 9 (lines are 0 to 5)` and gives a Value Error. The listing
comes from the file, so its line numbers match the function in the
workspace only if the file hasn't changed since it was entered. A name
containing `/` is used as a file path instead. Short form `)LI`.

`)FNSL`

`)FNSL pattern`

Lists the function source files in `$APLHOME/fns/`, optionally only
those whose names match `pattern`.

`)DROP fname`

Deletes the function source file `$APLHOME/fns/fname.fn` and reports
whether it was dropped or not found. This deletes a function file, not
a workspace — different from `)DROP` in most other APLs. Functions
already defined in the active workspace aren't affected.

**A limit on very long lines.** A function can't be called, and `⍎` or
`⎕` input can't be used, with more than 254 bytes of compiled code still
to run to its left on the same line — for example after two long string
constants joined to it with `,`. The call is refused with a line such as
`ID: called with 323 compiled bytes left on the line (limit 254)` and a
Length Error; splitting the line in two avoids it.

---

## Settings

`)ORIGIN`

`)ORIGIN n`

Displays or sets the index origin (the same setting as `⎕IO`). Setting
it displays the previous value. Use `0` or `1`: unlike `⎕IO`, this
command doesn't reject other values. Saved with the workspace. Short
form `)OR`.

`)WIDTH`

`)WIDTH n`

Displays or sets the output line width (the same setting as `⎕WI`).
Setting it displays the previous value; widths below 20 are raised to
20. Saved with the workspace. Short form `)WI`.

`)DIGITS`

`)DIGITS n`

Displays or sets print precision (the same setting as `⎕DIG`). Setting
it displays the previous value; `n` is limited to `¯17…17`. Positive
means exactly that many decimal places, trailing zeros kept; negative
means up to that many decimal places, trailing zeros trimmed; `0` is
valid either way. Saved with the workspace. Short form `)DIG`.

`)INTDIV`

`)INTDIV ON`

`)INTDIV OFF`

Displays or sets whether dyadic `÷` does integer division (the same
setting as `⎕INTDIV`). `ON`: when both arguments are integers the
result is truncated toward zero, so `7÷2` is `3`. `OFF`: `÷` always
returns a float, even for exact quotients, which is the usual APL
behavior. Setting it displays the previous value; any other argument
is a syntax error. Saved with the workspace. New and cleared
workspaces start `OFF`; workspaces saved before this setting existed
load as `ON`.

`)BASE`

`)BASE hex`

`)BASE dec`

Displays or sets whether numbers are displayed in hexadecimal or
decimal. Setting it displays the previous base; any argument other
than `hex` selects decimal. Short form `)BA`.

---

## State Indicator

`)SI`

Lists the functions that are running or suspended, innermost first,
each with the line it stopped on.

`)SIV`

Like `)SI`, but also lists each function's local names and the names
of its arguments and result.

`)PURGE`

Clears the state indicator, abandoning every running or suspended
function. Short form `)PUR`.

---

## Debugging

A running function can be stopped partway through and examined.
`)STOP` marks lines to stop before, `)STEP` makes `→0` continue one
line or one operation at a time, and `)TRACE` displays marked lines'
results as they run. While a function is stopped, its variables, including its
locals and arguments, can be displayed or changed at the prompt, and
`)SI` shows where it stopped. `→0` continues from the stopping point —
except after an error, when it reruns the failed line from its start,
so it only gets past the error once its cause has been fixed (anything
the line did before failing happens again). `→N` resumes it at the
start of line `N` instead — to skip a line, or go back to an earlier
one; a `)STOP` on line `N` itself doesn't stop it again, and a line
past the end leaves the function, returning to whatever called it.
`→` on its own abandons the stopped function along with everything
else in the state indicator. `)LIST fname n` shows the line a function
stopped on. Stops happen only in functions run from a typed line — in
code run by `⍎` or by `⎕` input they're skipped, and an error or
interrupt there stops the whole `⍎` instead, after a line such as
`In execute: FN [3]` saying where it happened.

`)TRACE`

`)TRACE fname`

`)TRACE fname n n …`

`)TRACE fname OFF`

`)TRACE OFF`

Displays the result of each marked line as it runs, as
`fname [n] value`, including the value of a line that assigns.
`)TRACE fname 1 3` marks lines 1 and 3 of `fname`, or unmarks any
already marked. With just a function name, shows which of its lines are
marked; `fname OFF` unmarks all of them. With no argument, lists every
function that has marked lines; `)TRACE OFF` unmarks every line in the
workspace. Every form ends by displaying the marks as `fname[1 3]` (or
`None`), starting with `Now:` when the command changed something. A
line number the function doesn't have displays a line such as
`FN has no line 4 (it has 3 lines)` and gives a Value Error, as does a
name that isn't a function; any other bad argument is a Syntax Error.
Nothing is changed when there's an error. `OFF` is always taken as the
keyword, so a function named `OFF` can't be traced. Short form `)TR`.

`)STOP`

`)STOP fname`

`)STOP fname n n …`

`)STOP fname OFF`

`)STOP OFF`

Marks lines to stop before. When a marked line is about to run, the
function stops and `fname [n] *` is displayed; nothing on that line
has run yet. `→0` continues, and the line isn't stopped again on
continuing — but it is the next time it starts, including after a
branch back to it. The forms, display and errors are the same as
`)TRACE`, and the two sets of marks are separate: a line can be both
traced and stopped. Both kinds of mark are saved with the workspace;
entering a function again with `)ENTER` clears its marks.

`)STEP`

`)STEP LINE`

`)STEP OP`

`)STEP OFF`

Displays or sets how `→0` continues a stopped function. With `OFF`
it runs on to the next `)STOP` line. With `LINE` it stops again at the
start of the next line, including lines of functions it calls, so each
`→0` moves one line. With `OP` it stops after every operation,
displaying what just ran, such as `RECURSE [3] push var: N`. These
describe the compiled line, which runs right to left: a constant
appears as `push byte`, `push short` and so on, and `exec fn:` is a
function call. There's no stop directly after a branch — the next
line's first operation stops instead. Stepping only applies to a
function continued with `→0`: a function run from a typed line runs
normally until it reaches a `)STOP` line, so to step from the top,
mark its first line with `)STOP`. Setting it displays the previous
setting; any other argument is a Syntax Error. Stepping isn't saved
with the workspace, and starts `OFF`.

`)LEFT`

`)RIGHT`

While a function is stopped, display the operands waiting on the
stopped line: `)LEFT` the value computed most recently, `)RIGHT` the
one before it — the left and right operands of the next operation.
With `)STEP OP`, stopping after `push var: N` in `N-1` gives `N`'s
value for `)LEFT` and `1` for `)RIGHT`. At the start of a line, before
anything on it has run, there's nothing to display, and a message says
so. After an error, they show the operands of the operation that
failed — for `3÷0`, `3` and `0`. Short forms `)LE` and `)RI`.

---

## System

`)SYS`

`)SYS command`

With a command, starts it as a separate operating-system program and
returns immediately without waiting for it to finish, the same as
`⎕EXEC`; its output doesn't appear in the APL session. With no command, lists the names of all
`⎕` functions.

`)VERSION`

Displays the interpreter version, followed by the names of all system
commands and `⎕` functions. Short form `)VER`.

`)FONT`

`)FONT pattern`

Lists the font families available to `⎕WFONT`, sorted and spread
across the current width, spelled the way the system spells them — so
a name can be copied straight into a string for `⎕WFONT`. With a
pattern, lists only matching names. A typical system has a great many
fonts, most of them meant for other languages, so a pattern is usually
the quickest way in: `)FONT APL`, `)FONT DEJ`, `)FONT *MONO`. Only the
first word is used, so a name containing a space is matched with `*`:
`)FONT DEJAVU*MONO`. The pattern matches leading letters, but
`⎕WFONT` needs the full name. Only works with graphics windows,
the main text APL development window is not affected.

---

## Diagnostics

These are mainly for investigating the interpreter itself rather than
for everyday use.

`)SYMBOLS`

`)SYMBOLS pattern`

Lists every name in the symbol table, used or not, with its internal
offset. Short form `)SYM`.

`)MEM`

Displays the free space in the workspace, then one line per internal
memory entry with its type and first bytes.

`)STACK`

Displays the raw contents of the interpreter's execution stack. Short
form `)STA`.

`)DUMP`

Writes a full internal dump of the workspace to `debug.log` in
`$APLHOME`, and displays that file's path.

`)TEXT`

Writes the names of all functions and variables to `text_dump.txt`
in the current directory, each function name prefixed with `)enter`.

`)GARB`

`)GARB on`

`)GARB off`

With no argument, compacts workspace memory and displays how many
bytes were recovered. `on` or `off` sets whether memory is compacted
on every allocation, which is slow; setting it displays the previous
state.

`)DEBUG`

Toggles debug mode, which displays extra internal detail such as each
line's compiled form. Displays the previous state.
