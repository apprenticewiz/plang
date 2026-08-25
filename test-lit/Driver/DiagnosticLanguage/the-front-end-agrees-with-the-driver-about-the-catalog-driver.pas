(*
Two processes resolving independently; if they disagreed, half a
compilation's messages would be in one language and half in the other.
This is the driver's own resolution; paired with
the-front-end-agrees-with-the-driver-about-the-catalog-front-end.pas, which
pins the identical line from the front end.

RUN: %plang_ir -fdiagnostics-language=qps_ploc --version > %t.out 2>&1; true
RUN: FileCheck --strict-whitespace --match-full-lines %s < %t.out
*)

(*
CHECK:Messages: qps_ploc (/home/cparrott/Projects/plang/build/share/plang/locale/qps_ploc.po)
*)
