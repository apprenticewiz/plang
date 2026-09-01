(*
Issue #685: EP §6.8.7's structured-value-constructor is an EXPRESSION, not
a variable-access -- §6.4.3's field-designator production selects from a
variable-access, and a constructed value was never one. Before this
diagnostic (err_selector_on_structured_value) existed to catch it in
Sema, `Point[x:5;y:6].y` reached CodeGen's emitFieldGEP, which asks
EmitLValue for the record operand's address; emitLValue has no
StructuredValueExpr case, returns null, and the whole compile aborted with
an internal CodeGen error ("field access 'y' on a non-record operand")
instead of a clean, ordinary diagnostic.
*)

(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: cannot select a component of a structured-value-constructor result
*)

program p;
type Point = record x, y: integer end;
begin
  writeln(Point[x: 5; y: 6].y)
end.
