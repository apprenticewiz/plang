(*
BuiltinIO::emitReadArg computed readTy (the width plang_read_i64 actually
writes through the destination pointer) by copying the destination's own
LLVM type, overriding it only for Real/Char.  Turbo's Integer is i16, so
readTy stayed i16 while the chosen reader was always plang_read_i64 (an
8-byte write) -- an 8-byte store through a 2-byte stack slot, corrupting
whatever sits six bytes past it.  readTy now defaults to i64 (the width
every non-double/non-char reader actually is) and only narrows back down
through the pre-existing convert/CoerceToType path, which read(a[i]) already
relied on for a different width mismatch.  The second variable here is what
would have been clobbered by the overflow if the fix regressed.
*)

(*
RUN: split-file %s %t.dir
RUN: %plang -std=turbo %t.dir/test.pas -o %t
RUN: %run %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:12345 99
*)

//--- test.pas
program p;
var i: Integer;
    guard: Integer;
begin
  guard := 99;
  read(i);
  writeln(i, ' ', guard)
end.

//--- stdin.txt
12345
