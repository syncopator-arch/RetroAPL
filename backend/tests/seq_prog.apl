⍝ V100: ⎕SEQPROG and ⎕SEQBANK (k3 slots 21, 22), sent at a track's pass start
1 ⎕SEQPROG 40
⎕SEQOPEN 'TEST'
1 2 ⎕SEQPROG 40 41
1 ⎕SEQBANK 129
1 2 ⎕SEQBANK 5
1 ⎕SEQBANK 129
⍝ errors: nothing changes
1 ⎕SEQPROG 128
1 ⎕SEQPROG ¯2
1 ⎕SEQPROG 1.5
1 2 ⎕SEQPROG 1 2 3
17 ⎕SEQBANK 0
1 ⎕SEQBANK 16384
⍝ a whole float is fine
2 ⎕SEQPROG 82÷2
1 ⎕SEQTRACK 2 2⍴60 62 1 1
3 5 ⎕SEQTRACK 2 1⍴72 1
3 ⎕SEQPROG 0
⍝ columns 13 and 14: program and bank
S←⎕SEQSTATE
⍴S
S[;1 13 14]
⎕SEQPLAY←1
⎕SEQRUNTO 1
⎕SEQLOG
⍝ a change while playing: at the track's next pass (beat 2)
1 ⎕SEQPROG 42
⎕SEQRUNTO 3
⎕SEQLOG
⍝ the file gets the program heard with the version, the bank too
'PROG.MID' ⎕MIDIWRITE ¯1
I←⎕MIDIINFO 'PROG.MID'
⍴I
I
⍝ columns 8 and 9 pass straight on
1 ⎕SEQPROG I[2;8]
1 ⎕SEQBANK I[2;9]
⍝ a restart sends every track's program again at beat 0
⎕SEQPLAY←2
⎕SEQRUNTO 1
⎕SEQLOG
⎕SEQCLOSE
1 ⎕SEQPROG 40
