# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) Keith Doyle

# crash_inject.gdb -- manual check for the crash log (V70).
#
# Damages one workspace entry's size so the next garbage collection
# finds it, exactly as real corruption would -- no test code in the
# interpreter. Works on apl_cli or the Qt app.
#
#   gdb -x crash_inject.gdb --args ./apl_cli
#   gdb -x crash_inject.gdb --args ./<qt app>
#
# 1. `run`, then use APL for a bit so the transcript has something in it
#    (define a function, call it, get a suspension if you want an SI).
# 2. Ctrl+C in the gdb terminal to stop the program, then type one of:
#       inject_zero   -- first pool entry's size set to 0
#       inject_big    -- first pool entry's size set past the watermark
#       inject_badref -- first pool entry's owner reference set outside
#                        the workspace: a warning, and APL carries on
#    (gdb continues the program by itself afterwards.)
# 3. Enter any line that allocates, e.g.  X←⍳10
#
# New build: "FATAL INTERNAL ERROR -- APL must exit", exit status 70
# (gdb: "exited with code 0106" -- octal), and a new entry at the end of
# $APLHOME/crash.log. Start APL again: apl_cli prints one line about it
# on stderr; the Qt app shows the notice, and Acknowledge appends the
# "*** acknowledged" line.
#
# inject_badref instead: the "bad reference" message plus "Details
# written to ...", APL keeps running, and crash.log gets an
# "=== APL internal warning" entry; the next start shows the warning
# notice (apl_cli: "reported an internal warning"). Once per session.
#
# Old build (before V70), the test before the fix: inject_zero makes the
# collector loop forever (Ctrl+C, `bt` shows GarbCollect); inject_big goes
# unnoticed.

set pagination off
handle SIGINT stop print nopass

define inject_zero
  set var $me = (struct MemEnt *)((char *)wspace + wspace->begin)
  printf "inject_zero: entry off=%08x size %d -> 0\n", wspace->begin, $me->me_size
  set var $me->me_size = 0
  continue
end

define inject_big
  set var $me = (struct MemEnt *)((char *)wspace + wspace->begin)
  printf "inject_big: entry off=%08x size %d -> %d\n", wspace->begin, $me->me_size, wspace->water - wspace->begin + 64
  set var $me->me_size = wspace->water - wspace->begin + 64
  continue
end

define inject_badref
  set var $me = (struct MemEnt *)((char *)wspace + wspace->begin)
  printf "inject_badref: entry off=%08x reference %08x -> %08x\n", wspace->begin, $me->me_reference, wspace->wssize + 100
  set var $me->me_reference = wspace->wssize + 100
  continue
end
