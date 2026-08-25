(*
RUN: %plang -fdiagnostics-language=C %s -o %t 2> %t.err; true
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: [!
*)

program p(output);
begin x := 1 end.
