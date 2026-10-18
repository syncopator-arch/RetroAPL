⍝ Window functions over lists of more than 32767 elements (V99):
⍝ get_data()'s short index, push_val()'s short count (⎕WVIS, ⎕WPIXEL
⍝ results), and a short loop counter in ⎕WCLOSE that would never reach
⍝ the end of the list. Before V99, apl_cli died at ⎕WVIS 40000⍴A.
)CLEAR 4000000
A←'A' ⎕WOPEN 0 0 200 150
V←⎕WVIS 1000⍴A
⍴V
∧/V=V[1]
V←⎕WVIS 40000⍴A
⍴V
∧/V=V[1]
P←A ⎕WPIXEL 2000⍴5
⍴P
∧/P=P[1]
P←A ⎕WPIXEL 80000⍴5
⍴P
∧/P=P[1]
Z←⎕WCLOSE 40000⍴A
⍴Z
⎕WVIS A
