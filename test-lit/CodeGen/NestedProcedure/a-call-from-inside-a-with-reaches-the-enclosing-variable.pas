(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7 900
*)

program p(output);
type rec = record n: integer end;
var r: rec;
procedure outer;
var n: integer;
  procedure bump; begin n := n + 1 end;
begin
  n := 5; r.n := 900;
  with r do begin bump; bump end;
  writeln(n, ' ', r.n)
end;
begin outer end.
