(*
RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: at least one character
*)

program p(output); begin writeln('') end.
