(*
Issue #773 companion: the fallback that lets a bare (no-parens) identifier or
qualified field access resolve to a parameterless FUNCTION method
(Sema::checkImplicitMethodIdent for the bare, unqualified spelling;
checkField's own findObjectMethod fallback for the qualified 'S.Method'
one) still refuses the two shapes a paren-free spelling cannot mean: a
PROCEDURE method (no result to read at all -- err_proc_as_value, the same
diagnostic a plain top-level procedure's bare name already gets) and a
FUNCTION method that takes parameters (a bare spelling has no argument-list
syntax to supply them with -- err_function_requires_args, the same
diagnostic a plain top-level function taking arguments already gets bare).
One of each shape, exercised both from INSIDE a sibling method (the
unqualified spelling) and from OUTSIDE any method (the qualified one).

RUN: not %plang_ir -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program BareImplicitMethodRefusesProcAndParamFunc;

type
  TShape = object
    procedure DoIt;
    function Sum(x: Integer): Integer;
    procedure UseBareInside;
  end;

procedure TShape.DoIt;
begin
end;

function TShape.Sum(x: Integer): Integer;
begin
  Sum := x;
end;

procedure TShape.UseBareInside;
var
  a, b: Integer;
begin
  a := DoIt;
  b := Sum;
end;

var
  S: TShape;
  c, d: Integer;

begin
  c := S.DoIt;
  d := S.Sum;
end.

(*
CHECK: error: procedure 'DoIt' cannot be used as a value
CHECK: error: function 'Sum' requires an argument list
CHECK: error: procedure 'DoIt' cannot be used as a value
CHECK: error: function 'Sum' requires an argument list
*)
