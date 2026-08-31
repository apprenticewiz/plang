(*
Turbo Tier 5, Cluster A item 3 regression check: this codebase has no
procedural-valued record field, so 'R.X(...)' was never legal before this
item and must not become a silent no-op or a miscompile now that
'.identifier(' unconditionally builds a MethodCallExpr at parse time (see
MethodCallExpr's own comment, AstExpr.h) -- Sema::checkMethodCall's own
"receiver is not an object" check (err_method_call_receiver_not_object) is
what catches it instead of the parser.
*)

(*
RUN: not %plang_ir -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program RecordFieldFollowedByParens;

type
  TRec = record
    X: Integer;
  end;

var
  R: TRec;

begin
  R.X(1);
end.

(*
CHECK: error: 'TRec' is not an object; '.' followed by '(' is only a method call
*)
