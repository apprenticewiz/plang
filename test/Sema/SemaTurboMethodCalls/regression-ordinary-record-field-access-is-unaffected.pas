(*
Turbo Tier 5, Cluster A item 3 regression check: ordinary record field
access -- read, write, and as a call argument -- still parses to a plain
FieldExpr and type-checks with no diagnostic.  Only '.identifier'
immediately followed by '(' is affected by this item's parser change (see
MethodCallExpr's own comment, AstExpr.h); 'R.X' with nothing after it, or
followed by ':=', was never touched.
*)

(*
RUN: %plang_ir -std=turbo -dump-ast %s | FileCheck %s
*)

program RecordFieldAccessRegression;

type
  TRec = record
    X: Integer;
  end;

var
  R: TRec;

begin
  R.X := 5;
  Writeln(R.X);
end.

(*
CHECK: (assign (field R X) 5)
CHECK-NEXT: (call Writeln (field R X))
*)
