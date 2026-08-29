(*
Turbo's `Pointer` (TypeContext::getGenericPointer) is the untyped pointer
type: unlike `^Integer` and `^Char`, which are only assignment-compatible
with themselves (ISO Sec 6.4.4 -- domain types must be identical), `Pointer`
has no domain type at all and is compatible with every pointer type in
BOTH directions, and with another `Pointer` too.  This is Sema::
isAssignCompatible's Pointer/Pointer arm (the null-PointeeType carve-out)
and the matching carve-out a few lines above it in the '=' / '<>' operator
check -- both needed fixing, not just the first: `p = q` is a comparison,
not an assignment, and reaches a separate check.

Also confirms `=`/`<>` still tell two distinct objects apart through a
generic Pointer on either side, so the carve-out is "no domain check", not
"every pointer equals every other pointer".
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:eq-typed-to-generic
CHECK-NEXT:ne-different-object
CHECK-NEXT:eq-generic-to-generic
CHECK-NEXT:42
CHECK-NEXT:eq-generic-back-to-typed
*)

program p;
var
  p1, p2:  Pointer;
  q, q2:   ^Integer;
begin
  new(q);
  q^ := 42;

  (* typed -> generic, then compared *)
  p1 := q;
  if p1 = q then writeln('eq-typed-to-generic') else writeln('FAIL1');

  (* a second, distinct typed pointer is not confused with the first *)
  new(q);
  q^ := 7;
  if p1 = q then writeln('FAIL2') else writeln('ne-different-object');

  (* generic -> generic *)
  p2 := p1;
  if p1 = p2 then writeln('eq-generic-to-generic') else writeln('FAIL3');

  (* generic -> typed (the reverse direction) *)
  q2 := p1;
  writeln(q2^);
  if q2 = p1 then writeln('eq-generic-back-to-typed') else writeln('FAIL4')
end.
