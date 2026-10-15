⍝ ⎕WCLICK in apl_cli (Phase 7a): the fake window table (cli_graphics.c),
⍝ events posted with ⎕EVPOST and delivered to the waits to come. Since
⍝ 7b ⎕WCLICK is the same wait as ⎕EVWAIT (evwait.c); waits mixing
⍝ windows and the sequencer are tested in evwait (-S).
⍝ The cases of wclick_before_results.txt (V89) that don't need a mouse.
A←'A' ⎕WOPEN 0 0 200 150
B←'B' ⎕WOPEN 0 0 200 150
A,B
⍝ arguments
⎕WCLICK ⍳0
⎕EVPOST 1,A,5 6
⎕WCLICK A
⍝ since 7b, ⎕EVWAIT's rules: a matrix is a Rank Error (was used whole),
⍝ a fraction, text or a negative number other than ¯1 a Domain Error
⎕WCLICK 2 2⍴A,B,A,B
⎕WCLICK 0 2⍴0
⎕WCLICK 2 0⍴0
⎕WCLICK A+0.5
⎕WCLICK 'A'
⎕WCLICK ¯2
Q←⎕EVPOST 1,B,7 8
⎕WCLICK A,A,B,B
⍝ only open windows that aren't hidden are waited on; none: empty at once
⍴⎕WCLICK 99
⍝ (¯1 is the sequencer since 7b; closed here, so skipped too)
⍴⎕WCLICK 0 ¯1
Q←⎕EVPOST 1,A,1 2
⎕WCLICK 99,A
Z←B ⎕WCTRL 5
⍴⎕WCLICK B
Q←⎕EVPOST 1,B,1 1
Q←⎕EVPOST 1,A,3 4
⎕WCLICK A,B
Z←B ⎕WCTRL 6
Z←B ⎕WCTRL 0
⎕WVIS B
Q←⎕EVPOST 1,B,9 9
⎕WCLICK B
Z←B ⎕WCTRL 1
⍝ a click in a window not waited on is ignored
Q←⎕EVPOST 1,B,1 1
Q←⎕EVPOST 1,A,2 2
⎕WCLICK A
⍝ Escape: empty, no interrupt
Q←⎕EVPOST 3
⍴⎕WCLICK A
⍝ Ctrl-C: empty, the line finishes (X is assigned), then the interrupt
X←99
Q←⎕EVPOST 4
X←⎕WCLICK A
⍴X
⍝ Ctrl-C already pending: the second wait on the line returns empty at
⍝ once and leaves the posted click for the next wait. (A result left on
⍝ an interrupted line isn't displayed, so it's assigned and shown next.)
Q←⎕EVPOST 4
Q←⎕EVPOST 1,A,60 60
Y←(⍴⎕WCLICK A),⍴⎕WCLICK A
Y
⎕WCLICK A
⍝ a window waited on closing ends the wait, even with another still open
Q←⎕EVPOST 2,B
⍴⎕WCLICK A,B
⎕WNUMS
⍝ a window not waited on closing doesn't
B←'B' ⎕WOPEN 0 0 200 150
Q←⎕EVPOST 2,B
Q←⎕EVPOST 1,A,70 70
⎕WCLICK A
⎕WNUMS
Q←⎕EVPOST 2,A
⍴⎕WCLICK A
⎕WNUMS
⍝ nothing posted: Domain Error, not a hang
A←'A' ⎕WOPEN 0 0 200 150
⎕WCLICK A
⍝ ⎕EVPOST's own errors, and its result (how many are queued)
⎕EVPOST ⍳0
⎕EVPOST 5
⎕EVPOST 1,A,5
⎕EVPOST 3 1
⎕EVPOST 2 2⍴2,A,2,A
⎕EVPOST 1.5
⎕EVPOST 3
⎕EVPOST 3
