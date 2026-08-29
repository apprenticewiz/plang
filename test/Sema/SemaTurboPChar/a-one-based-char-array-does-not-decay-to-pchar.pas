(*
The negative side of the zero-based decay rule (Sema::isAssignCompatible,
SemaExpr.cpp): a 1-based `array[1..n] of char` is ISO §6.4.3.2's canonical
string-type (isCharStringType, include/plang/Sema/Type.h, unchanged by this
feature) and must NOT decay to PChar the way a 0-based array does -- real
`fpc -Mtp` refuses the identical program ("Incompatible types: got
"Array[1..9] Of Char" expected "PChar""), and plang's own decay rule is
written to check IndexType->SubLo == 0 specifically rather than widen
isCharStringType's own SubLo == 1 condition, which every other caller
(string concatenation, comparison, length/substr/trim) still depends on
meaning exactly what it always has.

See pchar-pointer-arithmetic-indexing-and-array-decay.pas
(test/CodeGen/Turbo) for the positive, 0-based case this contrasts with.

RUN: not %plang -std=turbo %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: cannot assign 'array[1..9] of char' to variable of type 'PChar'
*)

program p;
var
  ptr: PChar;
  buf: array[1..9] of Char;
begin
  ptr := buf;
end.
