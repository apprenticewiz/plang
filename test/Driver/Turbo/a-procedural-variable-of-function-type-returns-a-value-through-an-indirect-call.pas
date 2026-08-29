(*
Turbo procedural VALUES cover functions as well as procedures --
TypeKind::Function, not just TypeKind::Procedure.  f is a variable of a
FUNCTIONAL type, assigned two different top-level functions in turn, and
each indirect call's result is used exactly like an ordinary function
call's would be (assigned to a variable and written out), confirming
ClosureAndCallABI::emitProcVarCall's return-value handling works, not just
its argument-passing.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:25
CHECK-NEXT:30
*)

program p;

type
  TIntFn = function(x: integer): integer;

var
  f: TIntFn;
  r: integer;

function Square(x: integer): integer;
begin
  Square := x * x;
end;

function AddTen(x: integer): integer;
begin
  AddTen := x + 10;
end;

begin
  f := Square;
  r := f(5);
  writeln(r);
  f := @AddTen;
  r := f(20);
  writeln(r);
end.
