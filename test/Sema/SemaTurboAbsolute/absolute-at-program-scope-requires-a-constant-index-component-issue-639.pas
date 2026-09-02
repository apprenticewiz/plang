(*
Issue #639: unlike the LOCAL case (absolute-inside-a-procedure-can-overlay-
a-component.pas, test/CodeGen/Turbo), a program-scope 'absolute' has to
resolve its target to a compile-time-constant ADDRESS -- no builder exists
yet to compute one at run time when globals are declared (CodeGenProcs.cpp's
emitGlobals).  A non-constant index used to reach CodeGen anyway and hit an
internal error (LLVM ERROR, no source location); Sema now refuses it with a
located diagnostic instead, the same restriction fpc -Mtp enforces for the
identical program ("Illegal expression"; confirmed empirically).

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'absolute' at program scope can only overlay a component reached by compile-time-constant array indices
*)

var
  Arr: array[1..4] of Byte;
  I: Integer;
  W: Word absolute Arr[I];
begin
  I := 1;
  writeln(W);
end.
