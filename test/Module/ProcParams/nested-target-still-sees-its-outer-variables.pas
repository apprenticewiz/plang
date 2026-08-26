(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:105
CHECK-NEXT:205
*)

program p;
procedure outer;
var base: integer;
  function addbase(x: integer): integer;
  begin addbase := x + base end;
  function ap(function f(x: integer): integer; v: integer): integer;
  begin ap := f(v) end;
begin
  base := 100; writeln(ap(addbase, 5));
  base := 200; writeln(ap(addbase, 5))
end;
begin outer end.
