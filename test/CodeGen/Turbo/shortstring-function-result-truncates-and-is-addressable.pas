(*
Turbo string[N] semantics item: return-value marshaling.  A function may
declare a string[N] result (Sema::checkProcHeading's "Simple" result-type
gate, Sema.cpp, gets a narrow Turbo-only ShortString carve-out alongside
its existing ordinal/numeric/pointer one, alongside EP's own separate
blanket carve-out for any assignable type) and its RESULT comes back as the
whole packed struct by value, spilled to an addressable temporary the same
way a VarString result already was (CGFuncCall::emitUserFuncCall's own
ShortString sibling branch to its VarString one).  Confirms the result
truncates like any other ShortString assignment, and that both a
variable-assigned result and a directly-indexed call-expression result
(makeit[1], with no intervening variable) work -- the latter needs the
call's return value to already be addressable by the time indexing reaches
it, with no separate temporary of the caller's own.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
function makeit: string[6];
begin
  makeit := 'abcdefgh';
end;
var s: string[6];
begin
  s := makeit;
  writeln(s, ' len=', ord(s[0]));
  writeln(makeit[1], makeit[6]);
end.

(*
CHECK:abcdef len=6
CHECK-NEXT:af
*)
