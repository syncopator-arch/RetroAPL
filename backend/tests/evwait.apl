⍝ ⎕EVWAIT ¯1 (Phase 5, V88): beat waits on the virtual clock (-S).
⍝ Note events from tracks (Phase 7c) are tested in seq_notes.
⍝ Since 7b (one wait for ⎕EVWAIT and ⎕WCLICK) also waits mixing windows
⍝ and the sequencer, with window events posted by ⎕EVPOST: in each wait
⍝ the posted events come first, then the clock runs to the next beat.
⍝ Closed: empty at once.
⍴⎕EVWAIT ¯1
⍝ Argument errors: empty list, other negative numbers, a fraction, text, a matrix.
⎕EVWAIT ⍳0
⎕EVWAIT ¯2
⎕EVWAIT ¯0.5
⎕EVWAIT 'A'
⎕EVWAIT 1 1⍴¯1
⍝ A window number is legal since 7b: one that isn't open is skipped,
⍝ so with the sequencer closed these are empty at once.
⍴⎕EVWAIT 1
⍴⎕EVWAIT ¯1 0
⎕SEQOPEN 'hw:3,0,0'
⍝ Phase 7c: beat events are off by default; this test waits for beats.
⎕SEQBEAT←1
⍝ Open but stopped: empty.
⍴⎕EVWAIT ¯1
1 ⎕SEQTRACK 2 4⍴60 62 64 65,1 1 1 1
⎕SEQPLAY←1
⍝ ¯1 1 beat: beats 0, 1, 2 (a vector and a float ¯1 work too).
⎕EVWAIT ¯1
⎕EVWAIT ¯1 ¯1
⎕EVWAIT ¯1.0
⎕SEQLOG
⍝ Missed beats show as a jump: busy until 5.5, so the next is 6.
⎕SEQRUNTO 5.5
⎕EVWAIT ¯1
⍝ Beat 8 is track 1's loop boundary. A new version sent after it waits for 12;
⍝ a fresh track sent after it starts on beat 9.
⎕SEQRUNTO 7.5
⎕EVWAIT ¯1
1 ⎕SEQTRACK 2 4⍴67 65 64 62,1 1 1 1
2 ⎕SEQTRACK 2 1⍴48 4
⎕SEQRUNTO 13
⎕SEQLOG
⍝ In an expression, and the loop's test (0: a beat, carry on).
E←⎕EVWAIT ¯1
E
∨/¯1 1≠2↑E
⍝ Stopped: empty, and the same test says leave (1).
⎕SEQPLAY←0
E←⎕EVWAIT ¯1
⍴E
∨/¯1 1≠2↑E
⍝ A restart counts from beat 0 again.
⎕SEQPLAY←2
⎕EVWAIT ¯1
⎕EVWAIT ¯1
⍝ Phase 7b: windows and the sequencer in one wait; ⎕WCLICK is the same.
A←'A' ⎕WOPEN 0 0 200 150
⍝ A posted click comes before the next beat.
Q←⎕EVPOST 1,A,5 6
⎕EVWAIT ¯1,A
⍝ Nothing posted: the beat.
⎕WCLICK A,¯1
⍝ A click in a window not waited on is ignored: the beat.
Q←⎕EVPOST 1,A,7 8
⎕EVWAIT ¯1
⍝ Escape: empty, no interrupt.
Q←⎕EVPOST 3
⍴⎕EVWAIT ¯1,A
⍝ Ctrl-C: empty, the line finishes (X is assigned), then the interrupt.
X←99
Q←⎕EVPOST 4
X←⎕EVWAIT ¯1
⍴X
⍝ Ctrl-C already pending: a second wait on the line returns empty at once.
Q←⎕EVPOST 4
Y←(⍴⎕EVWAIT ¯1),⍴⎕WCLICK A,¯1
Y
⍝ The beat goes on after the interrupts.
⎕EVWAIT ¯1
⍝ A window waited on closing ends the wait, empty, as ⎕WCLICK always has.
Q←⎕EVPOST 2,A
⍴⎕EVWAIT ¯1,A
⍝ The closed window is skipped afterward: the beat.
⎕EVWAIT ¯1,A
⎕SEQCLOSE
⍴⎕EVWAIT ¯1
