(*
The exact combined comparison neither existing boolean-evaluation test makes:
boolean-and-under-b-minus-does-not-evaluate-the-right-operand.pas proves
Turbo's default short-circuits, alone, under -std=turbo only; iso7185-and-
extended-pascal-always-evaluate-both-boolean-operands.pas proves ISO 7185/EP
always evaluate both, alone, under -std=iso7185/-std=iso10206 only.  Neither
compiles the SAME source both ways in one file, so neither is a direct
witness that changing only -std -- nothing else about the program -- flips
this behavior.  This file is that witness: one source, two RUN lines
differing only in -std, two expected outputs checked side by side.

No dollar-B directive anywhere in the source below: Turbo's own short-circuit
default needs none (real Turbo Pascal ships with it that way already), the
same way -std=turbo alone already starts with range checking off -- see
range-checks-default-off-under-turbo-lets-an-out-of-range-write-through.pas
for that sibling default.  So what decides the outcome here is -std alone,
exactly the point.

NOTE: this comment deliberately avoids writing a literal brace-delimited
dollar-directive (e.g. dollar-B-minus) anywhere in this prose block: under
ISO 7185, the left and right curly brace are alternate spellings of the SAME
comment delimiters this paren-star comment uses, and comments do not nest,
so a bare closing brace in running prose would close this very comment early
and feed the rest of the paragraph to the parser as code.  -std=turbo alone
would not have this problem (Turbo requires a comment to close with the same
*kind* of delimiter it opened with), but this file is compiled under
-std=iso7185 too.
*)

(*
RUN: %plang -std=turbo %s -o %t.turbo
RUN: %run %t.turbo | FileCheck --check-prefix=TURBO --strict-whitespace --match-full-lines %s

RUN: %plang -std=iso7185 %s -o %t.iso
RUN: %run %t.iso | FileCheck --check-prefix=ISO --strict-whitespace --match-full-lines %s
*)

(*
TURBO:b=FALSE
ISO:SIDEEFFECT CALLED
ISO-NEXT:b=false
*)

program p;
var b: boolean;

function SideEffect: boolean;
begin
  writeln('SIDEEFFECT CALLED');
  SideEffect := true
end;

begin
  b := false and SideEffect;
  writeln('b=', b)
end.
