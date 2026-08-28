(*
Regression gate: Exit/Break/Continue are TP-only Builtins.def entries
(Dialects = TP, like Assert), so under -std=iso7185 or -std=iso10206 they
must be refused the same way every other required name only one dialect
declares is -- with err_turbo_required_name's own wording ("is a Turbo
Pascal extension"), not err_ep_required_name's (which would wrongly promise
-std=iso10206 accepts it) and not a plain "undefined identifier" either,
matching the treatment every other TP-only name already gets (see Assert's
own regression test, assert-is-turbo-only-...).  None of the three needs an
enclosing loop or function to be reached here: checkEPOnly's dialect gate
runs before checkCallStmt's own Break/Continue/Exit arms even ask that
question, so a bare `Break;` at the program's own top level is refused for
being Turbo-only well before "outside a loop" would ever come up.
*)

(*
RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK-DAG: 'exit' is a Turbo Pascal extension and is only available under -std=turbo
CHECK-DAG: 'break' is a Turbo Pascal extension and is only available under -std=turbo
CHECK-DAG: 'continue' is a Turbo Pascal extension and is only available under -std=turbo
*)

program p;
begin
  Exit;
  Break;
  Continue
end.
