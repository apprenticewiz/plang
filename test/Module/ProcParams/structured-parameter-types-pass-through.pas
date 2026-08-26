(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
CHECK-NEXT:6
CHECK-NEXT:hello
*)

program p;
type pt = record x, y: integer end;
     vec = array[1..3] of integer;
var a: pt; v: vec;
procedure showpt(q: pt); begin writeln(q.x + q.y) end;
procedure showvec(q: vec); begin writeln(q[1] + q[2] + q[3]) end;
procedure shows(s: string); begin writeln(s) end;
procedure runpt(procedure s(q: pt); w: pt); begin s(w) end;
procedure runvec(procedure s(q: vec); w: vec); begin s(w) end;
procedure runs(procedure s(t: string)); begin s('hello') end;
begin
  a.x := 3; a.y := 4; runpt(showpt, a);
  v[1] := 1; v[2] := 2; v[3] := 3; runvec(showvec, v);
  runs(shows)
end.
