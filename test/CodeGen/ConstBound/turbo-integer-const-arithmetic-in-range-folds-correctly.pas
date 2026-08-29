(*
Sibling of turbo-integer-const-arithmetic-overflow-is-rejected.pas: the
width-generic bounds check newly threaded through constBoundImpl and
ConstFold.cpp must not fire for an arithmetic constant that genuinely DOES
fit Turbo's 16-bit Integer -- 30000 + 2000 = 32000, one below that width's
maxint.  No diagnostic, and the value read back at run time is the honest
sum, not an artifact of the width check (or, pre-fix, of a silent 64-to-16
truncation that happened to also compute 32000 here only by accident of
the numbers chosen).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:32000
*)

program t;
const Ok = 30000 + 2000;
var x: Integer;
begin
  x := Ok;
  writeln(x)
end.
