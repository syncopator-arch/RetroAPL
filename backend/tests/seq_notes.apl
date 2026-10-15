⍝ Phase 7c: note events (⎕SEQNOTES) and the beat switch (⎕SEQBEAT), on
⍝ the virtual clock (-S). A wait on ¯1 takes notes from the reporting
⍝ tracks, and beats only with ⎕SEQBEAT←1; each wait holds just past its
⍝ event. Results: ¯1 3 N, then track, pitch, index, count per note.
⍝ Closed: both are Sequencer Not Open.
⎕SEQBEAT
1 ⎕SEQNOTES 1
⎕SEQOPEN 'hw:3,0,0'
⍝ Defaults: beats off, no track reporting (the result: previous settings).
⎕SEQBEAT
1 2 ⎕SEQNOTES 0
⍝ Argument errors: a value other than 0 or 1, text, two values, a bad
⍝ track, a length mismatch.
⎕SEQBEAT 2
⎕SEQBEAT 'A'
⎕SEQBEAT 1 1
1 ⎕SEQNOTES 2
0 ⎕SEQNOTES 1
1 2 ⎕SEQNOTES 1 1 1
⍝ ⎕SEQBEAT is MONAD_OPT: the four shapes (1+ and +1 give 2 once set).
⎕SEQBEAT 1
⎕SEQBEAT
1+⎕SEQBEAT
⎕SEQBEAT+1
⎕SEQBEAT ,0
⎕SEQBEAT ⎕SEQBEAT
⍝ The ← form: nothing displayed; X gets the previous setting (1).
⎕SEQBEAT←1
X←⎕SEQBEAT←0
X
⎕SEQBEAT←
⍝ Two tracks. Track 2's rest is column 2: a column, never reported.
1 ⎕SEQTRACK 2 4⍴60 62 64 65,1 1 1 1
M←2 3⍴36 ¯1 38,2 1 1
2 ⎕SEQTRACK M
⎕SEQPLAY←1
⍝ Playing, beats off, nothing reporting: empty at once, not a hang.
⍴⎕EVWAIT ¯1
⍝ Track 2 reports: beat 0 (36, column 1), beat 3 (38, column 3), beat 4.
2 ⎕SEQNOTES 1
⎕EVWAIT ¯1
⎕EVWAIT ¯1
E←⎕EVWAIT ¯1
E
⍝ The index is the note's column: M[;I] is the note (36 2).
M[;E[⎕IO+5]]
⍝ Track 1 reports too, muted: it still reports (a sync track), but sends
⍝ nothing. Beat 5 (62, column 2), beat 6 (64, column 3), then beat 7 has
⍝ both tracks in one event (65 column 4; 38 column 3), shown as an N×4
⍝ matrix. The log has only track 2's notes.
1 ⎕SEQNOTES 1
1 ⎕SEQMUTE 1
Q←⎕SEQLOG
⎕EVWAIT ¯1
⎕EVWAIT ¯1
E←⎕EVWAIT ¯1
(E[⎕IO+2],4)⍴3↓E
⎕SEQLOG
⍝ Catch-up: busy until beat 15.5, then each track's latest note and how
⍝ many it started since the last one reported, at once, in one event:
⍝ track 1 65 column 4 count 8; track 2 38 column 3 count 4.
⎕SEQRUNTO 15.5
E←⎕EVWAIT ¯1
(E[⎕IO+2],4)⍴3↓E
⍝ ⎕SEQSTATE column 7: how late that event was handed over, in ms: the
⍝ newest note (15) was half a beat old, 250 ms at 120 bpm.
(⎕SEQSTATE)[1;7]
⍝ The index follows ⎕IO: beat 16, column 0 for both.
⎕IO←0
E←⎕EVWAIT ¯1
(E[⎕IO+2],4)⍴3↓E
⎕IO←1
⍝ A wait that was already waiting gets its notes as they start: 0.
(⎕SEQSTATE)[1;7]
⍝ Beats and notes. Track 2 alone: beats 17 and 18 are beats; beat 19 has
⍝ a note, which wins (that beat isn't reported); beat 20 the same.
1 ⎕SEQNOTES 0
⎕SEQBEAT←1
⎕EVWAIT ¯1
⎕EVWAIT ¯1
⎕EVWAIT ¯1
⎕EVWAIT ¯1
⎕SEQBEAT←0
⍝ A new version takes over at track 2's boundary (beat 24), and its
⍝ index starts again: 38 at 23 (column 3), 40 at 24 (column 1), 41 at 25.
2 ⎕SEQTRACK 2 2⍴40 41,1 3
⎕EVWAIT ¯1
⎕EVWAIT ¯1
⎕EVWAIT ¯1
⍝ ⎕SEQSTATE: column 6 of the sequencer row is ⎕SEQBEAT, column 10 of a
⍝ track row ⎕SEQNOTES.
⎕SEQSTATE
⍝ Stopped with notes outstanding: still handed over (41 column 2, count
⍝ 4: 40 at 28, 41 at 29, 40 at 32, 41 at 33). Stopped with none: empty.
⎕SEQRUNTO 33.5
⎕SEQPLAY←0
⎕EVWAIT ¯1
⍝ Measured from where it stopped: 41 at 33, half a beat: 250.
(⎕SEQSTATE)[1;7]
⍴⎕EVWAIT ¯1
⍝ Mixed with a window: a posted click comes first, then the next note
⍝ (40 at 36: the 41 at 33 lasts 3 beats).
A←'A' ⎕WOPEN 0 0 200 150
Q←⎕EVPOST 1,A,5 6
⎕SEQPLAY←1
⎕EVWAIT ¯1,A
⎕EVWAIT ¯1,A
⍝ A reporting track whose notes are all rests: the virtual clock gives
⍝ up after 10000 beats, a Domain Error rather than a hang.
⎕SEQPLAY←0
⎕SEQCLEAR 2
2 ⎕SEQTRACK 2 1⍴¯1 1
⎕SEQPLAY←1
⎕EVWAIT ¯1
⍝ Closed and opened again: the settings are back to their defaults.
⎕SEQCLOSE
⎕SEQOPEN 'hw:3,0,0'
⎕SEQBEAT
1 2 ⎕SEQNOTES 0
⎕SEQCLOSE
