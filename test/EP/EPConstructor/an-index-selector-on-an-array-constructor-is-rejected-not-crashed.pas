(*
Issue #685: the array sibling of
a-field-selector-on-a-record-constructor-is-rejected-not-crashed.pas --
EP §6.8.7's structured-value-constructor is an expression, not a
variable-access, so `Row[1:7;2:8;3:9][3]` has no legal EP meaning either.
Before err_selector_on_structured_value existed to catch it in Sema, this
shape reached CodeGen's indexed-variable lowering with no address to index
through and did not stop there the clean way the field case did.
*)

(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: cannot select a component of a structured-value-constructor result
*)

program p;
type Row = array[1..3] of integer;
begin
  writeln(Row[1: 7; 2: 8; 3: 9][3])
end.
