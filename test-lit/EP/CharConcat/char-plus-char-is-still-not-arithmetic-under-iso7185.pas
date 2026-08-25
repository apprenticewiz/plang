(*
RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: requires numeric operands
*)

program p(output); var c: char; begin c := 'a' + 'b' end.
