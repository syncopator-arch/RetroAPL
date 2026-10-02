S←⎕STRUCT 3
S[1]←⍳3
S[2]←'AB'
S[3]←10 20
⍴,S
T←,S
T[1]
T[2]
T[3]
S[2]←'XYZ'
S[2]
T[2]
)ERASE S
T[1]
T[3]
W←,,T
W[3]
U←⍪T
⍴U
V←,U
V[1]
V[2]
E←⎕STRUCT 2
E[2]←5
F←,E
F[2]
F[1]
G←⎕STRUCT 2 2
G[1;1]←'P'
G[2;2]←⍳2
H←⍪G
⍴H
H[2;2]
K←,G
K[1]
K[4]
)ERASE G
K[4]
H[1;1]
