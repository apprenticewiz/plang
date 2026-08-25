(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1007
*)

program p;
function ap(function f(x: integer): integer; v: integer): integer;
begin ap := f(v) end;
procedure outer;
var base: integer;
  function addbase(x: integer): integer;
  begin addbase := x + base end;
begin base := 1000; writeln(ap(addbase, 7)) end;
begin outer end.
