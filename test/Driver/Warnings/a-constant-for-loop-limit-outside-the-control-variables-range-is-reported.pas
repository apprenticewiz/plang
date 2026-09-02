(*
Issue #654: a for-loop bound that is a compile-time constant provably
outside the control variable's declared ordinal range compiled silently
(not even warnIfConstantOutOfRange -- the same check assignment and
Exit/function-result already use for exactly this "right type, wrong
constant value" gap -- ever looked at a for-loop's own From/Limit) and
misbehaved at run time instead. checkFor (SemaStmt.cpp) now runs the same
check on both bounds once they are known assignment-compatible.
*)

(*
RUN: %plang %s -o %t 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 15 is outside the range 1..10
*)

program p(output);
var i: 1..10;
begin
  for i := 1 to 15 do writeln(i)
end.
