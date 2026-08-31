(*
Issue #178 gave a NAMED array type-denoter its own unique, uncached Type
(TypeContext::makeArrayUncached) instead of sharing TypeContext::getArray's
interned slot for its shape.  Two variables declared with the SAME name --
`var a, b: TArr` -- must still resolve to that one shared symbol-table Type
object and remain trivially assignable; this is the regression a fix that
instead minted a fresh, unshared Type PER USE (rather than per declaration)
would have introduced.

RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:9
*)

program p;
type TArr = array[1..5] of Integer;
var a, b: TArr;
begin a[1] := 9; b := a; writeln(b[1]) end.
