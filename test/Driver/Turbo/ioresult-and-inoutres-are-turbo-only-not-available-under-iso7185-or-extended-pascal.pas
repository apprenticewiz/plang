(*
Regression gate: IOResult (Builtins.def, Dialects=TP, Func, 0 args) is a
required identifier refused BY NAME under -std=iso7185/-std=iso10206
(err_turbo_required_name's wording, "is a Turbo Pascal extension and is
only available under -std=turbo" -- the same message RunError/Assert/
Random already get -- see
random-randomize-int-frac-and-randseed-are-turbo-only-not-available-under-
iso7185-or-extended-pascal.pas, this file's own direct model), while
InOutRes (a predefined Var registered exactly the way RandSeed is -- see
Sema::registerBuiltins' InOutRes Symbol) is never declared at all outside
Turbo and so is an ordinary undefined identifier instead.

IOResult's BARE (no-parens) spelling and its explicit-call spelling
IOResult() BOTH need checking here, not assumed to transfer from
Random/ParamCount's own precedent: Sema::checkIdent's generic
SymbolKind::Builtin case (the bare form's path) needed its own dedicated
checkEPOnly call added specifically because Random's bare form was
originally missed by it (see that file's own comment) -- so a newly
registered zero-argument TP-only Func has to be checked against BOTH
paths independently, not assumed correct by analogy.
*)

(*
RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK-DAG: 'ioresult' is a Turbo Pascal extension and is only available under -std=turbo
CHECK-DAG: 'ioresult' is a Turbo Pascal extension and is only available under -std=turbo
CHECK-DAG: undefined identifier 'InOutRes'
*)

program p;
var
  code: Integer;
begin
  code := IOResult;
  code := IOResult();
  InOutRes := 0
end.
