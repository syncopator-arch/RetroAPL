⍝ V96: ⎕F←NAME compiles when NAME has no value yet, as a function's local does at )ENTER (it was a Syntax Error at compile time)
⍝ no value: these compile now, and give Value Error when run, as X←QQZ does; ⎕IO stays 1
⎕IO←QQZ
⎕IO←QQZ[1;1]
V←⎕IO←QQZ
⎕IO←QQZ+1
⎕IO
⍝ any quad function given a name with no value: Value Error (V96; it read the workspace header instead)
⎕UCASE QQZ
⎕VT QQZ
⍝ once it has a value: the line from the report, and its shapes
QQZ←2 5⍴0 1 1 1 1 0 0 0 0 0
V←⎕IO←QQZ[1;1]
V
⎕IO
⍝ origin 0 now: row 0, column 1 is 1
⎕IO←QQZ[0;1]
⎕IO
⍝ still Syntax Errors: nothing to the right of the ←
⎕IO←
(⎕IO←)
