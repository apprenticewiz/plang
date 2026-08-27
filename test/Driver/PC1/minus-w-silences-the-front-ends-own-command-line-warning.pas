(*
Issue #276: the front end's own command-line diagnostics -- an unknown
-W name, here -- used to print straight to std::cerr instead of going
through DiagnosticsEngine, so -w had no effect on them at all.
*)

(*
RUN: %plang_ir -pc1 -w -Wbogus-warning-name %s -o %t.ir > %t.out 2>&1; true
RUN: FileCheck --allow-empty --check-prefix=OUT-ABSENT %s < %t.out
*)

(*
OUT-ABSENT-NOT: unknown warning
*)

program p;
begin end.
