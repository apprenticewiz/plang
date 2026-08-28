(*
TP-only Continue (Builtins.def) is refused with no enclosing loop, the same
way Break is -- here from inside an ordinary procedure that itself has no
loop of its own (only its CALLER might, and that is irrelevant: Continue
means the innermost loop enclosing ITS OWN text, not whatever the caller
happens to be inside).
*)

(*
RUN: not %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'continue' is not valid outside a while, for, for-in or repeat loop
*)

program p;
procedure NoLoopHere;
begin
  Continue
end;
begin
  NoLoopHere
end.
