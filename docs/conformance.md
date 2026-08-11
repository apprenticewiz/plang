# Conformance to ISO/IEC 7185

ISO/IEC 7185 clause 5.1 asks a processor to be accompanied by documents that
define its implementation-defined features and describe its extensions. This is
those documents.

## Compliance statement

> plang complies with the requirements of level 1 of ISO/IEC 7185.

Clause 5.1 defines two levels and no others. Level 0 excludes conformant array
parameters (§6.6.3.6 e), §6.6.3.7 and §6.6.3.8); level 1 includes them. plang
accepts them, in both the `array` and the `packed array` form, under
`-std=iso7185`.

The statement above is the one clause 5.1 prescribes for a processor that
complies in all respects. Where a departure is found it belongs in the list
below and the statement becomes the "with the following exceptions" form; the
list is empty as this is written.

That is a claim about what is known, not a proof, and the two are worth keeping
apart. No validation suite has certified this processor. What the statement
rests on is the testing described next, and every round of it so far has found
something — which is the honest reason to expect the list not to stay empty,
rather than a reason to think it should be longer today than it is.

What is known is bounded by what is tested. Two suites bear on the statement,
and they ask different questions. `test/Conformance` is 377 programs from
Pascal-P5, each one construct written wrongly, and it asks whether a violation
is caught. `test/Acceptance` is the Pascal Acceptance Test from the same suite:
one standard program of three thousand lines that uses nearly all of the
language at once, run and compared against expected output. Passing the first
suite entire is consistent with failing to compile ordinary programs, which is
what the second was adopted to find out, and did.

Neither reaches a rule that no test program was written around. Both suites are
programs somebody chose to write, and the standard has more "shall" in it than
they cover: reading it clause by clause and trying what each clause forbids
found seven requirements this processor was not enforcing at all — assignment
of a file variable (§6.8.2.2), two case arms sharing a label (§6.8.3.5), two
variants sharing one (§6.4.3.3), a function returning a structured type
(§6.6.2), and `writeln`, `page` and `eoln` applied to a file that is not a text
file (§6.9.5, §6.9.4, §6.6.6.5). Each is enforced now and has a test in
`test/Driver`, but the way they were found is the point: a suite scores what it
asks about and says nothing about what it does not.

Two of the seven are worth noting for the shape they had. The case-statement
rule and the variant-part rule are the same requirement written twice in the
standard, and enforcing one is no help with the other. `readln` was checked
against the text-file rule while `writeln`, `page` and `eoln` were not. A rule
that holds of a family tends to be implemented for whichever member came up
first, so the others are where to look next.

The expected output is audited two ways, because the first way was not enough.
The program prints beside each of its 743 checks the result it should give, and
every check now either matches that or differs only in what this document
records as implementation-defined: the number of exponent digits (§6.9.3.4.1),
the accuracy of real arithmetic (§6.7.2.2), and the range of integer
(§6.4.2.2). Ten checks are annotated with text that differs from the correct
result in spacing, and those were read against the clause instead.

The second audit compares the whole output against the one the Pascal-P5
interpreter produces, which is distributed with the suite. That output cannot
serve as the expected file — it comes from an implementation with 32-bit
integers, single-precision reals, and booleans written with a capital letter,
which is what the first two of those differences are — but where it disagrees
for any other reason, one of the two is wrong. It found four defects that the
annotations had not: a string written into a field narrower than itself was not
truncated (§6.9.3.6), on the standard output and on a file; reading a text file
character by character yielded the newline it is stored with rather than the
space §6.4.3.5 gives a line marker; a file built with `write` and no closing
`writeln` came back a line short of what was written; and `eof` was false on a
file open for writing (§6.6.5.2). An annotation is written by hand and can be
wrong or vague; a second implementation disagrees for a reason.

## Implementation-defined features

Every feature ISO/IEC 7185 leaves to the implementation, in the order the
standard raises them.

### §6.1.7 — the characters a string-element may be drawn from

Any character with an ordinal number in 0..255 other than the apostrophe, which
is written twice to denote itself, and the line terminator, which ends a line
rather than a string.

### §6.1.9 — the lexical alternatives

Both the reference representations and the alternatives are provided, and the
two spellings of a token are not distinguished:

| reference | alternative |
|-----------|-------------|
| `[`       | `(.`        |
| `]`       | `.)`        |
| `{`       | `(*`        |
| `}`       | `*)`        |

