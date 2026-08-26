(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:123
*)

program p;
function ap(function f(x: integer): integer; v: integer): integer;
begin ap := f(v) end;
procedure l1;
var a: integer;
  procedure l2;
  var b: integer;
    function deep(x: integer): integer;
    begin deep := x + a + b end;
  begin b := 20; writeln(ap(deep, 3)) end;
begin a := 100; l2 end;
begin l1 end.
