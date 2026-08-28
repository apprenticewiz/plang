(*
None of the real-mode-DOS names (Mem, Seg, Intr, ...) are reserved words or
Symbol table entries -- checkRealModeDosName (Sema.h) only ever runs once
ordinary name resolution has already come back empty.  A program that
declares its own variable named Mem must resolve to THAT declaration and
never even reach the rejection check, exactly like declaring a variable named
any other identifier would.  Compiled and run, not just type-checked, so a
regression that only broke codegen (and not Sema) would still be caught.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5
*)

program p;
var Mem: integer;
begin
  Mem := 5;
  writeln(Mem)
end.
