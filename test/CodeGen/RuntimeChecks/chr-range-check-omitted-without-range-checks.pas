(*
RUN: %plang -fno-range-checks %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:0 255
*)

(*
issue #166: chr's new range check is gated by -frange-checks/-fno-range-checks
like every other RangeCheckGuards check, not unconditional (contrast the
nil-deref and div-by-zero checks, which stay on under -fno-range-checks).
With it off, chr(256) and chr(-1) keep truncating to i8 exactly as before
this fix -- 0 and 255 respectively -- rather than trapping.
*)

program p;
begin writeln(ord(chr(256)), ' ', ord(chr(-1))) end.
