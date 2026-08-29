(*
'absolute' declared INSIDE a procedure can overlay any addressable
designator, not only a bare variable name -- unlike the program-scope case
(see absolute-overlays-shared-storage.pas's own comment), a local
declaration's storage is wired up in emitBlockAllocas (CodeGenProcs.cpp),
which runs with a real entry block already open, so emitLValue can address
a component ('B[1]') the same way it would for '@B[1]' or a var-parameter
argument.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:B
CHECK-NEXT:Z
*)

procedure P;
var
  B: array[0..2] of Char;
  W: Char absolute B[1];
begin
  B[0] := Chr(65);
  B[1] := Chr(66);
  B[2] := Chr(67);
  writeln(W);
  B[1] := Chr(90);
  writeln(W);
end;

begin
  P;
end.
