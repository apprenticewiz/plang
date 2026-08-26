(*
RUN: %plang_ir -fdiagnostics-language=qps_ploc > %t.out 2>&1; true
RUN: FileCheck --check-prefix=OUT %s < %t.out
*)

(*
OUT: [!no input files!]
*)
