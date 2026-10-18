⍝ System commands run only from the prompt (V99). On a function line, in
⍝ ⍎ and in ⎕ input they're Incorrect Command. At the prompt they work,
⍝ with a function suspended too -- the debugging commands above all.
)ENTER CMW
)ENTER QQ
)LIST CMW
)WIDTH
CMW
)SI
⍝ at the suspended prompt
)STOP CMW 2
)STOP OFF
)STEP
)LEFT
)RIGHT
)DEBUG
)DEBUG
)WIDTH
→
)SI
⍝ in ⍎, at the prompt
⍎')WIDTH'
⍝ in ⎕ input, at the prompt and in a function
X←⎕
)WIDTH
5
X
QQ
)WIDTH
5
)SI
→
)SI
⍝ )OFF is refused too, until ⎕SYS 1's exit works. Before the guard it
⍝ ended the function quietly and left the stack inconsistent (the next
⍝ → printed a line of the function, and an earlier suspension survived)
)ENTER CMOFF
)LIST CMOFF
CMOFF
)SI
→
)SI
