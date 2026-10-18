⍝ )CLEAR, )PURGE and )LOAD off the prompt (V99). Before the guard,
⍝ )CLEAR and )PURGE reset the workspace or popped the SI under the
⍝ running function. )LOAD names no real workspace here (a saved one
⍝ would print its save time), so before the guard it was Ws Not Found.
)ENTER CMCLR
)ENTER CMPUR
)ENTER CMLOAD
Y←42
CMCLR
)SI
Y
→
CMPUR
)SI
Y
→
CMLOAD
)SI
→
⍎')CLEAR'
Y
⍎')PURGE'
Y
X←⎕
)CLEAR
7
Y
)FNS
)SI
