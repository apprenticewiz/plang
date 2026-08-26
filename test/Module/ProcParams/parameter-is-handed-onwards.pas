(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:306
*)

program p;
function inner(function g(x: integer): integer; v: integer): integer;
begin inner := g(v) end;
function outerf(function f(x: integer): integer; v: integer): integer;
begin outerf := inner(f, v) + 1 end;
procedure hold;
var bias: integer;
  function add(x: integer): integer; begin add := x + bias end;
begin bias := 300; writeln(outerf(add, 5)) end;
begin hold end.
