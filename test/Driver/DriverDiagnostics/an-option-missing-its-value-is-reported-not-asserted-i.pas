(*
An option that takes a value but is given none is a driver error with
a real message and a real exit status, not an assertion failure.  This
file checks -I.
*)

(*
RUN: not %plang_ir -I > %t.out 2>&1
RUN: FileCheck --strict-whitespace --match-full-lines %s < %t.out
*)

(*
CHECK:plang: error: -I requires an argument
*)
