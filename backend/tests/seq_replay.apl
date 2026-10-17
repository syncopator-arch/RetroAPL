⍝ V96: a finished play-once track keeps its version until a restart plays it again
⎕SEQOPEN 'TEST'
⍝ three voices of different lengths, all played once, as with a file's voices
1 ⎕SEQTRACK 2 2⍴60 62 1 1
2 ⎕SEQTRACK 2 1⍴48 3
3 ⎕SEQTRACK 2 4⍴72 74 76 77,4⍴0.5
(⍳3) ⎕SEQLOOP 0
⎕SEQPLAY←1
⎕SEQRUNTO 4
⎕SEQLOG
⍝ all three have finished: their rows stay, with column 12 set
⎕SEQSTATE
⎕SEQSTATE[;12]
⍝ a stop and continue doesn't replay them: nothing sounds
⎕SEQPLAY←0
⎕SEQPLAY←1
⎕SEQRUNTO 6
⎕SEQLOG
⍝ a restart plays all three again from beat 0
⎕SEQPLAY←2
⎕SEQRUNTO 4
⎕SEQLOG
⍝ ⎕MIDIWRITE writes finished tracks
'r.mid' ⎕MIDIWRITE ¯1
'r.mid' ⎕MIDIREAD 3
⍝ resending a finished track starts it at the next whole beat
⎕SEQRUNTO 5.5
1 ⎕SEQTRACK 2 1⍴65 1
⎕SEQSTATE[;12]
⍝ ⎕SEQCLEAR of a finished track: it held something, and its row goes at once
⎕SEQCLEAR 2
⎕SEQSTATE[;1]
⍝ ⎕SEQCLOSE lists the finished tracks
⎕SEQRUNTO 8
⎕SEQLOG
⎕SEQCLOSE
