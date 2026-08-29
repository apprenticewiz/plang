(*
SizeOf(x) and High(x)/Low(x) answer purely from x's own static TYPE --
exactly the unevaluated-operand rule C's own `sizeof` follows -- so x's
side effects, if it has any, must never run.  CGFuncCall's own SizeOf/
High/Low lowering never calls EmitExpr on the argument at all, only reads
back the Sema-resolved type Args[0]->ResolvedType already carries; this
file is the regression proof: F below increments a counter every time it
is actually called, and SizeOf(F)/High(F) here -- F used bare, meaning F's
own result type, exactly the way a parenthesis-free function name inside
SizeOf(...)/High(...) is read -- must leave that counter at 0.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2
CHECK-NEXT:0
CHECK-NEXT:32767
CHECK-NEXT:0
*)

program p;
var
  calls: Integer;

  function F: Integer;
  begin
    calls := calls + 1;
    F := calls;
  end;

begin
  calls := 0;
  writeln(SizeOf(F)); { SizeOf(F's own return type, integer) -- F itself must not run }
  writeln(calls);
  writeln(High(F));   { High(F's own return type's range) -- F itself must not run }
  writeln(calls);
end.
