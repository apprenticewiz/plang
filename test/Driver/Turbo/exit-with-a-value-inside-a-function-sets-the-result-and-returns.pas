(*
TP-only Exit(value): FPC's accepted extension to strict Delphi's
argument-less Exit, legal only inside a function -- Exit(42) both sets the
function's result and returns immediately, skipping whatever else the body
would otherwise have done (including its own `F := 99` past the Exit).
Confirmed against `fpc -Mtp`: an identical program prints "in F" then
"r=42", never assigning 99.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t > %t.out
RUN: FileCheck %s < %t.out
*)

(*
CHECK: in F
CHECK-NEXT: r=42
*)

program exit_with_value;

function F: Integer;
begin
  writeln('in F');
  Exit(42);
  F := 99
end;

var r: Integer;
begin
  r := F;
  writeln('r=', r)
end.
