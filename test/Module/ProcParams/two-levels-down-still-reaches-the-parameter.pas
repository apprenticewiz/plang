(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:700
*)

program p;
function ap(function f(x: integer): integer; v: integer): integer;
  function l1(y: integer): integer;
    function l2(z: integer): integer;
    begin l2 := f(z) * 100 end;
  begin l1 := l2(y) end;
begin ap := l1(v) end;
procedure outer;
var base: integer;
  function addbase(x: integer): integer;
  begin addbase := x + base end;
begin base := 3; writeln(ap(addbase, 4)) end;
begin outer end.
