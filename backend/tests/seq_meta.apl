⍝ V101: ⎕SEQMETER (MONAD_OPT), ⎕SEQNAME, ⎕SEQNAMES; ⎕MIDIWRITE writes them
⍝ closed: Sequencer Not Open
⎕SEQMETER
⎕SEQNAMES
1 ⎕SEQNAME 'BASS'
⎕SEQOPEN 'TEST'
⍝ the meter: ¯1 ¯1 (none) by default; the result is the previous one
⎕SEQMETER
⎕SEQMETER←3 4
⎕SEQMETER
X←⎕SEQMETER←6 8
X
⎕SEQMETER 12 8
⍝ the MONAD_OPT shapes: niladic on either side of +, then followed by
⍝ something that resolves a value at once
⎕SEQMETER+1
1+⎕SEQMETER
⎕SEQMETER ,3 4
⎕SEQMETER ⎕SEQMETER
⎕SEQMETER
⍝ errors change nothing
⎕SEQMETER←
⎕SEQMETER←3
⎕SEQMETER←3 4 4
⎕SEQMETER←3 6
⎕SEQMETER←3 64
⎕SEQMETER←0 4
⎕SEQMETER←256 4
⎕SEQMETER←¯1 4
⎕SEQMETER←3.5 4
⎕SEQMETER←'AB'
⎕SEQMETER←2 1⍴3 4
⎕SEQMETER
⎕SEQMETER←¯1 ¯1
⎕SEQMETER
⍝ names: one track per call; the result is the previous name
⎕SEQNAMES
⍴⎕SEQNAMES
1 ⎕SEQNAME 'BASS'
2 ⎕SEQNAME 'PIANO   '
16 ⎕SEQNAME '  LEAD'
⎕SEQNAMES
⍴⎕SEQNAMES
1 ⎕SEQNAME 'DOUBLE BASS'
⍴1 ⎕SEQNAME 'BASS'
N←⎕SEQNAMES
N[⎕IO+1;]
⍴2 ⎕SEQNAME ''
16 ⎕SEQNAME '   '
(,3) ⎕SEQNAME 'A'
3 ⎕SEQNAME ⍳0
⎕SEQNAMES
⍝ letters, digits, space, - and . , ( ) ' & + _ only
4 ⎕SEQNAME 'Bass ',⎕LCASE 'GUITAR'
7 ⎕SEQNAME 'Drums 1-2 (Kit A), Perc. & Fx + Lead_B''s'
N←⎕SEQNAMES
N[⎕IO+3 6;]
4 ⎕SEQNAME ''
7 ⎕SEQNAME ''
⍝ everything else is a Domain Error, one character per line
1 ⎕SEQNAME 'A#'
1 ⎕SEQNAME 'A⎕'
1 ⎕SEQNAME 'A|'
1 ⎕SEQNAME 'A¯'
1 ⎕SEQNAME 'A/'
1 ⎕SEQNAME 'A\'
1 ⎕SEQNAME 'A{'
1 ⎕SEQNAME 'A<'
1 ⎕SEQNAME 'A:'
1 ⎕SEQNAME 'A!'
1 ⎕SEQNAME 'A"'
1 ⎕SEQNAME 'A*'
1 ⎕SEQNAME 'A$'
1 ⎕SEQNAME 'A~'
⍝ errors change nothing
1 2 ⎕SEQNAME 'X'
(⍳0) ⎕SEQNAME 'X'
0 ⎕SEQNAME 'X'
17 ⎕SEQNAME 'X'
1.5 ⎕SEQNAME 'X'
'A' ⎕SEQNAME 'X'
1 ⎕SEQNAME 5
1 ⎕SEQNAME 2 2⍴'ABCD'
1 ⎕SEQNAME 'A⍴B'
1 ⎕SEQNAME 'A←B'
1 ⎕SEQNAME 65⍴'A'
⍴6 ⎕SEQNAME 64⍴'A'
⍴6 ⎕SEQNAME (64⍴'B'),'   '
N←⎕SEQNAMES
⍴N
N[⎕IO+0 5;⍳10]
⍝ ⎕MIDIWRITE: the meter in track 1, which ⎕MIDIINFO reads back;
⍝ names aren't read back yet (⎕MIDIINFO is numeric)
6 ⎕SEQNAME ''
5 ⎕SEQNAME ''
4 ⎕SEQNAME ''
⎕SEQMETER←3 4
1 ⎕SEQTRACK 2 3⍴60 62 64 1 1 1
2 ⎕SEQTRACK 2 2⍴48 55 1.5 1.5
'META.MID' ⎕MIDIWRITE ¯1
(⎕MIDIINFO 'META.MID')[⎕IO;]
⎕SEQMETER←¯1 ¯1
'META2.MID' ⎕MIDIWRITE 1
(⎕MIDIINFO 'META2.MID')[⎕IO;]
⍝ a file's meter passes straight across, as ⎕SEQTEMPO's ¯1 does
⎕SEQMETER←(⎕MIDIINFO 'META.MID')[⎕IO;6 7]
⎕SEQMETER←(⎕MIDIINFO 'META2.MID')[⎕IO;6 7]
⎕SEQMETER
⍝ kept through ⎕SEQCLEAR and play; gone with ⎕SEQCLOSE
⎕SEQMETER←6 8
⎕SEQCLEAR 1 2
⎕SEQPLAY←1
⎕SEQRUNTO 4
⎕SEQPLAY←0
⎕SEQMETER
⎕SEQNAMES
⎕SEQCLOSE
⎕SEQOPEN 'TEST'
⎕SEQMETER
⍴⎕SEQNAMES
⎕SEQCLOSE
