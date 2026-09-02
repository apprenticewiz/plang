(*
Issue #578: SizeOf/High/Low never evaluate their argument as an expression
-- a value argument is only ever asked for its static type (docs/turbo.md:
"SizeOf(arr[F]) does not call F"), the same unevaluated-operand rule C's
own sizeof follows. flowRead (SemaFlow.cpp) walked every CallExpr's Args
uniformly via the generic walkExprs helper, so a bare variable named as
SizeOf/High/Low's argument was reported as an ordinary read -- a false
"is read here before it has been given a value" on a variable never
actually read at that point.
*)

(*
RUN: %plang -std=turbo -Wall %s -o %t 2> %t.err
RUN: %run %t > %t.out
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
RUN: FileCheck %s < %t.out
*)

(*
ERR-ABSENT-NOT: is read here before it has been given a value

CHECK: 2
CHECK: 65535
CHECK: 0
*)

program p;
var
  x: Integer;
  y: Word;
begin
  writeln(SizeOf(x));
  writeln(High(y));
  writeln(Low(y));
end.
