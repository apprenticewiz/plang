(*
Turbo string[N] semantics item, concrete work 2 and 3, at the Sema layer:
comparing two ShortStrings with every relational operator, and indexing at
0, both type-check with no diagnostic.  Sema::checkBinary's comparison case
needs its own ShortString-aware disjuncts (isShortStrLike, alongside --
never in place of -- the pre-existing isStringLike ones, SemaExpr.cpp) or
`s1 < s2` for two ShortStrings would be refused as an incomparable pair
(isVarStringLike is false for ShortString by construction, so neither the
EP-shaped isStringLike predicate nor isAssignCompatible's pre-ShortString
Kind-equality switch would have settled it on their own).
Sema::checkIndex needs its own ShortString arm, separate from VarString's,
to accept index 0 (an ordinary array/VarString index check would reject an
out-of-1..N-range subscript, but ShortString's own bounds -- CodeGen's job,
via RangeCheckGuards -- start at 0, and Sema itself does no bounds-checking
for either dialect's string index, only decides the element type is Char).

RUN: %plang -std=turbo -dump-ast %s > %t.out 2>&1
RUN: FileCheck %s < %t.out
*)

program p;
var
  s1, s2: string[10];
  eq, ne, lt, le, gt, ge: boolean;
  c: char;
begin
  s1 := 'ab';
  s2 := 'cd';
  eq := s1 = s2;
  ne := s1 <> s2;
  lt := s1 < s2;
  le := s1 <= s2;
  gt := s1 > s2;
  ge := s1 >= s2;
  c := s1[0];
  writeln('ok')
end.

(*
CHECK-NOT: error:
CHECK: (call writeln
*)
