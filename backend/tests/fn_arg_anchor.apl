⍝ begin_fn() anchors: a left temp with garbage below it, a right argument that must be copied (V103)
B←43 43 43 43 43 41 43 41 43 41 43 41 41 37 41 37 41 37 41 37 37 35 37 35 37 35 37 35
)ENTER LFTTMP
)ENTER RGTTMP
)LIST LFTTMP
)LIST RGTTMP
(12+7 4 0) LFTTMP B
(12+7 4 0) RGTTMP B
M←12+7 4 0
M LFTTMP B
M RGTTMP B
(12+7 4 0) LFTTMP 1 2 3
B
