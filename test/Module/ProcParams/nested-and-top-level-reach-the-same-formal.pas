(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:6
CHECK-NEXT:53
*)

program p;
function ap(function f(x: integer): integer; v: integer): integer;
begin ap := f(v) end;
function dbl(x: integer): integer; begin dbl := x * 2 end;
procedure outer;
var base: integer;
  function addbase(x: integer): integer;
  begin addbase := x + base end;
begin base := 50; writeln(ap(dbl, 3)); writeln(ap(addbase, 3)) end;
begin outer end.
