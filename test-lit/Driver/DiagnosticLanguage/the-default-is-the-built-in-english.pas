(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR: not an assignable variable
ERR-ABSENT-NOT: [!
*)

program p(output);
begin x := 1 end.
