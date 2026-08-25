(*
RUN: %plang_ir -fno-color-diagnostics nosuchfile.pas > %t.out 2>&1; true
RUN: FileCheck --strict-whitespace --match-full-lines %s < %t.out
*)

(*
CHECK:plang: error: no such file or directory: 'nosuchfile.pas'
*)
