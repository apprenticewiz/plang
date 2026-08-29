(*
Inc(p[, n]) / Dec(p[, n]) on a PChar-like typed pointer (isCharPointerType,
Type.h) go no further than `p + n` / `p - n` already do (CGBinaryOps' own
pointer-arithmetic arm, gated the identical way): a GEP scaled by the
pointee's own size, matching the same scope checkBinary already allows.
Extending Inc/Dec to arbitrary typed pointers beyond PChar-like ones would
first need ordinary `+`/`-` pointer arithmetic to support that, which it
does not yet (a separate, not-yet-landed change) -- see Sema::checkCallStmt
and CGProcCall's own Inc/Dec comments for this deliberate scope line.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:d
CHECK-NEXT:b
CHECK-NEXT:j
*)

program p;
var
  ptr: PChar;
  buf: array[0 .. 9] of Char;
  k: Integer;
begin
  for k := 0 to 9 do buf[k] := Chr(Ord('a') + k);
  ptr := @buf[0];
  Inc(ptr, 3); writeln(ptr^);
  Dec(ptr, 2); writeln(ptr^);
  Inc(ptr, 8); writeln(ptr^);
end.
