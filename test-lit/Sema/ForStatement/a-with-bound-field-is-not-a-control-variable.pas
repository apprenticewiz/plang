(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p(output);
type rec = record i: integer end;
var rr: rec; s: integer;
begin s := 0; with rr do for i := 1 to 3 do s := s + i end.

(*
CHECK: must be declared in the immediately enclosing block
*)
