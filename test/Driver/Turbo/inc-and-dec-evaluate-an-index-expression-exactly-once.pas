(*
Inc(x)/Dec(x) has to be a single read-modify-write through x's address,
computed exactly ONCE -- CGProcCall's own Inc/Dec lowering takes x's
address via EmitLValue and then reads the current value back through THAT
SAME address, rather than also calling EmitExpr(x) separately (which would
re-walk x's own AST, e.g. re-running a function call inside an index
expression, a second time). `Inc(arr[F])` below is the regression case: if
x's index expression were evaluated twice, F would run twice and this
would print 2/11 instead of the correct 1/11.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
CHECK-NEXT:11
*)

program p;
var
  arr: array[1 .. 5] of Integer;
  calls: Integer;

  function F: Integer;
  begin
    calls := calls + 1;
    F := calls;
  end;

begin
  calls := 0;
  arr[1] := 10;
  Inc(arr[F]);
  writeln(calls);
  writeln(arr[1]);
end.
