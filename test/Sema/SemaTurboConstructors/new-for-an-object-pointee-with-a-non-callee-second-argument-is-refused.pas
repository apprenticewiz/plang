(*
Turbo Tier 5, Cluster A item 6: 'new(p, 5)' -- the second argument to
'new' for an object pointee must be a constructor callee (a bare
identifier or a CallExpr), not an arbitrary expression.
*)

(*
RUN: not %plang_ir -std=turbo %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
type
  PA = ^TA;
  TA = object
    constructor Init;
  end;

constructor TA.Init;
begin
end;

var P: PA;
begin
  New(P, 5);
end.

(*
CHECK: error: 'new' for a pointer to object type 'TA' needs a constructor call, as in 'new(p, Init(...))'
*)
