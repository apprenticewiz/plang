(*
Issue #791's own guard-rail case: unlike a method PARAMETER (companion
files: -method-parameter-...pas), a FIELD of an object type naming that
SAME type by value (`Y: TFoo` inside TFoo) must keep being refused -- a
value field of the enclosing type would give TFoo infinite size, a genuine
layout violation and not the over-broad forward-reference check the
parameter case turned out to be. The fix's own guard
(InSelfObjectParamPosition_, Sema.h) is scoped to parameter-type resolution
only, specifically so this case is unaffected. Confirmed against a local
`fpc -Mtp` build, which also rejects this ("Type size not yet known").

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: used here before its declaration
*)

program SelfFieldStillErrors;
type
  TFoo = object
    X: Integer;
    Y: TFoo;
  end;
begin
end.
