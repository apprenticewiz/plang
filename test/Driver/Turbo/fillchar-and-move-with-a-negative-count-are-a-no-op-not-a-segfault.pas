(*
Issue #628: FillChar(var X; Count: Integer; Value) and Move(const Source;
var Dest; Count: Integer) both pass Count straight to CreateMemSet/
CreateMemMove (CGProcCall::emitCallStmt) as an i64, which those LLVM
intrinsics both consume as an UNSIGNED size -- a negative Count silently
became a size near SIZE_MAX and segfaulted the process (confirmed on
unmodified main: exit 139, core dumped, for both FillChar(s, -5, 65) and
Move(s[1], s[5], -3)).  Real Turbo/FPC (`fpc -Mtp`) instead treats a
negative Count as a no-op and exits cleanly.  Fixed by clamping a negative
Count to zero before either intrinsic ever sees it, matching that
reference behaviour, exercised here at both -O0 and -O2 and for both
builtins, confirming neither the destination nor (for Move) any byte
outside the untouched range is disturbed.

RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --match-full-lines %s
*)

program p;
var
  s: array[1 .. 20] of Byte;
  k: Integer;
begin
  for k := 1 to 20 do s[k] := k;

  { A negative FillChar Count must touch nothing at all. }
  FillChar(s, -5, 65);
  for k := 1 to 20 do write(s[k], ' ');
  writeln;

  { A negative Move Count must copy nothing at all either. }
  for k := 1 to 20 do s[k] := k;
  Move(s[1], s[5], -3);
  for k := 1 to 20 do write(s[k], ' ');
  writeln;

  writeln('done');
end.

(*
CHECK:1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20
CHECK-NEXT:1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20
CHECK-NEXT:done
*)
