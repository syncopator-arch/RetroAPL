⍝ Phase 8, step 1: ⎕SEQPORTS and opening by device name, under -S.
⍝ Under -S ⎕SEQPORTS is a fixed list of three test ports (not the
⍝ machine's), and every ⎕SEQOPEN opens the test backend, a device name
⍝ too (nothing is looked up). Matching names against real ports is
⍝ tested in the sequencer library (tests/device/byname).
⍝ Closed: ⎕SEQPORTS works, a character matrix, port then label.
P←⎕SEQPORTS
⍴P
P
⍝ A device name opens (the case of the name doesn't matter).
⎕SEQOPEN 'TEST CABLE'
⍝ Open: the same list.
⍴⎕SEQPORTS
⎕SEQCLOSE
⍝ Still text only: a matrix is a Rank Error.
⎕SEQOPEN 2 2⍴'ABCD'
⍝ ⎕SEQPORTS is niladic: it combines like a value.
3=1↑⍴⎕SEQPORTS
