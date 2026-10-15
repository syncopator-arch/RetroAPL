⍝ Phase 7c: ⎕SEQBEAT under -q new (-S -q new): only the ← form sets it.
⎕SEQOPEN 'hw:3,0,0'
⎕SEQBEAT←1
⎕SEQBEAT
⍝ The old form is a Syntax Error under -q new.
⎕SEQBEAT 0
⎕SEQBEAT
⎕SEQCLOSE
