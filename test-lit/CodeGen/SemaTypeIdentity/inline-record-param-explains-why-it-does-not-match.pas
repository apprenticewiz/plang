(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: two distinct types
*)

program p;
var r: record a: integer end;
procedure w(var q: record a: integer end);
begin q.a := 5 end;
begin w(r) end.
