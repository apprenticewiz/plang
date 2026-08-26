(*
RUN: %plang -fdiagnostics-language=zz_ZZ %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:ok
*)

program p(output);
begin writeln('ok') end.
