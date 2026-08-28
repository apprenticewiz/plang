(*
alwaysTransfers (SemaStmt.cpp) already recognized Halt as a required
procedure that never returns to what follows it; TP's Exit (Builtins.def)
is the same shape -- a call resolved through the symbol table to
BuiltinID::Exit (builtinAlwaysTransfers, BuiltinIDs.h), not matched on how
the name is spelled -- so a statement right after one, in the same
sequence, is unreachable the same way one right after Halt already was.
*)

(*
RUN: %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: this statement cannot be reached
*)

program p;
function F: Integer;
begin
  F := 1;
  Exit;
  writeln('dead')
end;
var r: Integer;
begin
  r := F
end.
