(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: no module named 'nowhere'
*)

program p;
  import nowhere;
begin end.
