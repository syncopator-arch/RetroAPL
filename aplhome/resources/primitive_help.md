<!-- font: Georgia, DejaVu Serif, serif -->
# Primitive Function Reference

What each APL primitive does in RetroAPL, for quick lookup while you're
working. This isn't a guide to the language. For that, use Gilman and
Rose, *APL: An Interactive Approach*, third edition (John Wiley &
Sons, 1984), which this
reference calls "the book." Where an entry says nothing beyond a short
description, the primitive works as the book describes. Entries say
more only where RetroAPL adds something, differs, or leaves something
out.

In the calling shapes, `L` is the left argument and `R` the right.
Examples assume the default index origin, `⎕IO←1`.

Scalar functions (the arithmetic, comparison, logic and bitwise
groups) work item by item. A single-item argument is used with every
item of the other. When both arguments are vectors of different
lengths, the shorter one is reused from its start to fill out the
longer, where the book gives a `Length Error`: `1 2 3 4+1 2` is
`2 4 4 6`.

The same goes for arrays of any shape. Items are taken in row order,
and the result has the shape of the argument with more items (if both
have as many, the one of higher rank, or else the left). So
`(3 4⍴⍳12)+⍳4` adds `1 2 3 4` to each row, and `M+V` with `V` as long
as a row of `M` is a quick way to work row by row. Shapes aren't
otherwise checked: `(2 3⍴1)=2 4⍴1` gives a 2 by 4 result, made by
reusing the left argument's 6 items, so its rows don't line up with the
right's. Compare matrices of different widths with care.

---

## Summary

A dash means that form isn't implemented. Using it is a
`Syntax Error`.

