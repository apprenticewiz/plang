(*
EP non-regression (Tier 2 capstone): assigning a value longer than an EP
VarString's own declared capacity is still a runtime ERROR (ISO 10206
Sec6.9.2.2), unaffected by Turbo's ShortString work landing a deliberately
OPPOSITE rule for its own `string[N]` (silent truncation, no error at all
-- see docs/turbo.md's "Assignment: truncates, never errors" section).
The two runtimes (plang_str.cpp for EP, plang_sstr.cpp for Turbo) share no
code for this operation, so this is a real pin, not merely a restatement
of Tier 2's own documentation.  The over-long value has to arrive through a
VARIABLE (u), not a literal directly assigned to s -- a literal too long
for its destination is instead a COMPILE-TIME error (see the sibling
test/EP/StringCapacity/an-over-long-literal-is-rejected-at-compile-time.pas),
a separate check this test isn't about; assigning a value plang can't know
is too long until run time is what actually exercises the RUNTIME capacity
check Turbo's own silent-truncation rule bypasses entirely.
*)

(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: assigned to a string(3)
*)

program p(output);
var
  s: string(3);
  u: string(10);
begin
  u := 'abcdef';
  s := u;
  writeln(s)
end.
