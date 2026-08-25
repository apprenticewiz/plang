(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10
CHECK-NEXT:20
CHECK-NEXT:30
*)

program p;
procedure outer;
  var arr: array [1..3] of integer;
  procedure fill;
  var i: integer;
  begin for i := 1 to 3 do arr[i] := i * 10 end;
  procedure show;
  var i: integer;
  begin for i := 1 to 3 do writeln(arr[i]) end;
begin
  fill;
  show
end;
begin outer end.