Either terminator closes either comment, as §6.1.8 provides: a comment opened
with `{` may be closed with `*)`, and one opened with `(*` with `}`.

The alternative token `@` for `^`, whose provision §6.1.9 leaves open, is **not**
provided. `@` is not a character of any token.

### §6.4.2.2 b) — the values of the real-type

IEEE 754 binary64, as the machine provides it: 53 bits of significand, decimal
exponents from about −308 to +308. A `real` occupies 8 bytes.

### §6.4.2.2 d) — the values of the char-type, and their ordinal numbers

The 256 values with ordinal numbers 0..255, each the corresponding octet. The
first 128 are ASCII, so `ord('A')` is 65, `ord('a')` is 97 and `ord('0')` is 48,
and the letters and the digits are each consecutive and in order as §6.4.2.2 d)
requires. A `char` occupies one byte.

### §6.4.3.5 — the characters prohibited from textfiles

None. Every value of the char-type may be written to a textfile and read back,
the line terminator included, though a line terminator written as a character
ends the line it is written to.

### §6.6.5.2 — when the activities implied by `get` and `put` are performed

A textfile or a file bound to an external entity is a C stream, so a `put`
places its component in that stream's buffer and the buffer reaches the entity
when it fills, when the file is closed, or when the program ends normally. A
file with no external entity is held in temporary storage that is discarded when
the program ends.

### §6.7.2.2 — `maxint`

9223372036854775807, which is 2^63 − 1. An `integer` is a two's-complement
64-bit value, so the least is −9223372036854775808.

### §6.7.2.2 — the accuracy of real arithmetic

The accuracy of IEEE 754 binary64 as the machine provides it: the four
operators are correctly rounded to nearest, ties to even. The required real
functions are those of the platform's C library and are not guaranteed
correctly rounded.

### §6.9.3.1 — the default `TotalWidth`

| type    | default          |
|---------|------------------|
| integer | as many characters as the value needs, and no padding |
| real    | 22               |
| Boolean | as many characters as `true` or `false` needs, and no padding |

A real written at the default width therefore reads

```
 1.00000000000000e+000
```

which is the 22 characters §6.9.3.4.1 lays out: the sign, a digit, the point,
14 decimal places, the exponent character, its sign and three digits.

### §6.9.3.4.1 — `ExpDigits` and the exponent character

`ExpDigits` is 3 and the exponent character is `e`. Three digits cover the whole
range binary64 reaches, so no value is written in a shape other than the one
described here.

Note that `TotalWidth` sets how many decimal places are written and not how much
padding precedes them: `DecPlaces` is `TotalWidth - ExpDigits - 5`, so a wider
field is a more precise one. A `TotalWidth` below `ExpDigits + 6`, the narrowest
the representation fits in, is raised to it.

### §6.9.3.5 — the case of `true` and `false`

Lower case throughout: `true` and `false`.

### §6.9.4 — the effect of `page`

A form feed, `chr(12)`, is written to the file.

### §6.10 — the binding of a file program-parameter to an external entity

`input` and `output` are bound to the standard input and the standard output.

No other program-parameter is bound to anything by being named in the program
heading. A file variable acquires an external entity by being named in `reset`
or `rewrite` — `reset(f, 'data.txt')` — or, under `-std=iso10206`, through the
`bind` procedure of Extended Pascal §6.7.5.6. A file that has acquired none and
is `reset` or `rewritten` is held in temporary storage for the run.

## Implementation-dependent features

Clause 5.1 i) asks that the processor be able to process any use of an
implementation-dependent feature in a manner similar to an error. plang does not
report these; the note to clause 5.1 permits a switch for the purpose and there
is not one yet. A program that relies on any of them does not conform (clause
5.2 c)), and what plang does with it today is:

| feature | plang |
|---------|-------|
| §6.5.3.2 the order of evaluating an indexed variable's index expressions and accessing the array | left to right, array first |
| §6.7.1 the order of evaluating a set-constructor's member-designators | left to right |
| §6.7.2.1 the order of evaluating the operands of a dyadic operator | left to right |
| §6.7.3 the order of evaluating, accessing and binding actual parameters | left to right |
| §6.8.2.2 whether an assignment's target reference is established before its expression is evaluated | the reference is established first |
| §6.9.2 whether `read` on a textfile takes `f^` or `f.R.first` | `f^` |
| §6.4.3.5 the effect of a prohibited character in a textfile | none are prohibited |
| §6.9.4 the effect of inspecting a textfile `page` was applied to | the form feed is read back as a character |

