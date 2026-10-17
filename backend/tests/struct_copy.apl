⍝ a struct with an empty cell, copied by assignment (crashed before V97)
S←⎕STRUCT 3
S[1]←⍳3
S[3]←'AB'
T←S
T[1]
T[3]
S[1]←99
T[1]
S[1]
)ERASE S
T[1]
T[3]
⍝ a struct of structs, copied (nested empty cells)
N←⎕STRUCT 2
N[2]←T
P←N
P[2][3]
)ERASE N
P[2][1]
⍝ catenate copies too
U←T,T
⍴U
U[1]
U[3]
U[4]
U[6]
V←T⍪T
V[3]
V[4]
V[6]
)ERASE T
U[4]
V[6]
⍝ scalar functions on a struct: Domain Error (S+1 gave the cell offsets)
S←⎕STRUCT 2
S[1]←5
S+1
1+S
S=S
S|1
S⌈S
1 2∘.×S
⍴S
)ERASE S U V P
