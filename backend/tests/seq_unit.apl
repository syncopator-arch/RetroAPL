⍝ )SEQUNIT (V102): steps and lengths counted in units of a beat, a
⍝ workspace setting. It works with the sequencer closed, and a new
⍝ workspace starts at 1, counting in beats. )SAVE and )LOAD print
⍝ times, so keeping it across a save is checked by hand.
)SEQUNIT
)SEQUNIT 24
)SEQUNIT
)SEQUNIT 8
⍝ only divisors of 2520 from 1 to 255; anything else is a Domain
⍝ Error and changes nothing
)SEQUNIT 16
)SEQUNIT 0
)SEQUNIT 256
)SEQUNIT 2520
)SEQUNIT ¯24
)SEQUNIT 1.5
)SEQUNIT X
)SEQUNIT 24 8
)SEQUNIT
⍝ from the prompt only, as every command
⍎')SEQUNIT 24'
)SEQUNIT
⍝ )CLEAR goes back to 1
)CLEAR
)SEQUNIT
⍝ the same music at unit 1 and at unit 24 sends the same MIDI
⎕SEQOPEN 'TEST'
M←4 4⍴60 62 64 65, 0.25 0.25 0.5 1, ¯1 ¯1 ¯1 ¯1, 0.25 0.125 ¯1 0.75
1 ⎕SEQTRACK M
⎕SEQPLAY←1
⎕SEQRUNTO 4
L1←⎕SEQLOG
⎕SEQCLOSE
)SEQUNIT 24
⎕SEQOPEN 'TEST'
M←4 4⍴60 62 64 65, 6 6 12 24, ¯1 ¯1 ¯1 ¯1, 6 3 ¯1 18
1 ⎕SEQTRACK M
⎕SEQPLAY←1
⎕SEQRUNTO 96
L2←⎕SEQLOG
⍴L1
⍴L2
∧/,L1=L2
L2[⍳3;]
⍝ ⎕SEQSTATE in units: the position (row 1), the track's loop length
⍝ and where it is in its loop (columns 7 and 9)
S←⎕SEQSTATE
S[1;3]
S[2;7 9]
⎕SEQRUNTO 100
S←⎕SEQSTATE
S[1;3]
S[2;7 9]
⍝ ⎕SEQTRACK's result, where the new version takes over: the next
⍝ loop boundary, 144
1 ⎕SEQTRACK M
⍝ the gate is still a fraction of the step, the tempo still beats a
⍝ minute
1 ⎕SEQGATE 0.5
⎕SEQTEMPO
⍝ changing the unit changes only how numbers are read and reported
)SEQUNIT 1
S←⎕SEQSTATE
S[1;3]
S[2;7]
)SEQUNIT 24
⍝ the shortest step is one tick, 1/2520 of a beat: 1/105 of a unit
⍝ at 24
2 ⎕SEQTRACK 2 1⍴60,÷105
2 ⎕SEQTRACK 2 1⍴60,÷106
⎕SEQCLEAR 2
⎕SEQRUNTO ¯1
⎕SEQCLOSE
⍝ unit 7: results come from the sequencer's ticks, so whole numbers
⍝ stay whole (29 sevenths of a beat taken as beats and multiplied by
⍝ 7 would not be exactly 29)
)SEQUNIT 7
⎕SEQOPEN 'TEST'
1 ⎕SEQTRACK 2 3⍴60 62 64, 10 10 9
⎕SEQPLAY←1
⎕SEQRUNTO 31
S←⎕SEQSTATE
S[1;3]
S[2;7 9]
(S[1;3]=31),S[2;7 9]=29 2
⍝ ⎕MIDIWRITE, then ⎕MIDIREAD and ⎕MIDIINFO in units
'UNIT7.MID' ⎕MIDIWRITE 1
M←'UNIT7.MID' ⎕MIDIREAD 1
M
∧/,M[2 4;]=⌊M[2 4;]
29=+/M[2;]
(⎕MIDIINFO 'UNIT7.MID')[2;4]
29=(⎕MIDIINFO 'UNIT7.MID')[2;4]
⎕SEQCLOSE
⍝ files from elsewhere: t5000m3.mid (MuseScore, 480 a beat) in 24ths,
⍝ t5000.mid (120 a beat) in 8ths
)SEQUNIT 24
M←'t5000m3.mid' ⎕MIDIREAD 2
M[;⍳4]
+/M[2;]
(⎕MIDIINFO 't5000m3.mid')[2;4]
)SEQUNIT 8
('t5000.mid' ⎕MIDIREAD 1)[2;⍳6]
(⎕MIDIINFO 't5000.mid')[2;4]
)SEQUNIT 1
