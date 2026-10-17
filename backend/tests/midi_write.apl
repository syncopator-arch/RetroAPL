⍝ ⎕MIDIWRITE (Phase 9, step 2): sequencer tracks to a MIDI file, read back with ⎕MIDIINFO and ⎕MIDIREAD
⍝ the sequencer must be open
'w.mid' ⎕MIDIWRITE ¯1
⎕SEQOPEN 'TEST'
1 ⎕SEQTRACK 2 3⍴60 62 64 1 1 2
1 ⎕SEQGATE 0.5
3 10 ⎕SEQTRACK 5 4⍴¯1 42 42 38,1,(1÷3),(4÷3),1,¯1 100 70 127,¯1 0.25 0.25 ¯1,4⍴¯1
3 ⎕SEQVEL 50
⍝ ¯1: every track with something; track 2, a gap, is written empty
'w.mid' ⎕MIDIWRITE ¯1
⎕MIDIINFO 'w.mid'
⍝ as it sounds: track 1's lengths from the gate, track 3's velocities scaled by ⎕SEQVEL
'w.mid' ⎕MIDIREAD 1
'w.mid' ⎕MIDIREAD 3
⍝ a list: track 5 has nothing, so only 1 is in the result; 2-5 are written empty
'W2.MID' ⎕MIDIWRITE 5 1
⎕MIDIINFO 'W2.MID'
⍝ an existing file is replaced; the tempo is ⎕SEQTEMPO's
⎕SEQTEMPO←90
'w.mid' ⎕MIDIWRITE 3
⎕MIDIINFO 'w.mid'
⍝ a loop whose length isn't on its notes' grid (triplets, then a rest to 4.5 beats) comes back exactly: the writer records it
4 ⎕SEQTRACK 2 4⍴60 62 64 ¯1,(1÷3),(1÷3),(1÷3),3.5
'w.mid' ⎕MIDIWRITE 4
+/('w.mid' ⎕MIDIREAD 4)[2;]
⍝ errors
'x.mid' ⎕MIDIWRITE 17
'x.mid' ⎕MIDIWRITE 0
'x.mid' ⎕MIDIWRITE ¯1 1
'x.mid' ⎕MIDIWRITE ⍳0
'x.mid' ⎕MIDIWRITE 1.5
'x.mid' ⎕MIDIWRITE 1 1⍴1
'nodir/x.mid' ⎕MIDIWRITE 1
1 ⎕MIDIWRITE 1
'' ⎕MIDIWRITE 1
⍴⎕MIDIINFO 'x.mid'
⎕SEQCLOSE
⍝ written in lower case (V98): 'W2.MID' is w2.mid on disk; x.mid was never written
)MIDI W
