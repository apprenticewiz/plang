(*
RunError (Builtins.def) aborts the program the same way Halt does, so it
belongs in alwaysTransfers' (SemaStmt.cpp) list of builtins a statement
sequence never returns past -- same mechanism as Halt/Exit, resolved
through the symbol table's BuiltinID rather than the call's spelling.
*)

(*
RUN: %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: this statement cannot be reached
*)

program p;
begin
  RunError(5);
  writeln('dead')
end.
