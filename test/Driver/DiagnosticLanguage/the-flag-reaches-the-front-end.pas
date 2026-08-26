(*
RUN: %plang -fdiagnostics-language=qps_ploc %s -o %t 2> %t.err; true
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: [!
*)

program p(output);
begin x := 1 end.
