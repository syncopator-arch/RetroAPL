⍝ ⎕ straight before a name that isn't a special function (V109) is
⍝ Unknown Special Function. Before, the ⎕ was quad input beside the
⍝ name: ⎕BLBLB gave a ⎕: prompt. Quoted, or after a space, unchanged.
⎕IO
⎕BLBLB
⎕IO∆X
⎕IOX
⎕BLBLB←1
'⎕BLBLB'
X←⎕IO
X
⍝ ⎕BLBLB IN A COMMENT
