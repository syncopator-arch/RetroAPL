⍝ ⎕MIDIREAD and ⎕MIDIINFO (Phase 9): files from tests/midi/, which run_tests copies to $APLHOME/midi/
⍝ t5000.mid: the old utility's file, two tracks, no tempo
⎕MIDIINFO 't5000.mid'
M←'t5000.mid' ⎕MIDIREAD 1
⍴M
M[;⍳6]
+/M[2;]
M←'t5000.mid' ⎕MIDIREAD 2 1
M[5;⍳6]
⍝ t5000m3.mid: MuseScore 3; its end (1 tick after the last note-off) rounds up to 35 beats
⎕MIDIINFO 't5000m3.mid'
M←'t5000m3.mid' ⎕MIDIREAD 2
⍴M
M[;⍳4]
+/M[2;]
⍝ chords.mid: a chord with its bottom note held under a melody: three voices
⎕MIDIINFO 'chords.mid'
'chords.mid' ⎕MIDIREAD 1 1
'chords.mid' ⎕MIDIREAD 1 2
'chords.mid' ⎕MIDIREAD 1,3
'chords.mid' ⎕MIDIREAD 2÷2
⍝ conductor.mid: a first track with no notes (voice 0 in ⎕MIDIINFO), a leading program change
⎕MIDIINFO 'conductor.mid'
'conductor.mid' ⎕MIDIREAD 2
⍝ no such file: no rows, not an error
⍴⎕MIDIINFO 'nosuch.mid'
0=1↑⍴⎕MIDIINFO 'nosuch.mid'
⍝ a name typed in capitals: tried as given, then with A-Z in lower case
⎕MIDIINFO 'CHORDS.MID'
⍴'T5000M3.MID' ⎕MIDIREAD 2 1
⍴⎕MIDIINFO 'NOSUCH.MID'
⍝ results don't depend on ⎕IO
⎕IO←0
'chords.mid' ⎕MIDIREAD 1 2
(⎕MIDIINFO 'chords.mid')[;1]
⎕IO←1
⍝ errors
'conductor.mid' ⎕MIDIREAD 1
'conductor.mid' ⎕MIDIREAD 3
'chords.mid' ⎕MIDIREAD 1 4
'chords.mid' ⎕MIDIREAD 1 0
'chords.mid' ⎕MIDIREAD 2
'chords.mid' ⎕MIDIREAD 0
'chords.mid' ⎕MIDIREAD 1.5
'chords.mid' ⎕MIDIREAD 1 1 1
'chords.mid' ⎕MIDIREAD ⍳0
'chords.mid' ⎕MIDIREAD 1 1⍴1
'chords.mid' ⎕MIDIREAD '1'
1 ⎕MIDIREAD 1
'' ⎕MIDIREAD 1
'nosuch.mid' ⎕MIDIREAD 1
'NOSUCH.MID' ⎕MIDIREAD 1
'notmidi.mid' ⎕MIDIREAD 1
⎕MIDIINFO 'notmidi.mid'
⎕MIDIINFO 1
⎕MIDIINFO ''
