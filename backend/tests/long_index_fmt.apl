⍝ ⍕ of a matrix with more than 32767 elements (V99). Its column-width
⍝ pass reads every element, through get_data()/get_fdata(), whose index
⍝ was a short: past 32767 it read before the data. G is there so what
⍝ lies before M's data is large numbers. Column widths should be as
⍝ for the small matrices. A big workspace, so nothing runs out.
)CLEAR 4000000
G←40000⍴99999
M←200 200⍴1
X←⍕M
⍴X
20↑X
20↑⍕2 3⍴1
G←33000⍴1E300
F←200 200⍴1.5
X←⍕F
⍴X
20↑X
20↑⍕2 3⍴1.5
