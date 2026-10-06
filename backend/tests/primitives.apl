⍝ PRIMITIVES: ONE CHECK PER PRIMITIVE_HELP.MD ENTRY.
V←1 2 3 4
M←2 3⍴⍳6
⍝ ---- SCALAR EXTENSION: SHORTER ARGUMENT REUSED ----
V+1 2
1 2 3+V
M+1
⍝ ---- ARITHMETIC ----
-V
×¯3 0 5
3+V
3-V
3×V
÷4 2
12÷V
5÷0
⍟1
2⍟8
2⍟1024
○1
|¯3 4
10|7 ¯7
¯3|¯7
3|¯7
0|5
2.5|7.25
⌊1.5 ¯2.25 3.75
3⌈V
3⌊V
⎕RL←42
?6 6 6
⍝ ---- COMPARISON AND LOGIC ----
1 2 3<2 2 2
1 2 3≤2 2 2
1 2 3=1 3 3
1 2 3≥2 2 2
1 2 3>2 2 2
1 2 3≠1 3 3
'ABC'='ABD'
0 0 1 1∧0 1 0 1
0 0 1 1∨0 1 0 1
0 0 1 1⍲0 1 0 1
0 0 1 1⍱0 1 0 1
2∧3
0 1∨0.5
~0 1
⍝ ---- BITWISE ----
12∩10
¯1∩255
12∪3
4⊂1
16⊃2
500.0⊃2
⍝ ---- STRUCTURE ----
⍴M
⍴5
5⍴V
0⍴V
,M
V,5 6
M,9
M⍪M
V⍪V
⍉M
2 1⍉M
1 1⍉3 3⍴⍳9
⌽V
⌽M
1⌽V
¯1⌽V
1 2⌽M
⊖M
1⊖M
2↑V
¯2↑V
6↑V
1 2↑M
2↓V
¯1↓V
1 0↓M
0 1↓M
⍝ ---- SELECTION AND SEARCH ----
1 0 1 0/V
2 0 1 0/V
1 0 1/M
1 0⌿M
1 0 1\7 8
1 0 1⍀2 2⍴⍳4
V⍳3
V⍳9
'ABC'⍳'CQ'
3∊V
V∊2 4
⍋3 1 2
⍒3 1 2
⍋'CAB'
V[3 1]
M[1;]
M[;2]
M[2;3]
W←V
W[2]←9
W
(2 2⍴1 2 2 3)⌷M
⍝ ---- MONADIC ⍳ ON A VECTOR (MULTI-DIMENSIONAL INDEX GENERATOR) ----
⍳,4
⍳2 3
⍳2 2 2
⍴⍳2 0
⍴⍳0 3
R←2 3⍴10×⍳6
(⍳⍴R)⌷R
⎕IO←0
⍳2 3
⍳3
⎕IO←1
⍝ ---- ENCODE AND DECODE ----
24 60 60⊤3725
10⊥1 2 3
⍝ ---- OPERATORS ----
+/V
-/V
×/V
⌈/V
⌊/V
∧/1 1 0
∨/0 0 1
≠/1 0 1
+/M
+⌿M
+/⍳0
×/⍳0
⌈/⍳0
⌊/⍳0
+\V
-\V
∧\1 1 0 1
+\M
+⍀M
V∘.×V
2 4∘.÷1 2
2 4∘.⊂1 2
1 2∘.⌽M
1 2∘.,3 4
M+.×3 2⍴⍳6
2 4⌈.×1 1
2 4<.×1 1
2 4+.|1 2
2 4+.⊂1 2
⍝ ---- EXECUTE AND FORMAT ----
⍎'2+3'
⍕1.5 ¯2.25 3.75
⍕M
⍕'AB'
⍝ ---- OUTPUT ----
⎕←7
⍞←'HI'
⍝ ---- NOT IMPLEMENTED (SYNTAX ERROR) ----
+V
↑V
∊V
1⍋3 1 2
1⍕1.5
÷/V
2 4÷.×1 1
2 4,.×1 1
+/[1]M
⍝ ---- END ----
