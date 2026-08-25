(*
An option that takes a value but is given none is a driver error with
a real message and a real exit status, not an assertion failure.  This
file checks -Xlinker.
*)

(*
RUN: not %plang_ir -Xlinker > %t.out 2>&1
RUN: FileCheck --strict-whitespace --match-full-lines %s < %t.out
*)

(*
CHECK:plang: error: -Xlinker requires an argument
*)
