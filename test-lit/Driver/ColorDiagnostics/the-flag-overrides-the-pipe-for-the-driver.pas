(*
-fcolor-diagnostics forces color even though the driver's own output is
going to a pipe rather than a terminal.

RUN: %plang_ir -fcolor-diagnostics nosuchfile.pas > %t.out 2>&1; true
RUN: FileCheck %s < %t.out
*)

(*
CHECK: [1;31merror[0m
*)
