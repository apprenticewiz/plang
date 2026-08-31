(*
CGIndexAccess::emitIndexGEP widens an index expression to i64 through
ToI64 with no signedness argument -- the same pre-ladder "guess zero-
extend for any i8" fallback CGAssign's plain-assignment path used to fall
into (see shortint-assigned-to-a-wider-signed-integer-sign-extends-
issue-177.pas).  A negative ShortInt index's zero-extended bit pattern is
a large positive number, so `arr[idx]` with idx a negative ShortInt landed
on a GEP offset hundreds of elements away from the intended (legally
negative-indexed) one instead of failing its range check or reaching the
right element (issue #177's sibling audit).  Fixed by consulting the
index expression's own Sema-resolved signedness (exprIsSigned,
OrdinalSignedness.h).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:99
*)

program p;
var
  arr: array[-5..5] of Integer;
  idx: ShortInt;
begin
  idx := -3;
  arr[idx] := 99;
  writeln(arr[-3])
end.
