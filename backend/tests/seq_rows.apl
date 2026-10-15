⍝ Phase 8, step 2: rows 3-5 of ⎕SEQTRACK's matrix (velocity, length in
⍝ beats, channel; ¯1 is the track's setting) and ⎕SEQGATE, on the test
⍝ clock (-S). Steps (row 2) and lengths (row 4) are both in beats.
⎕SEQOPEN 'hw:3,0,0'
⍝ Row 3: velocity, times the track's ⎕SEQVEL ÷ 100 (100: as written).
1 ⎕SEQTRACK 3 3⍴60 62 64, 1 1 1, 80 ¯1 127
⎕SEQPLAY←1
⎕SEQRUNTO 3
⎕SEQLOG
⍝ Track velocity 50: 80 gives 40, ¯1 gives 50, 127 gives 64.
1 ⎕SEQVEL 50
⎕SEQRUNTO 6
⎕SEQLOG
⎕SEQCLOSE
⍝ Row 4: length (row 3 all ¯1). 0.25 of a 0.5 step; 2 is shortened to
⍝ its 0.5 step; ¯1 takes the gate, here 0.5 of a 1-beat step.
⎕SEQOPEN 'hw:3,0,0'
1 ⎕SEQGATE 0.5
1 ⎕SEQTRACK 4 3⍴60 62 64, 0.5 0.5 1, ¯1 ¯1 ¯1, 0.25 2 ¯1
⎕SEQPLAY←1
⎕SEQRUNTO 2
⎕SEQLOG
⍝ ⎕SEQSTATE's column 11 is the gate (0 in the sequencer row).
S←⎕SEQSTATE
S[;1 11]
⎕SEQCLOSE
⍝ Row 5: channel, for that note alone; the track's stays 2. A rest's
⍝ rows 3-5 aren't looked at (track 3's first column).
⎕SEQOPEN 'hw:3,0,0'
2 ⎕SEQTRACK 5 2⍴60 62, 1 1, ¯1 ¯1, ¯1 ¯1, 10 ¯1
3 ⎕SEQTRACK 5 2⍴¯1 64, 1 1, 0 ¯1, 0 ¯1, 99 ¯1
⎕SEQPLAY←1
⎕SEQRUNTO 2
⎕SEQLOG
S←⎕SEQSTATE
S[;1 3]
⍝ Matrix errors: 1 or 6 rows; velocity 0 or a fraction; length 0;
⍝ channel 17.
1 ⎕SEQTRACK 1 2⍴60 1
1 ⎕SEQTRACK 6 1⍴60 1 ¯1 ¯1 ¯1 ¯1
1 ⎕SEQTRACK 3 1⍴60 1 0
1 ⎕SEQTRACK 3 1⍴60 1 0.5
1 ⎕SEQTRACK 4 1⍴60 1 ¯1 0
1 ⎕SEQTRACK 5 1⍴60 1 ¯1 ¯1 17
⍝ ⎕SEQGATE: one value for all or one per track; the previous settings.
1 2 ⎕SEQGATE 0.25
1 2 ⎕SEQGATE 1 0.75
1 ⎕SEQGATE 1
⍝ Gate errors: 0, over 1, a bad track, too many values, text, a matrix.
1 ⎕SEQGATE 0
1 ⎕SEQGATE 1.5
17 ⎕SEQGATE 0.5
1 2 ⎕SEQGATE 0.5 0.5 0.5
1 ⎕SEQGATE 'A'
1 ⎕SEQGATE 1 1⍴0.5
⎕SEQCLOSE
1 ⎕SEQGATE 0.5