| Glyph | Monadic | Dyadic |
|---|---|---|
| `+` | — | add |
| `-` | negate | subtract |
| `×` | sign | multiply |
| `÷` | reciprocal | divide |
| `*` | exponential | power |
| `⍟` | natural log | log |
| `○` | pi times | circle functions |
| `∣` | magnitude | residue |
| `⌈` | ceiling | maximum |
| `⌊` | floor | minimum |
| `?` | roll | deal |
| `< ≤ = ≥ > ≠` | — | comparison |
| `∧ ∨` | — | and, or |
| `⍲ ⍱` | — | nand, nor |
| `~` | not | — |
| `∩ ∪` | — | bitwise and, or |
| `⊂ ⊃` | — | shift left, right |
| `⍴` | shape | reshape |
| `,` | ravel | catenate |
| `⍪` | table | catenate first axis |
| `⍉` | transpose | transpose by axes |
| `⌽` | reverse | rotate |
| `⊖` | reverse first axis | rotate first axis |
| `↑` | — | take |
| `↓` | — | drop |
| `/` | (reduce, as `f/`) | compress, replicate |
| `⌿` | (reduce first axis) | compress first axis |
| `\` | (scan, as `f\`) | expand |
| `⍀` | (scan first axis) | expand first axis |
| `⍳` | index generator | index of |
| `∊` | — | membership |
| `⍋` | grade up | — |
| `⍒` | grade down | — |
| `⌷` | — | squad (gather) |
| `⊤` | — | encode |
| `⊥` | — | decode |
| `∘.f` | — | outer product |
| `f.g` | — | inner product |
| `⍎` | execute | — |
| `⍕` | format | — |

---

## Arithmetic

`-R`

`L-R`

Negate, subtract.

`×R`

`L×R`

Sign (`¯1`, `0` or `1`), multiply.

`L+R`

Add. There's no monadic `+`.

`÷R`

`L÷R`

Reciprocal, divide. Dividing by zero is a `Domain Error`, except
`0÷0`, which is `1`, as in the book. Whether
`7÷2` is `3.5` or `3` depends on the `)INTDIV` setting (also
`⎕INTDIV`): off, the default, gives the usual APL result; on, a
division of two integers is truncated toward zero. Monadic `÷`
always gives a float.

`*R`

`L*R`

Exponential (`e` to the power `R`), power. A result too large to
hold, such as `*1000` or `10*400`, is a `Domain Error`, and so is a
negative number to a fractional power (`¯8*÷3`).

`⍟R`

`L⍟R`

Natural logarithm, logarithm of `R` to base `L` (`2⍟1024` is `10`).
A `Domain Error` for `R` of zero or less, or a base of zero, one or
less than zero.

`○R`

`L○R`

Pi times `R`, and the circle functions. `L` runs from `¯7` to `7` as in
the book's table: `0` is `(1-R*2)*0.5`, `1` to `3` are sine, cosine
and tangent, `4` is `(1+R*2)*0.5`, `5` to `7` are the hyperbolic sine,
cosine and tangent, and the negatives are their inverses. Any other
`L`, or an `R` outside a function's domain (`¯4○0.5`), is a
`Domain Error`. `¯4○R` is never negative.

`|R`

`L|R`

Magnitude, residue. `0|R` is `R`. The result has `L`'s sign
(`¯3|7` is `¯2`, `3|¯7` is `2`): for `L` not 0, `R` is always
`(L×⌊R÷L)+L|R`.

`⌈R`

`L⌈R`

Ceiling, maximum.

`⌊R`

`L⌊R`

Floor, minimum.

`⌈` rounds up and `⌊` rounds down (`⌈¯2.1` is `¯2`, `⌊¯2.1` is `¯3`),
and the result is stored as a whole number when it fits. For a whole
number, monadic `⌈` and `⌊` leave the value alone and change only how
it's stored: `⌈` moves it up one storage size (the last step makes it a
float, so `(⌈⌈⌈3)÷2` is `1.5` even under `)INTDIV ON`), and `⌊` stores
it in the smallest size that holds it. `⎕VT` shows the storage type.

`?R`

`L?R`

Roll: a random whole number from `1` to `R` (from `⎕IO`), item by
item. Deal: `L` different numbers chosen at random from `⍳R`. `L` and
`R` must each be a single number, with `L` no larger than `R`. Set
`⎕RL` to get the same results every time.

---

## Comparison and logic

`L<R`

`L≤R`

`L=R`

`L≥R`

`L>R`

`L≠R`

Comparisons, giving `1` for true and `0` for false. `=` and `≠` also
compare characters (`'ABC'='ABD'` is `1 1 0`). Comparisons are exact:
there's no `⎕CT`, so two floats that differ only in the last place are
unequal.

`L∧R`

`L∨R`

`L⍲R`

`L⍱R`

And, or, nand, nor. Any nonzero number counts as true, where the book
allows only `0` and `1`: `2∧3` is `1`, `0 1∨0.5` is `1 1`.

`~R`

Not. Dyadic `~` isn't supported: don't use it. (It isn't APL2's
"without"; at present it gives the same result as `∨`.)

---

## Bitwise

These four glyphs are APL2 primitives in other APLs. Here they work on
the bits of whole numbers, with negative numbers in two's complement.
A float with a whole value is accepted (`500.0⊃2` is `125`).

`L∩R`

Bitwise and: `12∩10` is `8`, `¯1∩255` is `255`.

`L∪R`

Bitwise or: `12∪3` is `15`.

`L⊂R`

`L` shifted left by `R` bits: `4⊂1` is `8`.

`L⊃R`

`L` shifted right by `R` bits: `16⊃2` is `4`.

---

## Structure

`⍴R`

`L⍴R`

Shape, reshape. Reshape reuses `R`'s items from the start as often as
needed (`5⍴1 2 3 4` is `1 2 3 4 1`).

`,R`

`L,R`

Ravel, catenate. Catenation joins along the last axis (`M,9` adds a
column of 9s to a matrix `M`).

`⍪R`

`L⍪R`

Table, catenate along the first axis. Table makes a matrix: a scalar
becomes 1 by 1, a vector one column, and a higher-rank array keeps its
first axis and runs the rest together (a 2 by 3 by 4 array becomes 2
by 12). `M⍪M` stacks two matrices; `V⍪W` for two vectors is the same as
`V,W`.

`⍉R`

`L⍉R`

Transpose: monadic reverses the order of the axes. Dyadic gives the
new position of each axis of `R`, and repeating a number takes a
diagonal (`1 1⍉3 3⍴⍳9` is `1 5 9`).

`⌽R`

`L⌽R`

Reverse, rotate along the last axis. With a matrix, `L` can give a
separate amount for each row (`1 2⌽M`).

`⊖R`

`L⊖R`

Reverse, rotate along the first axis.

`L↑R`

`L↓R`

Take, drop: one number in `L` for each axis of `R`, negative to count
from the end. Taking more than there are pads with zeros
(`6↑1 2 3 4` is `1 2 3 4 0 0`). A shorter `L` applies to the leading
axes and leaves the rest whole: with `M←2 3⍴⍳6`, `1↓M` is the 1 by 3
matrix `4 5 6`, the same as `1 0↓M`. A longer `L` adds leading axes
(`3 4↑⍳9` is 3 by 4). A row picked with a vector index
(`M[(⍳1);]`) is a 1-row matrix, so `4↑M[(⍳1);]` takes 4 rows; ravel it
first (`4↑,M[(⍳1);]`) to take from the row. Before V97 a short `L` acted
on the last axis instead. No monadic forms.

---

## Selection and search

`L/R`

`L⌿R`

Compress and replicate, along the last axis (`/`) or first axis (`⌿`).
Each number in `L` says how many times to repeat the matching item of
`R`, so `2 0 1 0/1 2 3 4` is `1 1 3`. A negative number puts in that
many fill items instead (blanks or zeros), as in APL2:
`1 ¯2 1/'ABC'` is `A  C`. `f/` and `f⌿` with a function on the left
are reduction, below.

`L\R`

`L⍀R`

Expand, along the last axis (`\`) or first axis (`⍀`): `1 0 1\7 8` is
`7 0 8`. `f\` and `f⍀` with a function on the left are scan, below.

`⍳R`

`L⍳R`

Index generator, index of. Index of gives the first position of each
item of `R` in the vector `L`, or one past the end if it isn't there
(`'ABC'⍳'CQ'` is `3 4`). Matching is exact.

Monadic `⍳` with a vector of two or more numbers is an addition: it
gives a matrix with one row for every index of an array of that shape,
in order. `⍳2 3` is a 6 by 2 matrix, rows `1 1`, `1 2`, `1 3`, `2 1`,
`2 2` and `2 3`.

`L∊R`

Membership: `1` for each item of `L` found in `R`. There's no monadic
`∊`.

`⍋R`

`⍒R`

Grade up, grade down: the indices that would sort `R`. Works on
numbers and on characters, which sort by character code. No dyadic
forms.

`R[I]`

`R[I;J]`

Indexing, and indexed assignment (`V[2]←9`). An index is a whole
number or a vector of them (`V[3 1]`); leaving one out of a matrix index selects every row or
column (`M[1;]`). Assigning a value that doesn't fit the array's
current storage (a float into an integer array) widens it.

`L⌷R`

Squad, here a gather: `L` is a matrix with one row of coordinates for
each item wanted, one column for each axis of `R`, and the result is a
vector of those items. `(2 2⍴1 2 2 3)⌷M` gives `M[1;2]` and `M[2;3]`.
This isn't APL2's squad, whose left argument lists indices per axis.

---

## Encode and decode

`L⊤R`

`L⊥R`

Encode (representation) and decode (base value), as in the book,
including a `0` in `L` meaning an unlimited radix (`0 10⊤123` is
`12 3`), negative numbers, and matrix arguments.

---

## Operators

Which functions each operator takes. The scalar functions are the
arithmetic, comparison, logic and bitwise ones above. Any other
function gives a `Syntax Error`, except in outer product, where it's
a `Domain Error` (`1 2∘.⍴3`).

| Operator | Functions |
|---|---|
| reduction `f/` `f⌿` | `+ - × ⌈ ⌊ = ≠ < ≤ ≥ > ∧ ∨` |
| scan `f\` `f⍀` | `+ - × ⌈ ⌊ = ≠ < ≤ ≥ > ∧ ∨` |
| outer product `∘.f` | any dyadic scalar function, and `⌽ ⊖ ,` |
| inner product `f.g`, left (`f`) | `+ - × ⌈ ⌊ = ≠ < ≤ ≥ > ∧ ∨` |
| inner product `f.g`, right (`g`) | any dyadic scalar function |

`f/R`

`f⌿R`

Reduction, along the last axis (`/`) or the first (`⌿`). Reducing an
empty array gives the function's identity. For `⌈` and `⌊` that's the
most negative or most positive whole number the interpreter holds
(`¯2147483648` and `2147483647`), not the book's largest floats.

`f\R`

`f⍀R`

Scan, along the last axis or the first.

`L∘.fR`

Outer product. With `⌽`, `⊖` or `,` each result is an array, so the
result has more axes: `1 2∘.⌽M` gives `M` rotated by 1 and by 2.

`Lf.gR`

Inner product: `M+.×N` is the matrix product, `L∧.=R` tests for equal
rows, `L∨.×R` and others work as in the book.

There's no bracket axis (`+/[1]M`, `⌽[1]M`, `,[1]M` are
`Syntax Error`s). Use the first-axis glyphs `⌿ ⍀ ⊖ ⍪` instead.

---

## Execute and format

`⍎` and `⍕` are still being worked on, and cover only the common uses.

`⍎R`

Execute: runs the character vector `R` as an APL line and gives its
result (`⍎'1+2'` is `3`). The line can call functions, but it can't
be a system command: system commands run only when typed at the
prompt, so `⍎')VARS'` gives `Incorrect Command` and nothing is run,
even when the `⍎` itself is typed at the prompt. An error inside a
function run this way shows `In execute:` with the function and line.

`⍕R`

Format: the display of a number or numeric array, as a character
vector. Meant for numbers only; a character argument gives its
character codes (`⍕'AB'` is `65 66`). No dyadic form.

---

## Assignment, branch and output

`N←R`

Assignment.

`→R`

Branch, inside a function, as in the book. A target that isn't a line
of the function, including a negative one, leaves the function.

`⎕←R`

`⍞←R`

Display `R`. `⎕←` ends with a new line; `⍞←` doesn't, so the next
output continues on the same line.

---

## Differences from the book

The points above where RetroAPL behaves differently, gathered in one
place:

- Scalar functions reuse the shorter of two arguments, of any shape,
  instead of giving a `Length Error` (see the start of this page).
- `∧ ∨ ⍲ ⍱` treat any nonzero number as true.
- Comparisons are exact; there's no `⎕CT`.
- `÷` can do integer division (`)INTDIV`).
- `∩ ∪ ⊂ ⊃` are bitwise and shift functions.
- `⌷` is a gather, not APL2's squad.
- Monadic `⍳` with a vector argument gives a matrix of indices.
- Replicate accepts negative counts (as APL2).
- `⌈/` and `⌊/` of an empty array give integer limits.
- Monadic `⌈` and `⌊` of a whole number change its storage size.
- `⍕` and `⍎` are monadic only, and `⍕` expects numbers.
- There's no bracket axis.
- Names can use `_` and `∆` as well as letters and digits, and either
  can start a name (`∆X`, `MY_VAR`). They can't use the underscored
  letters, or `⍙`.

---

## Not implemented

Forms the book has that RetroAPL doesn't, at present. Each is a
`Syntax Error`.

- Monadic `+` (identity, or conjugate in APL2).
- Factorial and binomial (`!`).
- Monadic `↑` and `↓`, and monadic `∊`.
- Dyadic `~` (APL2's "without"; see Not, above).
- Dyadic `⍋` and `⍒` (grading with a collating sequence).
- Dyadic `⍕` (format with a width and precision).
- Reduction, scan, and the left function of inner product with
  `÷ * ⍟ ○ | ⍲ ⍱ ∩ ∪ ⊂ ⊃`.
- Non-scalar functions in inner product (`,.×`), and in outer
  product other than `⌽ ⊖ ,`.
- Bracket axis.
- Matrix inverse and division, `⌹`.

APL2's nested arrays aren't supported, so neither are the primitives
built on them: enclose and disclose (`⊂ ⊃`, which here are shifts),
match (`≡`), each (`¨`), first, pick, and the rest. `⍺`, `⍵`, `⌶` and
`⍫` are in the font but do nothing.
