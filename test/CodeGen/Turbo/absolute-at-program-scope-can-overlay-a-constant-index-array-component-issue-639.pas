(*
Issue #639: a program-scope 'absolute' naming a COMPONENT of a global
('absolute Arr[1]', as opposed to a bare 'absolute Arr') used to hit an
internal error (LLVM ERROR, no source location) instead of compiling --
CodeGen's emitGlobals only ever resolved a bare variable name, since
globals are declared before any function (including main) exists to run
instructions in, and had no way to fold a component designator's address
at compile time.  globalAbsoluteAddr (CodeGenProcs.cpp) now folds an
IdentExpr base under any number of constant-index IndexExpr layers to a
`getelementptr` CONSTANT EXPRESSION, matching what a real fpc -Mtp does
with the identical program (confirmed empirically: 513, the little-endian
Word of bytes 1 and 2).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:513
*)

var
  Arr: array[1..4] of Byte;
  W: Word absolute Arr[1];
begin
  Arr[1] := 1;
  Arr[2] := 2;
  writeln(W);
end.
