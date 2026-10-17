⍝ residue: the result takes L's sign (integer branch fixed; float was right)
¯3|7
¯3|7 ¯7 0 6 ¯6
3|7 ¯7
¯3|7.0
¯2.5|7.25
⍝ L|R = R - L×⌊R÷L for every sign of L and R
X←¯10+⍳21
∧/X=(¯3×⌊X÷¯3)+¯3|X
∧/X=(3×⌊X÷3)+3|X
⍝ 0|R is R, integer and float (float gave NaN)
0|7 ¯7
0|7.5 ¯2.5 0
0|2.5
⍝ 0÷0 is 1; any other ÷0 is still a Domain Error
0÷0
0 6 0÷0 3 2
0÷0.0
0.0÷0
5÷0
1 0+.÷1 0
)INTDIV ON
0÷0
0 6÷0 3
5÷0
)INTDIV OFF
⍝ take and drop: a short L applies to the leading axes
M←2 3⍴⍳6
1↓M
⍴1↓M
1↑M
⍴1↑M
¯1↑M
¯1↓M
3↑M
5↓M
⍴5↓M
1 0↓M
1 2↑M
A←2 3 4⍴⍳24
⍴1↑A
⍴1 2↑A
1 2↑A
⍴¯1↓A
¯1 ¯1↓A
C←2 3⍴'ABCDEF'
1↓C
¯1↑C
⍴(⍳0)↑M
(⍳0)↓M
(⍳0)↑5
⍴(⍳0)↑5
(⍳0)↑⍳3
⍝ unchanged: vectors, a scalar R, and a longer L (adds leading axes)
2↑⍳5
¯2↓⍳5
3↑5
3 4↑⍳9
⍴3 4↑⍳9
⍝ outer product: only scalar functions and , ⌽ ⊖
1 2∘.⍴3
1 2∘.↑3 4 5
1 2∘.↓3 4 5
1 2∘.⍳3
1 2∘.∊3
1 2∘.?5
1 2∘.⊤5
1 2∘.,3 4
1 2∘.⌽2 3⍴⍳6
1 2∘.|5 6
1 2∘.÷0 2