Neither `and` nor `or` is short-circuited: §6.7.2.2 makes both operands of a
dyadic Boolean operator subject to evaluation, and which are evaluated is not
left open by the standard.

## How errors are treated

Clause 5.1 f) asks that each violation designated an *error* be reported when
the program is prepared, or reported when it runs, or listed here as unreported.

Reported when the program is prepared: every violation the standard does not
designate an error, which is to say everything a compiler can see — the type
rules, the scope rules, the declaration rules, and the syntax.

Reported when the program runs, with a message on the standard error and an
exit status of 70:

- an array index outside the index type (§6.5.3.2)
- a value outside the subrange it is assigned to (§6.6.3.2, §6.8.2.2)
- a `case` statement whose selector matches no case-constant and which has no
  `otherwise` part (§6.8.3.5)
- division by zero, whether by `/`, `div` or `mod` (§6.7.2.2)
- dereferencing a pointer whose value is `nil` (§6.5.4)

The index and subrange checks are the ones `-fno-range-checks` turns off, and
the nil check is what `-fno-nil-checks` turns off; the two were one flag until
0.1.2. A program compiled with either is not one whose errors are reported, and
the flag is a statement by whoever compiles it that the program is known not to
commit them.

**Not reported.** Clause 5.1 f) 1) requires these be listed:

- a variable that is read before it has been given a value (§6.5.1). Its value
  is whatever the storage held.
- an integer operation whose result is outside the integer-type (§6.7.2.2).
  It wraps, two's-complement.
- a real operation whose result is outside the real-type. It gives an IEEE
  infinity or a NaN.
- `succ` or `pred` off the end of an ordinal type (§6.6.6.4). The result is the
  ordinal one past the end, reduced to the width the type is held in — so
  `succ(true)` is `false` and `succ(chr(255))` is `chr(0)`, while `succ` of the
  last value of an enumeration gives its ordinal count.
- reading a file that is at its end, or reading one opened for writing
  (§6.6.5.2).
- `dispose` of a pointer whose value was not obtained from `new`, or a
  reference through a pointer to a disposed variable (§6.6.5.3).
- a file whose buffer variable is altered while the file is being read
  (§6.6.5.2).
- the assorted errors of §6.6.5.3 concerning a variant record whose tag field
  is changed while a component of the active variant is in use.

Two of those are warned about where the compiler can see them coming, which
does not change the entry above them. The first — a variable read before it has
a value — is undecidable in general, and the analysis that looks for it gives
up on any block declaring a label, on a variable a nested procedure can reach,
and on a program that has not typechecked. What it reports is a subset, and the
entry stands for the rest. §6.8.3.9's rule that a for-statement leaves its
control variable undefined is the same error arriving by a route the compiler
can follow exactly, so that one is caught whenever the flow is followable at
all.

Two of the errors reported when the program runs are also warned about before
it does, when the value that will trip them is a constant: a division by zero,
and a value assigned outside the subrange it is going to. Neither is promoted
to an error. A statement nothing reaches commits no error, and refusing to
compile a program on account of a `div 0` in a branch that is never taken would
be refusing one the standard admits. `plang -w` turns all of it off and changes
nothing about what the program does.

## Extensions to Pascal as specified by ISO/IEC 7185

Clause 5.1 g) asks that extensions be described separately and in these words.
The following are **extensions to Pascal as specified by ISO/IEC 7185**:

- the whole of ISO 10206 Extended Pascal — schema types, `string(n)`, complex
  numbers, modules, `**`, `substr`, `card`, `halt`, `><`, `for ... in`, the
  `otherwise` part of a case-statement, initial-state specifiers, restricted
  types, `bind`, and the rest. `docs/modules.md` describes the module system;
  the man page lists the smaller ones.
- the non-decimal integer literals `16#ff` and the like.
- the underscore in an identifier, which ISO 10206 §6.1.3 admits and ISO 7185
  §6.1.3 does not.

Clause 5.1 h) asks that the processor be able to process any use of an extension
in a manner similar to an error, and `-std=iso7185` does exactly that: a
construct outside the standard is rejected, and rejected as an error rather than
as a warning. `-std=iso7185` is the default. There is no separate switch for
insisting on the standard because the standard is not relaxed without one.
