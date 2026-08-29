(*
Turbo's own `const` parameter (procedure P(const x: T)) reads like an
ordinary parameter but may not be written to, either as a whole or through
one of its fields -- parallel in spirit to EP's protected value parameter
(err_protected_param_assigned) but its own distinct diagnostic naming
'const' specifically, and its own distinct flag (ParamGroup::IsConst,
Symbol::IsConstParam) rather than folded into IsProtected -- see that
field's own comment (AstType.h) for why a shared flag would not do: const
also changes CodeGen's calling convention for a structured type (see
const-parameter-of-a-structured-type-is-passed-by-reference-not-copied.pas,
this directory's CodeGen twin), which protected never does.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
type
  TPoint = record
    x, y: Integer;
  end;

procedure TakeRec(const r: TPoint);
begin
  r.x := 1;
  r := r;
end;

var v: TPoint;
begin
  v.x := 1; v.y := 2;
  TakeRec(v);
end.

(*
CHECK: assignment to const parameter 'r'
CHECK: assignment to const parameter 'r'
*)
