(*
Tier 2 capstone: the single comparison-ordering pair most likely to be
misremembered between plang's two bounded-string types -- 'a' < 'a ' is
TRUE for a Turbo ShortString, and 'a' = 'a ' is TRUE for an EP VarString --
same two literals, opposite relational answer, for opposite reasons.  This
is the Turbo half; the paired EP half (same literals, same shape of
program) is
test/EP/Tier2NonRegression/a-equals-a-space-is-still-true-for-ep-varstrings-paired-with-turbos-opposite-answer.pas,
which cross-references this file directly.

ShortString compares by PREFIX lexicographic order with the SHORTER string
treated as less whenever one is a strict prefix of the other (sstrCmp,
plang_sstr.cpp): 'a' and 'a ' share the prefix 'a', and 'a' is shorter, so
'a' < 'a '.  Consequently 'a' <> 'a ' (they are UNEQUAL) and 'a' <= 'a '
(true for the same reason 'a' < 'a ' is).  See docs/turbo.md's ShortString
section ("Comparison: prefix order, shorter is less") for the full
contrast with EP's own space-padded rule.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:TRUE
CHECK-NEXT:FALSE
CHECK-NEXT:TRUE
CHECK-NEXT:TRUE
*)

program shortstring_prefix_order;
var
  a, b: string[10];
begin
  a := 'a';
  b := 'a ';
  writeln(a < b);
  writeln(a = b);
  writeln(a <> b);
  writeln(a <= b);
end.
