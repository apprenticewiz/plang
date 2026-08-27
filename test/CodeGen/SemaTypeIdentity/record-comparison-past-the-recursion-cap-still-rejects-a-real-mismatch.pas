(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(* Regression test for issue #171: isAssignCompatible's record-vs-record
   structural comparison gives up at recursion depth 16 and, on hitting the
   cap, used to return true ("compatible") rather than false.  Two records
   nested 17 levels deep are identical wrappers down to that depth and then
   diverge completely (a 1-field integer record vs. a 2-field boolean/char
   record) at the level the cap reaches first, so a correct comparison must
   still reject the assignment instead of giving up and accepting it. *)

program p;
var
  x: record f: record f: record f: record f: record f: record f: record f: record f: record f: record f: record f: record f: record f: record f: record f: record f: record f: record a: integer end end end end end end end end end end end end end end end end end end;
  y: record f: record f: record f: record f: record f: record f: record f: record f: record f: record f: record f: record f: record f: record f: record f: record f: record f: record b: boolean; c: char end end end end end end end end end end end end end end end end end end;
begin
  x := y
end.

(*
CHECK: cannot assign
*)
