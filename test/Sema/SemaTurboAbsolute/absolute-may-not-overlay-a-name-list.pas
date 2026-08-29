(*
Real Turbo Pascal writes 'absolute' on a single variable at a time; a
name-list group ('a, b: T absolute x;') has no sensible reading -- would
both a and b overlay x's storage, at the same address?  -- so it is refused
rather than silently guessed at (VarGroup::AbsoluteExpr's own comment,
AstDecl.h).

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'absolute' may overlay only a single variable, not a name list
*)

var
  Target: Integer;
  A, B: Integer absolute Target;
begin
  writeln(A);
end.
