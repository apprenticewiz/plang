(*
Issue #711.  `Sema::protectedBaseOf` walks a write target's IndexExpr and
FieldExpr chain down to the identifier it bottoms out at, so `r.f := x` and
`r[i] := x` on a `const` parameter are both refused
(const-parameter-rejects-whole-and-field-assignment.pas, this directory's
sibling) -- but it never walked a TypeCastExpr, so a same-size Turbo
VARIABLE typecast (`isLValue`'s own comment, SemaExpr.cpp, has the exact
rule) walked straight off the end of the loop and came back with no symbol
at all: `TOther(r).field := x` reinterprets `r`'s own storage in place
(TypeCastExpr's own comment, AstExpr.h) exactly as `r.field := x` would if
`r` were declared `TOther` to begin with, so the two must be refused
identically, and were not.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
type
  TPoint  = record x, y: Integer end;
  TPoint2 = record a, b: Integer end;

procedure Mutate(const r: TPoint);
begin
  TPoint2(r).a := 0;
end;

var v: TPoint;
begin
  v.x := 1; v.y := 2;
  Mutate(v);
end.

(*
CHECK: assignment to const parameter 'r'
*)
