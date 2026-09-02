(*
Issue #773 companion: 'S.Area' resolving to an implicit method call
(Sema::checkField's own IsImplicitMethodCall flag) is a function RESULT, not
a variable -- Sema::isLValue's own FieldExpr case refuses it as an
assignment target exactly like the bare, unqualified 'Area := x' spelling
already is (Symtab.lookup finds no Var/VarParam Symbol for a method name at
all). Without that isLValue guard this would reach CodeGen's ordinary field
GEP looking for a field named 'Area' that does not exist and crash with an
internal compiler error instead of this clean diagnostic.

RUN: not %plang_ir -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program ImplicitMethodCallNotAnLvalue;

type
  TShape = object
    function Area: Real;
  end;

function TShape.Area: Real;
begin
  Area := 1.0;
end;

var S: TShape;

begin
  S.Area := 3.0;
end.

(*
CHECK: error: left-hand side of assignment is not an assignable variable
*)
