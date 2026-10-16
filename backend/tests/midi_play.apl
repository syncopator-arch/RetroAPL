⍝ A file's voices sent straight to the sequencer (Phase 9): the virtual clock (-S)
⎕SEQOPEN 'TEST'
1 ⎕SEQTRACK 'chords.mid' ⎕MIDIREAD 1 1
2 ⎕SEQTRACK 'chords.mid' ⎕MIDIREAD 1 2
3 ⎕SEQTRACK 'chords.mid' ⎕MIDIREAD 1 3
⎕SEQPLAY←1
⎕SEQRUNTO 4.5
⎕SEQLOG
⎕SEQCLOSE
