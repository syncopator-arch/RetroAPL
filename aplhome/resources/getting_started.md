<!-- font: Georgia, DejaVu Serif, serif -->
# Getting Started with RetroAPL

A short introduction for people new to RetroAPL. The other pages on the
Help menu have the full details: the System Command Reference, the
Primitive Function Reference and the Quad Function Reference.

System commands are typed on their own line at the prompt, starting
with a right parenthesis, such as `)LIB` or `)SAVE`.

RetroAPL keeps your work in two kinds of files in a storage folder
under $APLHOME (Linux) or %APLHOME% (Windows):

- A **workspace** (`$APLHOME/ws/name.ws`) holds everything in the
  active session: variables, compiled functions and settings. You save
  it with `)SAVE` and bring it back with `)LOAD`.
- A **function source file** (`$APLHOME/fns/name.fn`) holds the text
  of one function in an internal form. You write it with the editor 
  (`)EDIT`) and compile it into the active workspace with `)ENTER`.
  You list it in readable form with )LIST.

---

## Classic System Commands

These work much as they do in APL\360 and APL2.

| What it does | Command |
|---|---|
| Start a new, empty workspace | `)CLEAR` |
| Replace the active workspace with a saved one | `)LOAD name` |
| Save the active workspace under its current name | `)SAVE` |
| Save it under a new name | `)SAVE name` |
| Show the active workspace's name | `)WSID` |
| Change the active workspace's name | `)WSID name` |
| List the saved workspaces | `)LIB` |
| Delete a saved workspace (it asks first) | `)DROP name` |
| List the functions in the active workspace | `)FNS` |
| List the variables in the active workspace | `)VARS` |
| Remove a function or variable from the active workspace | `)ERASE name` |
| Show the functions that are running or suspended | `)SI` |
| End the session (it doesn't save first) | `)OFF` |

Notes: 
- )LOAD of a new workspace does not save the existing workspace first.

## Classic Commands NOT in RetroAPL

These are in APL\360 or APL2 but not in RetroAPL.

| What it does elsewhere | Command |
|---|---|
| Copy objects from a saved workspace | `)COPY`, `)PCOPY` |
| Workspace groups | `)GROUP`, `)GRP`, `)GRPS` |
| Save and sign off in one step | `)CONTINUE` |
| Messages to other users or the operator | `)MSG`, `)OPR` |
| List the users signed on | `)PORTS` |
| Transfer files | `)IN`, `)OUT`, `)PIN` |
| Clear the state indicator | `)SIC`, `)RESET` |

Notes:
- )COPY of functions is less necessary due to the function source files 
  existence separate from the workspaces.

Command names are matched by their leading letters, so a few of these
run a different RetroAPL command instead of giving an error: `)SIC`
runs `)SI`, and `)OUT` runs `)OUTSYNC`. To clear the state indicator in
RetroAPL, use `)PURGE`.

## System Commands Unique to RetroAPL

These are new in RetroAPL, or work differently from other APLs.

| What it does | Command |
|---|---|
| Open a function's source file in the editor | `)EDIT name` |
| Compile a function source file into the active workspace | `)ENTER name` |
| Show a function source file with line numbers | `)LIST name` |
| List the function source files | `)FNSL` |
| Search the function source files | `)FSEARCH text` |
| Delete a function source file (it asks first) | `)DROP name` |
| Clear the state indicator | `)PURGE` |
| Debug functions | `)STOP`, `)STEP`, `)TRACE` |

Notes:
- Not a complete list, see the System Command Reference for more.

